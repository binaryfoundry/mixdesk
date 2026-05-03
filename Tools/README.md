# Mixdesk Tools

Utilities for preparing local track folders for the Mixdesk prototype.

## `generate_mixdesk.py`

Generates `mixdesk.json` metadata files for folders that contain source-separated stems.

The script scans a root directory recursively, finds audio files, classifies stems from filename tokens, reads metadata with `ffprobe`, and writes one `mixdesk.json` beside each detected stem set.

Recognized stems:

- `drum` / `drums`
- `bass`
- `vocals` / `vocal`
- `instrumental` / `instrument` for legacy instrumental files
- LALALAI-style complements: `no_bass`, `no_drum`, and `no_vocals` / `no-vocals` / `novocals`

For names such as `street-tuff_no_bass_split_by_lalalai.mp3`, composite labels are matched before plain labels. This prevents `no_bass` from being mistaken for the isolated bass stem.

Supported audio extensions include WAV, AIFF, FLAC, MP3, M4A, AAC, OGG, OPUS, WMA, and ALAC.

## Requirements

- Python 3.10 or newer
- FFmpeg available on `PATH`, specifically `ffprobe`

Check `ffprobe`:

```powershell
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

Run from a track folder:

```powershell
python C:\Users\paula\Documents\Projects\mixdesk\Tools\generate_mixdesk.py .
```

## Notes

The app currently uses `mixdesk.json` to locate the original track file plus direct drum/bass/vocal stems and the LALALAI complement stems. The C++ app decodes them to non-normalized floating-point PCM, derives complement candidates, and exposes only Drums, Bass, Vocals, and Music for playback.
