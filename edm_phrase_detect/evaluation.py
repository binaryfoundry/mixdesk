from __future__ import annotations

import importlib.util

import numpy as np


def _intervals_from_boundaries(boundary_times: list[float], duration_s: float) -> np.ndarray:
    duration = max(0.0, float(duration_s))
    points = [0.0]
    points.extend(float(time_s) for time_s in boundary_times if 0.0 < float(time_s) < duration)
    points.append(duration)
    points = sorted(set(round(point, 9) for point in points))
    if len(points) < 2:
        points = [0.0, max(duration, 1e-6)]
    intervals = np.asarray([[points[index], points[index + 1]] for index in range(len(points) - 1)], dtype=np.float64)
    intervals[:, 1] = np.maximum(intervals[:, 1], intervals[:, 0] + 1e-9)
    return intervals


def _trim_track_edges(boundary_times: list[float], duration_s: float) -> list[float]:
    duration = float(duration_s)
    return [float(time_s) for time_s in boundary_times if 1e-8 < float(time_s) < duration - 1e-8]


def evaluate_beats(predicted_beats: list[float], reference_beats: list[float]) -> dict[str, float]:
    if importlib.util.find_spec("mir_eval") is not None:
        import mir_eval.beat

        reference = np.asarray(reference_beats, dtype=np.float64)
        predicted = np.asarray(predicted_beats, dtype=np.float64)
        cmlc, cmlt, amlc, amlt = mir_eval.beat.continuity(reference, predicted)
        cemgil, cemgil_best_metric_level = mir_eval.beat.cemgil(reference, predicted)
        return {
            "f_measure": float(mir_eval.beat.f_measure(reference, predicted)),
            "cemgil": float(cemgil),
            "cemgil_best_metric_level": float(cemgil_best_metric_level),
            "cmlc": float(cmlc),
            "cmlt": float(cmlt),
            "amlc": float(amlc),
            "amlt": float(amlt),
        }
    return evaluate_boundaries_seconds(predicted_beats, reference_beats, tolerance_s=0.07)


def evaluate_downbeats(predicted_downbeats: list[float], reference_downbeats: list[float]) -> dict[str, float]:
    return evaluate_beats(predicted_downbeats, reference_downbeats)


def evaluate_boundaries_seconds(
    predicted_boundary_times: list[float],
    reference_boundary_times: list[float],
    tolerance_s: float,
) -> dict[str, float]:
    predicted = list(predicted_boundary_times)
    reference = list(reference_boundary_times)
    matched: set[int] = set()
    true_positive = 0
    for pred in predicted:
        best_index = None
        best_distance = tolerance_s
        for index, ref in enumerate(reference):
            if index in matched:
                continue
            distance = abs(float(pred) - float(ref))
            if distance <= best_distance:
                best_distance = distance
                best_index = index
        if best_index is not None:
            matched.add(best_index)
            true_positive += 1
    precision = true_positive / max(len(predicted), 1)
    recall = true_positive / max(len(reference), 1)
    f_measure = 2.0 * precision * recall / max(precision + recall, 1e-8)
    return {"precision": precision, "recall": recall, "f_measure": f_measure}


def evaluate_boundaries_beats(
    predicted_boundary_times: list[float],
    reference_boundary_times: list[float],
    beat_duration_s: float,
    tolerance_beats: float = 1,
) -> dict[str, float]:
    return evaluate_boundaries_seconds(
        predicted_boundary_times,
        reference_boundary_times,
        tolerance_s=float(beat_duration_s) * float(tolerance_beats),
    )


def evaluate_phrase_boundaries_seconds(
    predicted_boundary_times: list[float],
    reference_boundary_times: list[float],
    duration_s: float,
    tolerance_s: float = 0.5,
    trim_edges: bool = True,
) -> dict[str, float]:
    """Evaluate phrase-boundary hit rate with the segment metric used in MIR work."""

    if importlib.util.find_spec("mir_eval") is not None:
        import mir_eval.segment

        reference_intervals = _intervals_from_boundaries(reference_boundary_times, duration_s)
        predicted_intervals = _intervals_from_boundaries(predicted_boundary_times, duration_s)
        precision, recall, f_measure = mir_eval.segment.detection(
            reference_intervals,
            predicted_intervals,
            window=float(tolerance_s),
            trim=bool(trim_edges),
        )
        return {
            "precision": float(precision),
            "recall": float(recall),
            "f_measure": float(f_measure),
            "tolerance_s": float(tolerance_s),
        }

    predicted = _trim_track_edges(predicted_boundary_times, duration_s) if trim_edges else predicted_boundary_times
    reference = _trim_track_edges(reference_boundary_times, duration_s) if trim_edges else reference_boundary_times
    metrics = evaluate_boundaries_seconds(predicted, reference, tolerance_s=tolerance_s)
    metrics["tolerance_s"] = float(tolerance_s)
    return metrics


def evaluate_phrase_boundaries_beats(
    predicted_boundary_times: list[float],
    reference_boundary_times: list[float],
    beat_duration_s: float,
    duration_s: float,
    tolerance_beats: float = 1.0,
    trim_edges: bool = True,
) -> dict[str, float]:
    return evaluate_phrase_boundaries_seconds(
        predicted_boundary_times,
        reference_boundary_times,
        duration_s=duration_s,
        tolerance_s=float(beat_duration_s) * float(tolerance_beats),
        trim_edges=trim_edges,
    )
