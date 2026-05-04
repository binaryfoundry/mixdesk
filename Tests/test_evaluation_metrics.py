from __future__ import annotations

from edm_phrase_detect.evaluation import evaluate_beats, evaluate_downbeats, evaluate_phrase_boundaries_beats, evaluate_phrase_boundaries_seconds


def test_beat_evaluation_reports_research_metrics() -> None:
    beats = [0.5 + index * 0.5 for index in range(16)]
    metrics = evaluate_beats(beats, beats)

    assert metrics["f_measure"] == 1.0
    assert metrics["cemgil"] == 1.0
    assert metrics["cmlt"] == 1.0
    assert metrics["amlt"] == 1.0


def test_downbeat_evaluation_uses_beat_style_continuity_metrics() -> None:
    downbeats = [0.5 + index * 2.0 for index in range(8)]
    metrics = evaluate_downbeats(downbeats, downbeats)

    assert metrics["f_measure"] == 1.0
    assert "cemgil" in metrics
    assert "cmlt" in metrics


def test_phrase_boundary_evaluation_reports_segment_metrics() -> None:
    reference = [0.0, 8.0, 16.0, 24.0]
    exact = evaluate_phrase_boundaries_seconds(reference, reference, duration_s=24.0, tolerance_s=0.5)
    late = evaluate_phrase_boundaries_seconds([0.0, 8.7, 16.0, 24.0], reference, duration_s=24.0, tolerance_s=0.5)
    beat_window = evaluate_phrase_boundaries_beats(reference, reference, beat_duration_s=0.5, duration_s=24.0)

    assert exact["f_measure"] == 1.0
    assert late["f_measure"] < 1.0
    assert beat_window["tolerance_s"] == 0.5
