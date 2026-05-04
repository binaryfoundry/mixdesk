from __future__ import annotations

from pathlib import Path

from edm_phrase_detect.cli import detect_track

from test_synthetic_simple import make_config, write_synthetic_track


def _candidate_for_bar(debug: dict, bar_index: int) -> float:
    return float(debug["candidate_boundary_scores"][bar_index - 1])


def test_vocal_entry_raises_boundary_confidence(tmp_path: Path) -> None:
    no_vocal = tmp_path / "no_vocal"
    with_vocal = tmp_path / "with_vocal"
    write_synthetic_track(no_vocal, bars=24, bass_ranges=((1, 24),), vocal_ranges=())
    write_synthetic_track(with_vocal, bars=24, bass_ranges=((1, 24),), vocal_ranges=((17, 24),))
    _, debug_without, _, _, _ = detect_track(no_vocal, make_config())
    _, debug_with, _, _, _ = detect_track(with_vocal, make_config())
    assert debug_with["features"]["feature_arrays"]["vocal_entry"][16] > 0.4
    assert _candidate_for_bar(debug_with, 17) > _candidate_for_bar(debug_without, 17)

