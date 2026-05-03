#include "MainComponent.h"

#include "Model/DemoState.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace mixdesk::app
{
namespace
{
const juce::File tracksRoot { "D:\\tracks" };

std::vector<double> makeFallbackBeatTimes(double durationSeconds, double secondsPerBeat)
{
    std::vector<double> beatTimes;

    if (durationSeconds <= 0.0 || secondsPerBeat <= 0.0)
        return beatTimes;

    beatTimes.reserve(static_cast<std::size_t>(durationSeconds / secondsPerBeat) + 2);

    for (auto beatTime = 0.0; beatTime <= durationSeconds; beatTime += secondsPerBeat)
        beatTimes.push_back(beatTime);

    return beatTimes;
}
}

MainComponent::MainComponent()
    : workspaceController(model::createDemoWorkspaceState())
{
    phraseWorkspace = std::make_unique<ui::PhraseWorkspace>();
    addAndMakeVisible(*phraseWorkspace);

    phraseWorkspace->onPhraseBlockMoved = [this](model::DeckId deckId, std::size_t blockIndex, int newStartBar)
    {
        workspaceController.dispatch(engine::MovePhraseBlockCommand { deckId, blockIndex, newStartBar });
        refreshWorkspaceSnapshot();
    };

    phraseWorkspace->onLaunchOffsetNudged = [this](model::DeckId deckId, int deltaBars)
    {
        if (deckId == model::DeckId::A)
        {
            deckOneLaunchOffsetBars += deltaBars;
            deckPlaybackEngine.setLaunchOffsetBars(deckOneLaunchOffsetBars);
        }

        workspaceController.dispatch(engine::NudgeLaunchOffsetCommand { deckId, deltaBars });
        refreshWorkspaceSnapshot();
    };

    phraseWorkspace->onTrackLaunchOffsetMoved = [this](model::DeckId deckId, int launchOffsetBars)
    {
        if (deckId == model::DeckId::A)
        {
            deckOneLaunchOffsetBars = launchOffsetBars;
            deckPlaybackEngine.setLaunchOffsetBars(deckOneLaunchOffsetBars);
        }

        workspaceController.dispatch(engine::SetDeckLaunchOffsetCommand { deckId, launchOffsetBars });
        refreshWorkspaceSnapshot();
    };

    phraseWorkspace->onRoleChangeRequested = [this](model::DeckId deckId, model::DeckRole role)
    {
        workspaceController.dispatch(engine::SetDeckRoleCommand { deckId, role });
        refreshWorkspaceSnapshot();
    };

    phraseWorkspace->onPlaybackToggleRequested = [this]
    {
        togglePlayback();
    };

    phraseWorkspace->onStemToggleRequested = [this](model::DeckId deckId, model::StemType stemType, bool enabled)
    {
        workspaceController.dispatch(engine::SetDeckStemEnabledCommand { deckId, stemType, enabled });

        if (deckId == model::DeckId::A)
            deckPlaybackEngine.setStemEnabled(stemType, enabled);

        refreshWorkspaceSnapshot();
    };

    phraseWorkspace->onMasterVolumeChanged = [this](float volume)
    {
        workspaceController.dispatch(engine::SetMasterVolumeCommand { volume });
        deckPlaybackEngine.setMasterVolume(volume);
        refreshWorkspaceSnapshot();
    };

    phraseWorkspace->onBpmChanged = [this](double bpm)
    {
        workspaceController.dispatch(engine::SetBpmCommand { bpm });
        deckPlaybackEngine.setGlobalBpm(bpm, deckOneBeatGrid.has_value() ? deckOneBeatGrid->beatsPerBar : 4);
        refreshWorkspaceSnapshot();
    };

    loadDeckOneFromTracksFolder();
    refreshWorkspaceSnapshot();

    setAudioChannels(0, 2);
    startTimerHz(30);
    setSize(1280, 760);
}

MainComponent::~MainComponent()
{
    shutdownAudio();
}

void MainComponent::resized()
{
    phraseWorkspace->setBounds(getLocalBounds());
}

void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    deckPlaybackEngine.prepareToPlay(samplesPerBlockExpected, sampleRate);
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    deckPlaybackEngine.getNextAudioBlock(bufferToFill);
}

void MainComponent::releaseResources()
{
    deckPlaybackEngine.releaseResources();
}

void MainComponent::timerCallback()
{
    const auto isPlaying = deckPlaybackEngine.isPlaying();
    for (const auto deckId : { model::DeckId::A, model::DeckId::B, model::DeckId::C })
        workspaceController.dispatch(engine::SetDeckPlayingCommand { deckId, isPlaying });

    workspaceController.dispatch(engine::SetCurrentBarPositionCommand { deckPlaybackEngine.getCurrentGridBarPosition() });

    refreshWorkspaceSnapshot();
}

void MainComponent::loadDeckOneFromTracksFolder()
{
    const auto metadataFile = engine::findFirstMixdeskJson(tracksRoot);
    if (! metadataFile.has_value())
        return;

    const auto bundle = engine::loadTrackBundleFromMixdeskJson(*metadataFile);
    if (! bundle.has_value())
        return;

    auto preparedStemSet = stemPreparation.loadPreparedStemSet(bundle->primaryAudioFile,
        bundle->instrumentalStemFile,
        bundle->drumStemFile,
        bundle->bassStemFile,
        bundle->vocalStemFile);
    juce::Logger::writeToLog("Stem preparation: " + preparedStemSet.message);

    const auto hasPreparedStemSet = preparedStemSet.succeeded;
    if (! hasPreparedStemSet && ! deckPlaybackEngine.loadFile(bundle->primaryAudioFile))
        return;

    auto analysis = beatDetector.analyzeDrumStem(bundle->drumStemFile);
    if (! analysis.succeeded)
    {
        if (bundle->metadataBpm > 0.0)
        {
            analysis.beatGrid.bpm = static_cast<int>(std::round(bundle->metadataBpm));
            analysis.beatGrid.tempo = bundle->metadataBpm;
            analysis.beatGrid.secondsPerBeat = 60.0 / bundle->metadataBpm;
            analysis.beatGrid.beatsPerBar = 4;
            analysis.beatGrid.durationSeconds = bundle->loadedTrack.durationSeconds;
            analysis.beatGrid.beatTimesSeconds = makeFallbackBeatTimes(analysis.beatGrid.durationSeconds, analysis.beatGrid.secondsPerBeat);
            analysis.succeeded = true;
        }
        else
        {
            return;
        }
    }

    auto track = bundle->loadedTrack;
    if (hasPreparedStemSet)
    {
        track.stems = engine::createModelStemSet(preparedStemSet.stems);
        track.fullMixPath = track.stems.fullMixPath.value_or(std::string());
        track.instrumentalStemPath = track.stems.instrumentalOriginalPath.value_or(std::string());
        track.drumStemPath = track.stems.drumsPath.value_or(std::string());
        track.bassStemPath = track.stems.bassPath.value_or(std::string());
        track.vocalStemPath = track.stems.vocalsPath.value_or(std::string());
        track.musicResidualStemPath = track.stems.musicResidualPath.value_or(std::string());
    }

    if (track.durationSeconds <= 0.0)
    {
        track.durationSeconds = hasPreparedStemSet
            ? engine::preparedStemSetDurationSeconds(preparedStemSet.stems)
            : deckPlaybackEngine.getLengthSeconds();
    }

    analysis.beatGrid.durationSeconds = track.durationSeconds > 0.0 ? track.durationSeconds : analysis.beatGrid.durationSeconds;
    deckOneBeatGrid = analysis.beatGrid;

    // TODO(waveform rendering): move this load-time analysis onto a background job once track loading is interactive.
    std::vector<model::StemWaveform> stemWaveforms;
    stemWaveforms.reserve(4);

    if (hasPreparedStemSet)
    {
        if (preparedStemSet.stems.drums.hasAudio())
            stemWaveforms.push_back(waveformAnalyzer.analyzeBuffer(preparedStemSet.stems.drums.audio,
                preparedStemSet.stems.drums.sampleRate,
                model::StemType::Drums));

        if (preparedStemSet.stems.bass.hasAudio())
            stemWaveforms.push_back(waveformAnalyzer.analyzeBuffer(preparedStemSet.stems.bass.audio,
                preparedStemSet.stems.bass.sampleRate,
                model::StemType::Bass));

        if (preparedStemSet.stems.musicResidual.hasAudio())
            stemWaveforms.push_back(waveformAnalyzer.analyzeBuffer(preparedStemSet.stems.musicResidual.audio,
                preparedStemSet.stems.musicResidual.sampleRate,
                model::StemType::MusicResidual));

        if (preparedStemSet.stems.vocals.hasAudio())
            stemWaveforms.push_back(waveformAnalyzer.analyzeBuffer(preparedStemSet.stems.vocals.audio,
                preparedStemSet.stems.vocals.sampleRate,
                model::StemType::Vocals));
    }
    else
    {
        stemWaveforms.push_back(waveformAnalyzer.analyzeStem(bundle->drumStemFile, model::StemType::Drums));

        if (bundle->bassStemFile.existsAsFile())
            stemWaveforms.push_back(waveformAnalyzer.analyzeStem(bundle->bassStemFile, model::StemType::Bass));

        if (bundle->vocalStemFile.existsAsFile())
            stemWaveforms.push_back(waveformAnalyzer.analyzeStem(bundle->vocalStemFile, model::StemType::Vocals));
    }

    auto phraseBlocks = phraseAnalyzer.analyze(analysis.beatGrid, stemWaveforms, 8);

    if (hasPreparedStemSet && ! deckPlaybackEngine.loadPreparedStemSet(std::move(preparedStemSet.stems)))
        return;

    configureDeckOneGridPlayback();

    workspaceController.dispatch(engine::SetDeckLoadedTrackCommand {
        model::DeckId::A,
        std::move(track),
        analysis.beatGrid,
        std::move(stemWaveforms),
        std::move(phraseBlocks)
    });
    workspaceController.dispatch(engine::SetCurrentBarPositionCommand { deckPlaybackEngine.getCurrentGridBarPosition() });
}

void MainComponent::togglePlayback()
{
    deckPlaybackEngine.togglePlayback();
    const auto isPlaying = deckPlaybackEngine.isPlaying();
    for (const auto deckId : { model::DeckId::A, model::DeckId::B, model::DeckId::C })
        workspaceController.dispatch(engine::SetDeckPlayingCommand { deckId, isPlaying });
    refreshWorkspaceSnapshot();
}

void MainComponent::configureDeckOneGridPlayback()
{
    if (! deckOneBeatGrid.has_value())
        return;

    const auto secondsPerBar = deckOneBeatGrid->secondsPerBeat * static_cast<double>(std::max(1, deckOneBeatGrid->beatsPerBar));
    deckPlaybackEngine.configureGridPlayback(secondsPerBar, deckOneBeatGrid->firstBeatOffsetSeconds, deckOneLaunchOffsetBars);
    deckPlaybackEngine.setGlobalBpm(deckOneBeatGrid->bpm > 0 ? static_cast<double>(deckOneBeatGrid->bpm) : deckOneBeatGrid->tempo,
        deckOneBeatGrid->beatsPerBar);
}

void MainComponent::refreshWorkspaceSnapshot()
{
    phraseWorkspace->setStateSnapshot(workspaceController.createSnapshot());
}
} // namespace mixdesk::app
