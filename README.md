# MixDesk Prototype

This is a small JUCE/C++20 prototype for a touch-first 3-deck DJ phrase alignment workspace. It intentionally does not imitate CDJs, turntables, or a mixer surface. The main object is musical time: three horizontal deck lanes show phrase blocks against a shared bar grid.

## EDM Phrase Detector Package

This repo also contains `edm_phrase_detect`, an offline Python package for DJ-useful phrase detection on stem folders.

Run it with:

```powershell
python -m edm_phrase_detect.cli --track-dir .\track --out .\phrases.json
```

Optional debug output:

```powershell
python -m edm_phrase_detect.cli `
  --track-dir .\track `
  --out .\phrases.json `
  --plot .\phrases.png `
  --debug-json .\debug.json `
  --backend auto `
  --target-sr 44100 `
  --min-phrase-bars 4 `
  --allowed-phrase-bars 4,8,16,32 `
  --preferred-phrase-bars 8,16
```

Expected stem filenames are `kick.wav`, `drums.wav`, `bass.wav`, `synth.wav`, `vocals.wav`, `other.wav`, and `mix.wav`. Missing stems are allowed; available feature groups are reweighted during boundary scoring.

## Generating Track Metadata

MixDesk loads prepared track metadata from `mixdesk.json` files. Generate those files offline with Python before opening tracks in the app:

```powershell
python -m pip install -e .
python Tools\generate_mixdesk.py D:\tracks --backend auto --target-sr 44100
```

`Tools\generate_mixdesk.py` scans the root folder recursively, classifies audio files by filename tokens, reads tags with `ffprobe`, analyzes stems with `edm_phrase_detect`, and writes or overwrites one `mixdesk.json` beside each detected stem set. It also writes `mixdesk_phrase_analysis.png` next to each generated JSON when a waveform preview can be rendered.

Useful variants:

```powershell
python Tools\generate_mixdesk.py D:\tracks --dry-run
python Tools\generate_mixdesk.py D:\tracks --backend librosa --target-sr 22050
python C:\Users\paula\Documents\Projects\mixdesk\Tools\generate_mixdesk.py .
```

FFmpeg must be on `PATH` because the generator uses `ffmpeg` for decoding and `ffprobe` for metadata. The generated JSON contains the app-facing `beat_grid`, phrase blocks, phrase-analysis candidates, selected boundaries, labels, confidence scores, reasons, available/missing stems, and backend/debug metadata. Recognized stems include `drum`/`drums`, `kick`, `bass`, `synth`, `vocals`, `other`, `mix`, `instrumental`, and LALALAI-style complements such as `no_bass`, `no_drum`, and `no_vocals`.

## What It Demonstrates

- A custom `PhraseWorkspace` JUCE component with a horizontal phrase grid, deck lanes, phrase blocks, role labels, playhead, status row, and large touch-friendly controls.
- Starts with empty deck lanes; double-tap a lane to choose a track and place it at that bar.
- Bar/phrase snapping structure for dragging phrase blocks horizontally.
- Track selection scans the configured tracks root for `mixdesk.json` files, defaulting to `D:\tracks`.
- Beat grids are read from each track's `mixdesk.json`, then beat markers are drawn on the loaded deck lane.
- Phrase blocks are read from each track's `mixdesk.json` as beat-indexed sections generated offline by the Python tool.
- Drum, bass, music, and vocal stems are decoded once at load time into compact peak waveforms and rendered in layered colours.
- Stem preprocessing reconciles direct LALALAI stems with `no_bass`, `no_drum`, and `no_vocals` complements so playback exposes clean Drums, Bass, Music, and Vocals.
- The top bar provides one global play/pause control, a master volume slider, and a global BPM slider.
- BPM changes drive the shared grid playhead while Signalsmith Stretch keeps playback pitch locked.
- Per-deck D/B/M/V stem controls toggle drum, bass, music, and vocal layers and provide vertical per-stem volume control. Raising one deck's stem ducks the same stem on the other loaded decks so that stem's total stays at one.
- Early conflict indicators for overlapping bass-heavy or vocal-heavy blocks.
- A small controller boundary where UI commands are dispatched instead of touching any future audio engine directly.

This is still not a full DJ audio engine. Playback is a minimal three-deck JUCE `AudioSource` path used to prove grid-positioned track playback, stem mixing, and pitch-locked tempo changes.

## Build

The project uses CMake and pulls JUCE plus the header-only Signalsmith Stretch library from source by default. No JUCE or Signalsmith DLL is required.

```powershell
cmake -S . -B build
cmake --build build --config Release
```

To use an existing local JUCE checkout instead of fetching it:

```powershell
cmake -S . -B build -DJUCE_DIR=C:\path\to\JUCE
cmake --build build --config Release
```

On Windows/MSVC, `MIXDESK_STATIC_RUNTIME` is enabled by default to avoid depending on the MSVC runtime DLLs.

## Code Layout

- `Source/Model`: deck, role, phrase, timeline, workspace state, and demo state creation.
- `Source/UI/PhraseWorkspace.*`: custom JUCE component, rendering, hit testing, drag snapping, zoom scaffolding, and conflict display.
- `Source/Engine/TrackLoader.*`: reads `mixdesk.json`, finds the primary track and stems, and loads precomputed beat grids and phrase blocks.
- `Source/Engine/StemPreparation.*`: offline stem decoding, alignment, complement-stem reconciliation, `MusicResidual` derivation, reconstruction validation metrics, and public stem mix helpers.
- `Source/Engine/WaveformAnalyzer.*`: builds downsampled stem peak envelopes for timeline rendering.
- `Source/Engine/DeckPlaybackEngine.*`: reusable per-deck playback source, including prepared-stem playback, grid-positioned transport, master volume, and Signalsmith Stretch pitch-locked BPM changes.
- `Source/Engine/WorkspaceController.*`: temporary UI-thread command receiver and state snapshot source. This is where a command queue/state snapshot handoff should replace direct mutation later.
- `Source/App`: JUCE application shell and main component wiring.

## Track Loading

Mixdesk no longer starts with a track loaded. Double-tap the musical timeline area of a deck lane to open the track selector. The selected track is loaded at the tapped bar position.

The track root is stored in:

```text
%APPDATA%\Mixdesk\mixdesk.ini
```

Default contents:

```ini
tracksRoot=D:\tracks
```

Track loading, stem preparation, and waveform analysis run on a background thread. Beat detection, downbeat/bar grids, and stem-aware phrase detection are generated offline by `Tools/generate_mixdesk.py` through the `edm_phrase_detect` package when preparing `mixdesk.json`. The UI thread receives only the prepared result, so opening a track picker or loading a new deck should not block the interface or do heavy work in the audio callback.

## Stem Reconciliation Design

LALALAI complement files such as `no_bass`, `no_drum`, and `no_vocals` are full-mix-minus-one-part sources. They are useful analysis material, but they are not DJ-facing stems and must not be played alongside Drums and Bass as a normal performance set.

Mixdesk now prepares four public stems:

```text
Drums
Bass
Vocals
Music
```

Direct isolated stems are reconciled with complement-derived candidates:

```text
BassFromComplement   = FullMix - NoBass
DrumsFromComplement  = FullMix - NoDrums
VocalsFromComplement = FullMix - NoVocals
```

The first version chooses between direct and complement-derived bass/drum candidates using reconstruction error, while vocals use `VocalsFromComplement` when available. The internal Music stem is then:

```text
MusicResidual = FullMix - ReconciledDrums - ReconciledBass - ReconciledVocals
```

Fallback when a real FullMix is unavailable remains:

```text
MusicResidual = InstrumentalOriginal/NoVocals - Drums - Bass
```

Stem math is an import/preprocessing/background-cache job, not an audio callback job. MP3 is decoded once to floating-point PCM before subtraction because MP3 files can include encoder delay, padding, and lossy artifacts. Sources must share sample rate and channel count, and the preprocessing pass performs a conservative cross-correlation alignment check before subtraction. Display waveform normalization is separate and does not touch the raw sample data used for stem math.

Playback uses FullMix when all four public stems are enabled and a real FullMix is available, because that is the highest-quality path. As soon as any individual stem changes, playback reconstructs from:

```text
enabled(Drums) + enabled(Bass) + enabled(MusicResidual) + enabled(Vocals)
```

`NoBass`, `NoDrums`, `NoVocals`, direct candidates, and complement-derived candidates remain source/debug material. Normal playback never mixes `Bass + Drums + NoVocals`, never mixes `Bass + Drums + NoBass`, and never plays direct and complement versions of the same stem together. The debug report logs source availability, chosen candidates, signal metrics, reconstruction error, and warnings for suspiciously high error.

## Tests

```powershell
ctest --test-dir build -C Release --output-on-failure
```

`MixdeskStemTests` covers complement candidate derivation, alignment offset detection, mismatched source rejection, no-complement-stem playback rules, and FullMix preference when all public stems are enabled.

## Next Steps

- TODO(real audio engine): introduce an engine/control thread boundary and a lock-free command queue.
- TODO(waveform rendering): add waveform lanes behind phrase blocks.
- TODO(metadata cache): persist prepared stem and waveform artifacts beside analyzed track metadata.
- TODO(MIDI/HID controller support): map external controls to launch, role, and snap actions.
- TODO(multi-touch pinch zoom): complete two-finger zoom/pan handling against target hardware.
- TODO(GPU rendering path): move dense timeline rendering to an accelerated path when visual density increases.
- TODO(stem quality): add confidence-weighted candidate blending, frequency-dependent reconciliation, transient-aware drum selection, bass low-frequency preservation, vocal artifact detection, and spectral quality metrics.
