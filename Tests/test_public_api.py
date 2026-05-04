from __future__ import annotations

from pathlib import Path

import edm_phrase_detect as epd

from test_synthetic_simple import write_synthetic_track


def test_public_api_round_trip(tmp_path: Path) -> None:
    track = tmp_path / "api_track"
    write_synthetic_track(track, bars=16, bass_ranges=((9, 16),))

    stems, sr = epd.load_stems(track, target_sr=22_050)
    bpm, beat_events = epd.detect_beats_downbeats(stems, sr, backend="librosa")
    bars = epd.build_bar_grid(beat_events)
    features = epd.extract_barwise_features(stems, sr, bars, beat_events)
    scores = epd.score_phrase_boundaries(features)
    boundaries = epd.decode_phrase_boundaries(bars, scores)
    segments = epd.build_segments(boundaries, bars)

    assert bpm > 0.0
    assert len(bars) >= 4
    assert "drums_fill_prev_bar" in features
    assert len(scores) == len(bars)
    assert boundaries[0].bar_index == 1
    assert segments


def test_public_dataclasses_match_prompt_surface() -> None:
    segment = epd.PhraseSegment(start_s=0.0, end_s=4.0, start_bar=1, end_bar=2, length_bars=2, confidence=0.8)
    result = epd.PhraseResult(bpm=128.0, beats=[], downbeats=[], bars=[], boundaries=[], segments=[segment])

    assert segment.label == "unknown"
    assert result.schema_version == "1.0.0"
    assert result.duration_s == 0.0
