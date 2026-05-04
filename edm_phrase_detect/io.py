from __future__ import annotations

from pathlib import Path

import librosa
import numpy as np
import soundfile as sf

from .models import StemInfo


KNOWN_STEMS = ("kick", "drums", "bass", "synth", "vocals", "other", "mix")
SUPPORTED_EXTENSIONS = (".wav", ".aif", ".aiff", ".flac", ".ogg", ".mp3", ".m4a")


def _find_stem_file(track_dir: Path, stem_name: str) -> Path | None:
    for extension in SUPPORTED_EXTENSIONS:
        candidate = track_dir / f"{stem_name}{extension}"
        if candidate.exists():
            return candidate
    return None


def _read_audio(path: Path) -> tuple[np.ndarray, int, int, int]:
    data, sr = sf.read(path, always_2d=True, dtype="float32")
    channels = int(data.shape[1])
    original_num_samples = int(data.shape[0])
    mono = np.mean(data, axis=1, dtype=np.float64).astype(np.float32)
    return mono, int(sr), channels, original_num_samples


def load_stems(track_dir: str | Path, target_sr: int) -> tuple[dict[str, np.ndarray], int, dict[str, StemInfo]]:
    """Load known stem files from a track folder.

    Audio is converted to mono and resampled for analysis. No loudness normalization is applied here; feature
    extraction can normalize temporary copies while preserving relative stem energy in the loaded arrays.
    """

    root = Path(track_dir)
    if not root.exists() or not root.is_dir():
        raise FileNotFoundError(f"Track directory does not exist: {root}")

    stems: dict[str, np.ndarray] = {}
    stem_info: dict[str, StemInfo] = {}

    for stem_name in KNOWN_STEMS:
        path = _find_stem_file(root, stem_name)
        if path is None:
            continue

        audio, original_sr, channels, original_num_samples = _read_audio(path)
        if original_sr != target_sr:
            audio = librosa.resample(audio, orig_sr=original_sr, target_sr=target_sr).astype(np.float32)
        else:
            audio = audio.astype(np.float32, copy=False)

        stems[stem_name] = audio
        stem_info[stem_name] = StemInfo(
            name=stem_name,
            path=str(path),
            original_sample_rate=original_sr,
            target_sample_rate=target_sr,
            original_num_samples=original_num_samples,
            resampled_num_samples=int(len(audio)),
            duration_s=float(len(audio)) / float(target_sr),
            channels=channels,
        )

    if not stems:
        raise ValueError(f"No known stem files found in {root}")

    return stems, target_sr, stem_info
