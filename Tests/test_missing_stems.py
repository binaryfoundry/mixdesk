from __future__ import annotations

from pathlib import Path

from edm_phrase_detect.cli import detect_track

from test_synthetic_simple import make_config, write_synthetic_track


def test_missing_stems_still_runs(tmp_path: Path) -> None:
    track = tmp_path / "missing"
    write_synthetic_track(track, bars=16, bass_ranges=((9, 16),), stems=("drums", "bass"))
    result, debug, _, _, _ = detect_track(track, make_config())
    assert result.boundaries
    assert "kick" in result.metadata["missing_stems"]
    assert set(debug["features"]["available_groups"]) >= {"drums", "bass"}

