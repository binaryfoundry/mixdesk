from __future__ import annotations

from pathlib import Path

from edm_phrase_detect.bar_features import extract_barwise_features
from edm_phrase_detect.beatgrid import build_bars, detect_beats_downbeats
from edm_phrase_detect.io import load_stems

from test_synthetic_simple import make_config, write_synthetic_track


def test_kick_dropout_and_return(tmp_path: Path) -> None:
    track = tmp_path / "kick_drop"
    write_synthetic_track(track, bars=24, bass_ranges=((1, 8), (17, 24)), kick_ranges=((1, 8), (17, 24)))
    config = make_config()
    stems, sr, _ = load_stems(track, config.target_sr)
    beat_events, _, _, _ = detect_beats_downbeats(stems, sr, config)
    bars, _ = build_bars(beat_events, len(next(iter(stems.values()))) / sr, config)
    features = extract_barwise_features(stems, sr, bars, beat_events, config)
    arrays = features["feature_arrays"]
    assert arrays["kick_dropout"][8] > 0.25
    assert arrays["kick_return"][16] > 0.25

