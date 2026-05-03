# Mixdesk Tools

Utilities for preparing local track folders for the Mixdesk prototype.

## `generate_mixdesk.py`

Generates `mixdesk.json` metadata files for folders that contain source-separated stems.

The script scans a root directory recursively, finds audio files, classifies stems from filename tokens, reads metadata with `ffprobe`, and writes one `mixdesk.json` beside each detected stem set.

Recognized stems:

- `drum` / `drums`
- `bass`
- `vocals` / `vocal`
- `instrumental` / `instrument` / `no_vocals` / `no-vocals` / `novocals`

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

The app currently uses `mixdesk.json` to locate the original track file plus the drum, bass, vocal, and instrumental stems. The C++ app then derives the playable `Music` stem from raw, non-normalized buffers during import/preprocessing.
