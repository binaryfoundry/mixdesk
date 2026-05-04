# Mixdesk Tools

Utilities for preparing local track folders for the Mixdesk prototype.

## `generate_mixdesk.py`

Generates `mixdesk.json` metadata files for folders that contain source-separated stems.

The script scans a root directory recursively, finds audio files, classifies stems from filename tokens, reads metadata with `ffprobe`, analyzes stems offline, and writes one `mixdesk.json` beside each detected stem set. Beat grids, bar grids, boundary candidates, selected phrase boundaries, confidence/reason data, and phrase blocks are stored in the JSON so the app can load musical structure without recalculating low-level features on every track load.

Phrase generation is offline and deterministic:

- Beat/downbeat detection, barwise stem features, boundary scoring, and phrase decoding are handled by the `edm_phrase_detect` package.
- `--backend auto` tries optional stronger backends before the package's librosa backend.
- Stem-aware bar features include drum/kick/bass/vocal/harmonic evidence when those stems are available.
- The tool renders `mixdesk_phrase_analysis.png`, a waveform overview with beats, bars, boundary candidates, selected phrase boundaries, labels, and a boundary-score curve.
- The generated `beat_grid.beat_times_seconds`, `beat_grid.beat_events`, and `beat_grid.bars` are the app's authoritative timing source. The C++ loader does not regenerate a BPM-only beat grid when these fields are missing.
- Phrase blocks are loaded from JSON `start_bar` and `end_bar`, then mapped through the exported `beat_grid.bars`. The legacy `beat_index` and `beat_count` fields are retained only as compatibility/debug fields and are derived from exported downbeat indices.

Recognized stems:

- `drum` / `drums`
- `kick`
- `bass`
- `synth`
- `vocals` / `vocal`
- `other`
- `mix`
- `instrumental` / `instrument` for older instrumental files
- LALALAI-style complements: `no_bass`, `no_drum`, and `no_vocals` / `no-vocals` / `novocals`

For names such as `street-tuff_no_bass_split_by_lalalai.mp3`, composite labels are matched before plain labels. This prevents `no_bass` from being mistaken for the isolated bass stem.

Supported audio extensions include WAV, AIFF, FLAC, MP3, M4A, AAC, OGG, OPUS, WMA, and ALAC.

## Requirements

- Python 3.10 or newer
- Pillow, NumPy, SciPy, soundfile, librosa, matplotlib, pytest, and mir_eval (`python -m pip install -e .`)
- FFmpeg available on `PATH`, specifically `ffmpeg` and `ffprobe`

Check FFmpeg:

```powershell
ffmpeg -version
ffprobe -version
```

## Usage

Preview generated metadata without writing files:

```powershell
python Tools\generate_mixdesk.py D:\tracks --dry-run
```

Generate or overwrite `mixdesk.json` files:

```powershell
python Tools\generate_mixdesk.py D:\tracks
```

Choose a beat/downbeat backend and analysis sample rate:

```powershell
python Tools\generate_mixdesk.py D:\tracks --backend auto --target-sr 44100
python Tools\generate_mixdesk.py D:\tracks --backend librosa
```

Run from a track folder:

```powershell
python C:\Users\paula\Documents\Projects\mixdesk\Tools\generate_mixdesk.py .
```

## Notes

The app currently uses `mixdesk.json` to locate the original track file plus direct drum/bass/vocal stems, LALALAI complement stems, a precomputed `beat_grid`, and bar-indexed `phrases`. Beats, bars, downbeat phase, and phrase spans are loaded from metadata; they are not inferred again in C++. The C++ app decodes stems to non-normalized floating-point PCM, derives complement candidates, and exposes only Drums, Bass, Vocals, and Music for playback.
