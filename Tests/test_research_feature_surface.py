from __future__ import annotations

from pathlib import Path

from edm_phrase_detect.bar_features import extract_barwise_features
from edm_phrase_detect.beatgrid import build_bars, detect_beats_downbeats
from edm_phrase_detect.io import load_stems

from test_synthetic_simple import make_config, write_synthetic_track


def test_research_stem_features_are_exposed(tmp_path: Path) -> None:
    track = tmp_path / "research_features"
    write_synthetic_track(
        track,
        bars=24,
        bass_ranges=((1, 24),),
        synth_ranges=((9, 24),),
        vocal_ranges=((17, 24),),
    )
    config = make_config()
    stems, sr, _ = load_stems(track, config.target_sr)
    beat_events, _, _, _ = detect_beats_downbeats(stems, sr, config)
    bars, _ = build_bars(beat_events, len(next(iter(stems.values()))) / sr, config)
    features = extract_barwise_features(stems, sr, bars, beat_events, config)
    arrays = features["feature_arrays"]

    assert "harmonic_mfcc_delta_change" in arrays
    assert "mfcc_delta_bar" in arrays
    assert "vocal_syllabic_onset_density" in arrays
    assert "vocal_syllabic_onset_change" in arrays
    assert "vocal_first_entry_flag" in arrays
    assert float(arrays["vocal_first_entry_flag"][16]) == 1.0
