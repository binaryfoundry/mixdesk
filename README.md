# Mixdesk Phrase Workspace Prototype

This is a small JUCE/C++20 prototype for a touch-first 3-deck DJ phrase alignment workspace. It intentionally does not imitate CDJs, turntables, or a mixer surface. The main object is musical time: three horizontal deck lanes show phrase blocks against a shared bar grid.

## What It Demonstrates

- A custom `PhraseWorkspace` JUCE component with a horizontal phrase grid, deck lanes, phrase blocks, role labels, playhead, status row, and large touch-friendly controls.
- A clean Deck A loading view with Deck B and Deck C left empty for manual arrangement work.
- Bar/phrase snapping structure for dragging phrase blocks horizontally.
- Deck A auto-loads the first `mixdesk.json` found under `D:\tracks`, currently `D:\tracks\street-tuff\mixdesk.json`.
- The drum stem is low-pass filtered and analyzed for a beat grid, then beat markers are drawn on the Deck A lane.
- Drum, bass, music, and vocal stems are decoded once at load time into compact peak waveforms and rendered in layered colours.
- Stem preprocessing derives a first-class `MusicResidual` buffer so playback exposes Drums, Bass, Music, and Vocals without double-counting the supplied instrumental stem.
- The top bar provides one global play/pause control, a master volume slider, and a global BPM slider.
- BPM changes drive the shared grid playhead while Signalsmith Stretch keeps playback pitch locked.
- Per-deck D/B/M/V stem buttons toggle drum, bass, music, and vocal layers using the same colours as the waveform layers.
- Early conflict indicators for overlapping bass-heavy or vocal-heavy blocks.
- A small controller boundary where UI commands are dispatched instead of touching any future audio engine directly.

This is still not a full DJ audio engine. Playback is a minimal single-deck JUCE `AudioSource` used only to prove Deck A loading, stem mixing, and pitch-locked tempo changes.

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
- `Source/Engine/TrackLoader.*`: reads `mixdesk.json`, finds the primary track and drum stem.
- `Source/Engine/BeatDetector.*`: C++ translation of the `web-audio-beat-detector` worker algorithm: 240 Hz low-pass render, threshold peak detection, nearby peak interval counting, and tempo bucket scoring.
- `Source/Engine/StemPreparation.*`: offline stem preparation, `MusicResidual` derivation, reconstruction validation metrics, and public stem mix helpers.
- `Source/Engine/WaveformAnalyzer.*`: builds downsampled stem peak envelopes for timeline rendering.
- `Source/Engine/DeckPlaybackEngine.*`: minimal single-deck playback source for Deck A, including prepared-stem playback, global grid transport, master volume, and Signalsmith Stretch pitch-locked BPM changes.
- `Source/Engine/WorkspaceController.*`: temporary UI-thread command receiver and state snapshot source. This is where a command queue/state snapshot handoff should replace direct mutation later.
- `Source/App`: JUCE application shell and main component wiring.

## Stem Reconstruction Design

The supplied `instrumental` stem already contains drums and bass, so it must not be exposed as a normal playable layer alongside Drums and Bass. Mixdesk prepares a DJ-facing layout of Drums, Bass, Music, and Vocals, where the internal `MusicResidual` buffer backs the user-facing Music stem.

Preferred derivation:

```text
MusicResidual = FullMix - Drums - Bass - Vocals
```

Fallback when a real FullMix is unavailable:

```text
MusicResidual = InstrumentalOriginal - Drums - Bass
```

Residual generation is an import/preprocessing/background-cache job, not an audio callback job. It uses raw, non-normalized floating-point buffers and requires sample-aligned sources with matching sample rate, channel count, and sample count. Display waveform normalization is separate and does not touch the raw sample data used for subtraction.

Playback uses FullMix when all four public stems are enabled and a real FullMix is available, because that is the highest-quality path. As soon as any individual stem changes, playback reconstructs from:

```text
enabled(Drums) + enabled(Bass) + enabled(MusicResidual) + enabled(Vocals)
```

`InstrumentalOriginal` remains stored for derivation/debugging but is not mixed with Drums or Bass in the normal playback path.

## Tests

```powershell
ctest --test-dir build -C Release --output-on-failure
```

`MixdeskStemTests` covers full-mix derivation, instrumental fallback derivation, the no-double-count playback rule, and clear failures for mismatched source formats.

## Next Steps

- TODO(real audio engine): introduce an engine/control thread boundary and a lock-free command queue.
- TODO(waveform rendering): add waveform lanes behind phrase blocks.
- TODO(beatgrid/phrase analysis): load analyzed tracks and generate phrase blocks from metadata.
- TODO(MIDI/HID controller support): map external controls to launch, role, and snap actions.
- TODO(multi-touch pinch zoom): complete two-finger zoom/pan handling against target hardware.
- TODO(GPU rendering path): move dense timeline rendering to an accelerated path when visual density increases.
