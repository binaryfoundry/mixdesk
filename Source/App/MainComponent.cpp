#include "MainComponent.h"

#include "Engine/BeatDetector.h"
#include "Engine/PhraseAnalyzer.h"
#include "Engine/StemPreparation.h"
#include "Engine/WaveformAnalyzer.h"
#include "Model/DemoState.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <thread>
#include <vector>

namespace mixdesk::app
{
namespace
{
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

juce::String deckLabel(model::DeckId deckId)
{
    const auto text = model::toString(deckId);
    return juce::String(text.data(), static_cast<int>(text.size()));
}

void addPreparedStemWaveforms(engine::WaveformAnalyzer& waveformAnalyzer,
    const engine::PreparedStemSet& stems,
    std::vector<model::StemWaveform>& stemWaveforms)
{
    if (stems.drums.hasAudio())
        stemWaveforms.push_back(waveformAnalyzer.analyzeBuffer(stems.drums.audio,
            stems.drums.sampleRate,
            model::StemType::Drums));

    if (stems.bass.hasAudio())
        stemWaveforms.push_back(waveformAnalyzer.analyzeBuffer(stems.bass.audio,
            stems.bass.sampleRate,
            model::StemType::Bass));

    if (stems.musicResidual.hasAudio())
        stemWaveforms.push_back(waveformAnalyzer.analyzeBuffer(stems.musicResidual.audio,
            stems.musicResidual.sampleRate,
            model::StemType::MusicResidual));

    if (stems.vocals.hasAudio())
        stemWaveforms.push_back(waveformAnalyzer.analyzeBuffer(stems.vocals.audio,
            stems.vocals.sampleRate,
            model::StemType::Vocals));
}
}

struct TrackLoadResult
{
    int requestId {};
    model::DeckId deckId { model::DeckId::A };
    int launchOffsetBars {};
    bool succeeded {};
    bool hasPreparedAudio {};
    model::LoadedTrack track;
    model::BeatGrid beatGrid;
    std::vector<model::StemWaveform> stemWaveforms;
    std::vector<model::PhraseBlock> phraseBlocks;
    engine::PreparedStemSet preparedStems;
    juce::String message;
};

namespace
{
std::shared_ptr<TrackLoadResult> loadTrackForDeck(int requestId,
    model::DeckId deckId,
    int launchOffsetBars,
    const juce::File& metadataFile)
{
    auto result = std::make_shared<TrackLoadResult>();
    result->requestId = requestId;
    result->deckId = deckId;
    result->launchOffsetBars = launchOffsetBars;

    const auto bundle = engine::loadTrackBundleFromMixdeskJson(metadataFile);
    if (! bundle.has_value())
    {
        result->message = "Could not read " + metadataFile.getFullPathName();
        return result;
    }

    engine::StemPreparation stemPreparation;
    auto preparedStemSet = stemPreparation.loadPreparedStemSet(bundle->primaryAudioFile,
        bundle->noVocalsStemFile,
        bundle->drumStemFile,
        bundle->bassStemFile,
        bundle->vocalStemFile,
        bundle->noBassStemFile,
        bundle->noDrumStemFile);

    if (! preparedStemSet.succeeded)
    {
        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();

        if (auto fullMix = engine::readStemAudioFile(formatManager, bundle->primaryAudioFile, model::StemType::FullMix))
        {
            preparedStemSet.stems = {};
            preparedStemSet.stems.fullMix = std::move(*fullMix);
            preparedStemSet.succeeded = true;
            preparedStemSet.message = "Loaded primary audio as FullMix fallback after stem preparation failed: "
                + preparedStemSet.message;
        }
    }

    engine::BeatDetector beatDetector;
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
            result->message = "Beat analysis failed for " + juce::String(bundle->loadedTrack.name)
                + ": " + analysis.message;
            return result;
        }
    }

    result->track = bundle->loadedTrack;
    result->hasPreparedAudio = preparedStemSet.succeeded;

    if (result->hasPreparedAudio)
    {
        result->track.stems = engine::createModelStemSet(preparedStemSet.stems);
        result->track.fullMixPath = result->track.stems.fullMixPath.value_or(std::string());
        result->track.instrumentalStemPath = result->track.stems.instrumentalOriginalPath.value_or(std::string());
        result->track.drumStemPath = result->track.stems.drumsPath.value_or(std::string());
        result->track.bassStemPath = result->track.stems.bassPath.value_or(std::string());
        result->track.vocalStemPath = result->track.stems.vocalsPath.value_or(std::string());
        result->track.musicResidualStemPath = result->track.stems.musicResidualPath.value_or(std::string());
        result->track.noBassStemPath = result->track.stems.noBassPath.value_or(std::string());
        result->track.noDrumStemPath = result->track.stems.noDrumsPath.value_or(std::string());
        result->track.noVocalsStemPath = result->track.stems.noVocalsPath.value_or(std::string());
    }

    if (result->track.durationSeconds <= 0.0)
    {
        result->track.durationSeconds = result->hasPreparedAudio
            ? engine::preparedStemSetDurationSeconds(preparedStemSet.stems)
            : analysis.beatGrid.durationSeconds;
    }

    analysis.beatGrid.durationSeconds = result->track.durationSeconds > 0.0
        ? result->track.durationSeconds
        : analysis.beatGrid.durationSeconds;
    result->beatGrid = analysis.beatGrid;

    engine::WaveformAnalyzer waveformAnalyzer;
    result->stemWaveforms.reserve(4);

    if (result->hasPreparedAudio)
    {
        addPreparedStemWaveforms(waveformAnalyzer, preparedStemSet.stems, result->stemWaveforms);
    }
    else
    {
        result->stemWaveforms.push_back(waveformAnalyzer.analyzeStem(bundle->drumStemFile, model::StemType::Drums));

        if (bundle->bassStemFile.existsAsFile())
            result->stemWaveforms.push_back(waveformAnalyzer.analyzeStem(bundle->bassStemFile, model::StemType::Bass));

        if (bundle->vocalStemFile.existsAsFile())
            result->stemWaveforms.push_back(waveformAnalyzer.analyzeStem(bundle->vocalStemFile, model::StemType::Vocals));
    }

    engine::PhraseAnalyzer phraseAnalyzer;
    result->phraseBlocks = phraseAnalyzer.analyze(result->beatGrid, result->stemWaveforms, 8);
    result->preparedStems = std::move(preparedStemSet.stems);
    result->message = preparedStemSet.message;
    result->succeeded = true;
    return result;
}
} // namespace

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

    phraseWorkspace->onTrackLoadRequested = [this](model::DeckId deckId, int launchOffsetBars)
    {
        showTrackSelectionDialog(deckId, launchOffsetBars);
    };

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

void MainComponent::showTrackSelectionDialog(model::DeckId deckId, int launchOffsetBars)
{
    const auto tracksRoot = appSettings.getTracksRoot();
    auto catalog = std::make_shared<std::vector<engine::TrackCatalogEntry>>(engine::findTrackCatalog(tracksRoot));

    if (catalog->empty())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "No Tracks Found",
            "No mixdesk.json tracks were found under " + tracksRoot.getFullPathName()
                + "\n\nEdit " + appSettings.getSettingsFile().getFullPathName() + " to change tracksRoot.");
        return;
    }

    phraseWorkspace->showPendingTrackLoadMarker(deckId, launchOffsetBars);

    juce::StringArray trackNames;
    for (const auto& track : *catalog)
        trackNames.add(track.displayName);

    auto* alert = new juce::AlertWindow("Load Track",
        "Choose a track for " + deckLabel(deckId) + " at bar " + juce::String(launchOffsetBars) + ".",
        juce::AlertWindow::NoIcon);
    alert->addComboBox("track", trackNames, "Track");
    alert->addButton("Load", 1, juce::KeyPress(juce::KeyPress::returnKey));
    alert->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<MainComponent> safeThis(this);
    alert->enterModalState(true,
        juce::ModalCallbackFunction::create(
        [safeThis, catalog, alertPtr = alert, deckId, launchOffsetBars](int result)
        {
            if (safeThis == nullptr)
                return;

            if (result != 1)
            {
                safeThis->phraseWorkspace->clearPendingTrackLoadMarker();
                return;
            }

            const auto* combo = alertPtr->getComboBoxComponent("track");
            if (combo == nullptr)
            {
                safeThis->phraseWorkspace->clearPendingTrackLoadMarker();
                return;
            }

            const auto selectedIndex = combo->getSelectedItemIndex();
            if (selectedIndex < 0 || selectedIndex >= static_cast<int>(catalog->size()))
            {
                safeThis->phraseWorkspace->clearPendingTrackLoadMarker();
                return;
            }

            safeThis->beginLoadTrack(deckId, launchOffsetBars, (*catalog)[static_cast<std::size_t>(selectedIndex)].metadataFile);
        }),
        true);
}

void MainComponent::beginLoadTrack(model::DeckId deckId, int launchOffsetBars, const juce::File& metadataFile)
{
    const auto requestId = nextTrackLoadRequestId.fetch_add(1);
    activeTrackLoadRequestId = requestId;
    phraseWorkspace->showPendingTrackLoadMarker(deckId, launchOffsetBars);
    juce::Logger::writeToLog("Loading " + metadataFile.getFullPathName() + " for " + deckLabel(deckId));

    juce::Component::SafePointer<MainComponent> safeThis(this);
    std::thread([safeThis, requestId, deckId, launchOffsetBars, metadataFile]
    {
        auto result = loadTrackForDeck(requestId, deckId, launchOffsetBars, metadataFile);
        juce::MessageManager::callAsync([safeThis, result = std::move(result)]
        {
            if (safeThis != nullptr)
                safeThis->applyLoadedTrackResult(result);
        });
    }).detach();
}

void MainComponent::applyLoadedTrackResult(std::shared_ptr<TrackLoadResult> result)
{
    if (result == nullptr)
        return;

    if (! result->succeeded)
    {
        if (result->requestId == activeTrackLoadRequestId)
            phraseWorkspace->clearPendingTrackLoadMarker();

        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Track Load Failed", result->message);
        return;
    }

    juce::Logger::writeToLog("Track load complete: " + juce::String(result->track.name) + "\n" + result->message);

    if (result->deckId == model::DeckId::A)
    {
        deckOneLaunchOffsetBars = result->launchOffsetBars;
        deckOneBeatGrid = result->beatGrid;

        if (result->hasPreparedAudio)
        {
            if (! deckPlaybackEngine.loadPreparedStemSet(std::move(result->preparedStems)))
            {
                if (result->requestId == activeTrackLoadRequestId)
                    phraseWorkspace->clearPendingTrackLoadMarker();

                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                    "Track Load Failed",
                    "Prepared audio could not be loaded for Deck A.");
                return;
            }
        }

        configureDeckOneGridPlayback();
    }

    workspaceController.dispatch(engine::SetDeckLaunchOffsetCommand { result->deckId, result->launchOffsetBars });
    workspaceController.dispatch(engine::SetDeckLoadedTrackCommand {
        result->deckId,
        std::move(result->track),
        result->beatGrid,
        std::move(result->stemWaveforms),
        std::move(result->phraseBlocks)
    });
    workspaceController.dispatch(engine::SetCurrentBarPositionCommand { deckPlaybackEngine.getCurrentGridBarPosition() });
    if (result->requestId == activeTrackLoadRequestId)
        phraseWorkspace->clearPendingTrackLoadMarker();

    refreshWorkspaceSnapshot();
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
