from __future__ import annotations

from pathlib import Path

import numpy as np

from edm_phrase_detect.bar_features import extract_barwise_features
from edm_phrase_detect.beatgrid import build_bars, detect_beats_downbeats
from edm_phrase_detect.cli import detect_track
from edm_phrase_detect.io import load_stems

from test_synthetic_simple import make_config, write_synthetic_track


def test_sidechain_does_not_create_bar_boundaries(tmp_path: Path) -> None:
    track = tmp_path / "sidechain"
    write_synthetic_track(track, bars=24, bass_ranges=((1, 24),), sidechain_bass=True)
    result, debug, _, _, _ = detect_track(track, make_config())
    internal_boundaries = [boundary for boundary in result.boundaries if boundary.bar_index not in {1}]
    assert len(internal_boundaries) <= 3

    stems, sr, _ = load_stems(track, make_config().target_sr)
    beat_events, _, _, _ = detect_beats_downbeats(stems, sr, make_config())
    bars, _ = build_bars(beat_events, len(next(iter(stems.values()))) / sr, make_config())
    features = extract_barwise_features(stems, sr, bars, beat_events, make_config())
    arrays = features["feature_arrays"]
    assert float(np.mean(arrays["sidechain_pumping_score"])) > 0.05
    assert float(np.mean(arrays["bass_change_corrected"])) <= float(np.mean(arrays["bass_change"])) + 1e-6

