from __future__ import annotations

from pathlib import Path

from edm_phrase_detect.bar_features import extract_barwise_features
from edm_phrase_detect.beatgrid import build_bars, detect_beats_downbeats
from edm_phrase_detect.io import load_stems
from edm_phrase_detect.scoring import score_phrase_boundaries

from test_synthetic_simple import make_config, write_synthetic_track


def test_drum_fill_boosts_next_boundary(tmp_path: Path) -> None:
    track = tmp_path / "fill"
    write_synthetic_track(track, bars=16, bass_ranges=((9, 16),), drum_fill_before=(9,))
    config = make_config()
    stems, sr, _ = load_stems(track, config.target_sr)
    beat_events, _, _, _ = detect_beats_downbeats(stems, sr, config)
    bars, _ = build_bars(beat_events, len(next(iter(stems.values()))) / sr, config)
    features = extract_barwise_features(stems, sr, bars, beat_events, config)
    candidate_scores, reasons, _ = score_phrase_boundaries(features, bars, config)
    boundary_index = 8
    assert features["feature_arrays"]["drums_fill_prev_bar"][boundary_index] > 0.35
    assert reasons[boundary_index]["drums"] > 0.20
    assert candidate_scores[boundary_index] > candidate_scores[4]

