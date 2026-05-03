#include "MainComponent.h"

#include "Engine/BeatDetector.h"
#include "Engine/PhraseAnalyzer.h"
#include "Engine/StemPreparation.h"
#include "Engine/WaveformAnalyzer.h"
#include "Model/DemoState.h"

#include <algorithm>
#include <cmath>
#include <future>
#include <memory>
#include <optional>
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

double secondsPerBarFor(double bpm, int beatsPerBar)
{
    if (bpm <= 0.0)
        return 0.0;

    return (60.0 / bpm) * static_cast<double>(std::max(1, beatsPerBar));
}

std::size_t deckSlot(model::DeckId deckId)
{
    return model::deckIndex(deckId);
}

using OptionalStemWaveform = std::optional<model::StemWaveform>;

std::future<OptionalStemWaveform> startPreparedStemWaveformJob(const engine::StemAudioBuffer& stem,
    model::StemType stemType)
{
    return std::async(std::launch::async,
        [&stem, stemType]() -> OptionalStemWaveform
        {
            if (! stem.hasAudio())
                return std::nullopt;

            engine::WaveformAnalyzer waveformAnalyzer;
            return waveformAnalyzer.analyzeBuffer(stem.audio, stem.sampleRate, stemType);
        });
}

void addPreparedStemWaveforms(const engine::PreparedStemSet& stems,
    std::vector<model::StemWaveform>& stemWaveforms)
{
    auto drumsWaveform = startPreparedStemWaveformJob(stems.drums, model::StemType::Drums);
    auto bassWaveform = startPreparedStemWaveformJob(stems.bass, model::StemType::Bass);
    auto musicWaveform = startPreparedStemWaveformJob(stems.musicResidual, model::StemType::MusicResidual);
    auto vocalsWaveform = startPreparedStemWaveformJob(stems.vocals, model::StemType::Vocals);

    for (auto* waveformFuture : { &drumsWaveform, &bassWaveform, &musicWaveform, &vocalsWaveform })
    {
        if (auto waveform = waveformFuture->get())
            stemWaveforms.push_back(std::move(*waveform));
    }
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
    const auto loadStartMs = juce::Time::getMillisecondCounterHiRes();
    auto stageStartMs = loadStartMs;
    juce::String timingReport;
    const auto recordStage = [&stageStartMs, &timingReport](const juce::String& name)
    {
        const auto nowMs = juce::Time::getMillisecondCounterHiRes();
        timingReport += "\n- " + name + ": " + juce::String(nowMs - stageStartMs, 1) + " ms";
        stageStartMs = nowMs;
    };

    const auto bundle = engine::loadTrackBundleFromMixdeskJson(metadataFile);
    if (! bundle.has_value())
    {
        result->message = "Could not read " + metadataFile.getFullPathName();
        return result;
    }
    recordStage("metadata");

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
    recordStage("stem decode/preparation");

    engine::BeatDetector beatDetector;
    const auto canAnalyzePreparedDrums = preparedStemSet.succeeded && preparedStemSet.stems.drums.hasAudio();
    auto beatAnalysisSource = canAnalyzePreparedDrums ? juce::String("aligned playback drums") : juce::String("drum source file");
    auto analysis = canAnalyzePreparedDrums
        ? beatDetector.analyzeBuffer(preparedStemSet.stems.drums.audio,
            preparedStemSet.stems.drums.sampleRate,
            {},
            engine::preparedStemSetDurationSeconds(preparedStemSet.stems))
        : beatDetector.analyzeDrumStem(bundle->drumStemFile);
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
            beatAnalysisSource = "track metadata fallback";
        }
        else
        {
            result->message = "Beat analysis failed for " + juce::String(bundle->loadedTrack.name)
                + ": " + analysis.message;
            return result;
        }
    }
    recordStage("beat grid");

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
        addPreparedStemWaveforms(preparedStemSet.stems, result->stemWaveforms);
    }
    else
    {
        result->stemWaveforms.push_back(waveformAnalyzer.analyzeStem(bundle->drumStemFile, model::StemType::Drums));

        if (bundle->bassStemFile.existsAsFile())
            result->stemWaveforms.push_back(waveformAnalyzer.analyzeStem(bundle->bassStemFile, model::StemType::Bass));

        if (bundle->vocalStemFile.existsAsFile())
            result->stemWaveforms.push_back(waveformAnalyzer.analyzeStem(bundle->vocalStemFile, model::StemType::Vocals));
    }
    recordStage("waveforms");

    engine::PhraseAnalyzer phraseAnalyzer;
    result->phraseBlocks = phraseAnalyzer.analyze(result->beatGrid, result->stemWaveforms, 8);
    recordStage("phrases");
    result->preparedStems = std::move(preparedStemSet.stems);
    result->message = preparedStemSet.message;
    const auto beatGridSecondsPerBar = model::beatGridSecondsPerBar(result->beatGrid);
    const auto audioLeadInBars = beatGridSecondsPerBar > 0.0
        ? result->beatGrid.firstBeatOffsetSeconds / beatGridSecondsPerBar
        : 0.0;
    result->message += "\nBeat grid: " + juce::String(result->beatGrid.tempo, 4)
        + " BPM exact, first beat offset " + juce::String(result->beatGrid.firstBeatOffsetSeconds * 1000.0, 2)
        + " ms / " + juce::String(audioLeadInBars, 4)
        + " bars from file start (" + beatAnalysisSource + ")";
    const auto totalLoadMs = juce::Time::getMillisecondCounterHiRes() - loadStartMs;
    result->message += "\nLoad timings:" + timingReport
        + "\n- total: " + juce::String(totalLoadMs, 1) + " ms";
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
        const auto slot = deckSlot(deckId);
        deckLaunchOffsetBars[slot] += deltaBars;
        playbackEngineFor(deckId).setLaunchOffsetBars(deckLaunchOffsetBars[slot]);

        workspaceController.dispatch(engine::NudgeLaunchOffsetCommand { deckId, deltaBars });
        refreshWorkspaceSnapshot();
    };

    phraseWorkspace->onTrackLaunchOffsetMoved = [this](model::DeckId deckId, int launchOffsetBars)
    {
        const auto slot = deckSlot(deckId);
        deckLaunchOffsetBars[slot] = launchOffsetBars;
        playbackEngineFor(deckId).setLaunchOffsetBars(launchOffsetBars);

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
        syncStemControlsToPlayback();

        refreshWorkspaceSnapshot();
    };

    phraseWorkspace->onStemVolumeChanged = [this](model::DeckId deckId, model::StemType stemType, float volume)
    {
        workspaceController.dispatch(engine::SetDeckStemVolumeCommand { deckId, stemType, volume });
        syncStemControlsToPlayback();

        refreshWorkspaceSnapshot();
    };

    phraseWorkspace->onMasterVolumeChanged = [this](float volume)
    {
        workspaceController.dispatch(engine::SetMasterVolumeCommand { volume });
        for (auto& playbackEngine : deckPlaybackEngines)
            playbackEngine.setMasterVolume(volume);
        refreshWorkspaceSnapshot();
    };

    phraseWorkspace->onBpmChanged = [this](double bpm)
    {
        workspaceController.dispatch(engine::SetBpmCommand { bpm });
        updateTransportTempoFromSnapshot();

        refreshWorkspaceSnapshot();
    };

    phraseWorkspace->onTrackLoadRequested = [this](model::DeckId deckId, int launchOffsetBars)
    {
        showTrackSelectionDialog(deckId, launchOffsetBars);
    };

    refreshWorkspaceSnapshot();
    updateTransportTempoFromSnapshot();

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
    audioSampleRate.store(sampleRate);

    for (auto& playbackEngine : deckPlaybackEngines)
        playbackEngine.prepareToPlay(samplesPerBlockExpected, sampleRate);
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    bufferToFill.clearActiveBufferRegion();

    // One global grid clock drives every deck. Each deck maps this shared musical
    // position through its own beatgrid and launch offset before reading audio.
    const auto blockStartBar = transportGridBarPosition.load();
    const auto transportRunning = std::any_of(deckPlaybackEngines.begin(),
        deckPlaybackEngines.end(),
        [](const engine::DeckPlaybackEngine& playbackEngine) { return playbackEngine.isPlaying(); });

    for (auto& playbackEngine : deckPlaybackEngines)
        playbackEngine.addNextAudioBlockAtGrid(bufferToFill, blockStartBar);

    const auto sampleRate = audioSampleRate.load();
    const auto secondsPerBar = transportSecondsPerBar.load();
    if (transportRunning && sampleRate > 0.0 && secondsPerBar > 0.0)
    {
        const auto renderedBars = (static_cast<double>(bufferToFill.numSamples) / sampleRate) / secondsPerBar;
        transportGridBarPosition.store(blockStartBar + renderedBars);
    }
}

void MainComponent::releaseResources()
{
    audioSampleRate.store(0.0);

    for (auto& playbackEngine : deckPlaybackEngines)
        playbackEngine.releaseResources();
}

void MainComponent::timerCallback()
{
    updateDeckPlayingState();
    workspaceController.dispatch(engine::SetCurrentBarPositionCommand { transportGridBarPosition.load() });

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
    const auto transportWasPlaying = std::any_of(deckPlaybackEngines.begin(),
        deckPlaybackEngines.end(),
        [](const engine::DeckPlaybackEngine& playbackEngine) { return playbackEngine.isPlaying(); });

    const auto slot = deckSlot(result->deckId);
    deckLaunchOffsetBars[slot] = result->launchOffsetBars;
    deckBeatGrids[slot] = result->beatGrid;

    if (result->hasPreparedAudio)
    {
        auto& playbackEngine = playbackEngineFor(result->deckId);
        if (! playbackEngine.loadPreparedStemSet(std::move(result->preparedStems)))
        {
            if (result->requestId == activeTrackLoadRequestId)
                phraseWorkspace->clearPendingTrackLoadMarker();

            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                "Track Load Failed",
                "Prepared audio could not be loaded for " + deckLabel(result->deckId) + ".");
            return;
        }

        playbackEngine.setMasterVolume(workspaceController.createSnapshot().masterVolume);
        playbackEngine.setCurrentGridBarPosition(transportGridBarPosition.load());
    }

    workspaceController.dispatch(engine::SetDeckLaunchOffsetCommand { result->deckId, result->launchOffsetBars });
    workspaceController.dispatch(engine::SetDeckLoadedTrackCommand {
        result->deckId,
        std::move(result->track),
        result->beatGrid,
        std::move(result->stemWaveforms),
        std::move(result->phraseBlocks)
    });
    updateTransportTempoFromSnapshot();
    workspaceController.dispatch(engine::SetCurrentBarPositionCommand { transportGridBarPosition.load() });
    configureDeckGridPlayback(result->deckId);
    syncStemControlsToPlayback();

    if (transportWasPlaying)
        playbackEngineFor(result->deckId).start();

    if (result->requestId == activeTrackLoadRequestId)
        phraseWorkspace->clearPendingTrackLoadMarker();

    refreshWorkspaceSnapshot();
}

void MainComponent::togglePlayback()
{
    const auto anyPlaying = std::any_of(deckPlaybackEngines.begin(),
        deckPlaybackEngines.end(),
        [](const engine::DeckPlaybackEngine& playbackEngine) { return playbackEngine.isPlaying(); });

    if (anyPlaying)
    {
        for (auto& playbackEngine : deckPlaybackEngines)
            playbackEngine.stop();
    }
    else
    {
        setTransportGridBarPosition(transportGridBarPosition.load());

        for (auto& playbackEngine : deckPlaybackEngines)
            playbackEngine.start();
    }

    updateDeckPlayingState();
    refreshWorkspaceSnapshot();
}

void MainComponent::configureDeckGridPlayback(model::DeckId deckId)
{
    const auto slot = deckSlot(deckId);
    const auto& beatGrid = deckBeatGrids[slot];
    if (! beatGrid.has_value())
        return;

    const auto secondsPerBar = beatGrid->secondsPerBeat * static_cast<double>(std::max(1, beatGrid->beatsPerBar));
    auto& playbackEngine = playbackEngineFor(deckId);
    playbackEngine.configureGridPlayback(secondsPerBar, beatGrid->firstBeatOffsetSeconds, deckLaunchOffsetBars[slot]);

    const auto snapshot = workspaceController.createSnapshot();
    playbackEngine.setGlobalBpm(snapshot.bpm, snapshot.beatsPerBar);
    playbackEngine.setCurrentGridBarPosition(transportGridBarPosition.load());
}

void MainComponent::setTransportGridBarPosition(double gridBarPosition)
{
    const auto clampedGridBarPosition = std::max(0.0, gridBarPosition);
    transportGridBarPosition.store(clampedGridBarPosition);

    for (auto& playbackEngine : deckPlaybackEngines)
        playbackEngine.setCurrentGridBarPosition(clampedGridBarPosition);
}

void MainComponent::updateTransportTempoFromSnapshot()
{
    const auto snapshot = workspaceController.createSnapshot();
    const auto secondsPerBar = secondsPerBarFor(snapshot.bpm, snapshot.beatsPerBar);
    if (secondsPerBar > 0.0)
        transportSecondsPerBar.store(secondsPerBar);

    // The shared transport and every deck stretcher must agree on the global
    // bar duration, including when loading a track changes the workspace BPM.
    for (std::size_t slot = 0; slot < deckPlaybackEngines.size(); ++slot)
        deckPlaybackEngines[slot].setGlobalBpm(snapshot.bpm, snapshot.beatsPerBar);
}

void MainComponent::updateDeckPlayingState()
{
    for (const auto deckId : { model::DeckId::A, model::DeckId::B, model::DeckId::C })
        workspaceController.dispatch(engine::SetDeckPlayingCommand { deckId, playbackEngineFor(deckId).isPlaying() });
}

void MainComponent::syncStemControlsToPlayback()
{
    const auto snapshot = workspaceController.createSnapshot();

    for (const auto deckId : { model::DeckId::A, model::DeckId::B, model::DeckId::C })
    {
        const auto* deck = model::findDeck(snapshot, deckId);
        if (deck == nullptr)
            continue;

        auto& playbackEngine = playbackEngineFor(deckId);
        for (const auto stemType : { model::StemType::Drums, model::StemType::Bass, model::StemType::MusicResidual, model::StemType::Vocals })
        {
            playbackEngine.setStemEnabled(stemType, model::isStemEnabled(deck->stemEnabled, stemType));
            playbackEngine.setStemVolume(stemType, model::stemVolume(deck->stemEnabled, stemType));
        }
    }
}

engine::DeckPlaybackEngine& MainComponent::playbackEngineFor(model::DeckId deckId) noexcept
{
    return deckPlaybackEngines[deckSlot(deckId)];
}

const engine::DeckPlaybackEngine& MainComponent::playbackEngineFor(model::DeckId deckId) const noexcept
{
    return deckPlaybackEngines[deckSlot(deckId)];
}

void MainComponent::refreshWorkspaceSnapshot()
{
    phraseWorkspace->setStateSnapshot(workspaceController.createSnapshot());
}
} // namespace mixdesk::app
