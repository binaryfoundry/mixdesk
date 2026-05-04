from __future__ import annotations

import numpy as np
import pytest

from edm_phrase_detect.alignment import validate_and_align_stems
from edm_phrase_detect.config import Config


def _impulse_train(sr: int, seconds: float, interval_s: float = 0.5) -> np.ndarray:
    y = np.zeros(int(sr * seconds), dtype=np.float32)
    pulse = np.hanning(int(0.025 * sr)).astype(np.float32)
    for time_s in np.arange(0.25, seconds - 0.1, interval_s):
        start = int(round(time_s * sr))
        y[start : start + len(pulse)] += pulse[: max(0, min(len(pulse), len(y) - start))]
    return y


def _delay(y: np.ndarray, samples: int) -> np.ndarray:
    out = np.zeros_like(y)
    out[samples:] = y[:-samples]
    return out


def test_alignment_corrects_small_lag() -> None:
    sr = 22_050
    config = Config(target_sr=sr, hop_length=128, frame_length=512, max_alignment_shift_ms=30.0)
    drums = _impulse_train(sr, 8.0)
    bass = _delay(drums, int(round(0.015 * sr)))
    aligned, _, debug = validate_and_align_stems({"drums": drums, "bass": bass}, sr, config)
    assert abs(debug["alignment_shifts_ms"]["bass"]) <= 30.0
    assert np.argmax(aligned["bass"]) == pytest.approx(np.argmax(drums), abs=config.hop_length)


def test_alignment_rejects_large_lag() -> None:
    sr = 22_050
    config = Config(target_sr=sr, hop_length=128, frame_length=512, max_alignment_shift_ms=30.0)
    drums = _impulse_train(sr, 8.0)
    bass = _delay(drums, int(round(0.080 * sr)))
    with pytest.raises(ValueError):
        validate_and_align_stems({"drums": drums, "bass": bass}, sr, config)

