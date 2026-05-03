# Mixdesk Phrase Workspace Prototype

This is a small JUCE/C++20 prototype for a touch-first 3-deck DJ phrase alignment workspace. It intentionally does not imitate CDJs, turntables, or a mixer surface. The main object is musical time: three horizontal deck lanes show phrase blocks against a shared bar grid.

## What It Demonstrates

- A custom `PhraseWorkspace` JUCE component with a horizontal phrase grid, deck lanes, phrase blocks, role labels, playhead, status row, and large touch-friendly controls.
- A clean Deck A loading view with Deck B and Deck C left empty for manual arrangement work.
- Bar/phrase snapping structure for dragging phrase blocks horizontally.
- Deck A auto-loads the first `mixdesk.json` found under `D:\tracks`, currently `D:\tracks\street-tuff\mixdesk.json`.
- The drum stem is low-pass filtered and analyzed for a beat grid, then beat markers are drawn on the Deck A lane.
- Drum, bass, and vocal stems are decoded once at load time into compact peak waveforms and rendered in layered colours.
- Stem playback combines a residual instrumental bed with drum, bass, and vocal stems at render time.
- The top bar provides one global play/pause control, a master volume slider, and a global BPM slider.
- BPM changes drive the shared grid playhead while Signalsmith Stretch keeps playback pitch locked.
- Per-deck D/B/V stem buttons toggle drum, bass, and vocal layers using the same colours as the waveform layers.
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
- `Source/Engine/WaveformAnalyzer.*`: builds downsampled stem peak envelopes for timeline rendering.
- `Source/Engine/DeckPlaybackEngine.*`: minimal single-deck playback source for Deck A, including residual stem mixing, global grid transport, master volume, and Signalsmith Stretch pitch-locked BPM changes.
- `Source/Engine/WorkspaceController.*`: temporary UI-thread command receiver and state snapshot source. This is where a command queue/state snapshot handoff should replace direct mutation later.
- `Source/App`: JUCE application shell and main component wiring.

## Next Steps

- TODO(real audio engine): introduce an engine/control thread boundary and a lock-free command queue.
- TODO(waveform rendering): add waveform lanes behind phrase blocks.
- TODO(beatgrid/phrase analysis): load analyzed tracks and generate phrase blocks from metadata.
- TODO(MIDI/HID controller support): map external controls to launch, role, and snap actions.
- TODO(multi-touch pinch zoom): complete two-finger zoom/pan handling against target hardware.
- TODO(GPU rendering path): move dense timeline rendering to an accelerated path when visual density increases.
