from __future__ import annotations

import json
from pathlib import Path

from edm_phrase_detect.cli import detect_track
from edm_phrase_detect.export import save_result

from test_synthetic_simple import make_config, write_synthetic_track


def test_json_schema_stable(tmp_path: Path) -> None:
    track = tmp_path / "schema_track"
    out = tmp_path / "phrases.json"
    write_synthetic_track(track, bars=16, bass_ranges=((9, 16),))
    result, _, _, _, _ = detect_track(track, make_config())
    save_result(result, out)
    data = json.loads(out.read_text(encoding="utf-8"))
    assert set(data) == {"schema_version", "bpm", "duration_s", "beats", "downbeats", "bars", "boundaries", "segments", "metadata"}
    assert data["schema_version"] == "1.0.0"
    assert data["boundaries"][0]["bar_index"] == 1
    assert isinstance(data["bpm"], float)
    assert all(isinstance(value, float) for value in data["beats"][:4])
    assert all(segment["start_bar"] >= 1 for segment in data["segments"])

