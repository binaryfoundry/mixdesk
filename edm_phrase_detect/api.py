from __future__ import annotations

from pathlib import Path
from typing import Any

import numpy as np

from .alignment import validate_and_align_stems
from .bar_features import extract_barwise_features as _extract_barwise_features
from .beatgrid import detect_beats_downbeats as _detect_beats_downbeats
from .config import Config
from .decoding import decode_phrase_boundaries as _decode_phrase_boundaries
from .export import save_result
from .io import load_stems as _load_stems
from .models import Bar, BeatEvent, PhraseBoundary, PhraseResult, PhraseSegment
from .plotting import plot_phrase_result as _plot_phrase_result
from .scoring import GROUP_FEATURES, metrical_prior_for_bar_index, score_phrase_boundaries as _score_phrase_boundaries


def load_stems(track_dir: str | Path, target_sr: int = 44_100) -> tuple[dict[str, np.ndarray], int]:
    stems, sr, _ = _load_stems(track_dir, target_sr)
    return stems, sr


def validate_alignment(stems: dict[str, np.ndarray], sr: int) -> None:
    validate_and_align_stems(stems, sr, Config(target_sr=sr))


def detect_beats_downbeats(
    stems: dict[str, np.ndarray],
    sr: int,
    backend: str = "auto",
) -> tuple[float, list[BeatEvent]]:
    beat_events, bpm, _, _ = _detect_beats_downbeats(stems, sr, Config(target_sr=sr, backend=backend))
    return float(bpm), beat_events


def build_bar_grid(beat_events: list[BeatEvent]) -> list[float]:
    return [float(beat.time_s) for beat in beat_events if int(beat.beat_in_bar) == 1]


def _bars_from_starts(
    bars: list[float],
    beat_events: list[BeatEvent] | None = None,
    duration_s: float | None = None,
) -> list[Bar]:
    starts = [float(value) for value in bars]
    if not starts:
        return []

    beat_times = [float(beat.time_s) for beat in beat_events or []]
    if len(starts) > 1:
        bar_duration = float(np.median(np.diff(np.asarray(starts, dtype=np.float64))))
    elif len(beat_times) > 1:
        bar_duration = float(np.median(np.diff(np.asarray(beat_times, dtype=np.float64)))) * 4.0
    else:
        bar_duration = 1.0

    end_of_last = float(duration_s) if duration_s is not None else starts[-1] + bar_duration
    output: list[Bar] = []
    for index, start_s in enumerate(starts):
        end_s = starts[index + 1] if index + 1 < len(starts) else end_of_last
        inside = [time_s for time_s in beat_times if start_s <= time_s < end_s]
        output.append(Bar(index=index + 1, start_s=start_s, end_s=float(end_s), beat_times=inside))
    return output


def extract_barwise_features(
    stems: dict[str, np.ndarray],
    sr: int,
    bars: list[float],
    beat_events: list[BeatEvent],
) -> dict[str, np.ndarray]:
    bar_objects = _bars_from_starts(bars, beat_events)
    features = _extract_barwise_features(stems, sr, bar_objects, beat_events, Config(target_sr=sr))
    return dict(features.get("feature_arrays", {}))


def _infer_available_groups(arrays: dict[str, Any]) -> list[str]:
    available: list[str] = []
    for group, feature_names in GROUP_FEATURES.items():
        if any(name in arrays for name in feature_names):
            available.append(group)
    return available


def score_phrase_boundaries(features: dict[str, Any]) -> np.ndarray:
    if "feature_arrays" in features:
        feature_package = features
        arrays = features.get("feature_arrays", {})
    else:
        arrays = features
        feature_package = {"feature_arrays": arrays, "available_groups": _infer_available_groups(arrays)}

    num_bars = int(feature_package.get("num_bars") or 0)
    if num_bars <= 0:
        for value in arrays.values():
            arr = np.asarray(value)
            if arr.ndim == 1:
                num_bars = int(len(arr))
                break

    dummy_bars = [Bar(index=index + 1, start_s=float(index), end_s=float(index + 1), beat_times=[]) for index in range(num_bars)]
    scores, _, _ = _score_phrase_boundaries(feature_package, dummy_bars, Config())
    return scores


def decode_phrase_boundaries(
    bars: list[float],
    boundary_scores: np.ndarray,
    allowed_lengths: tuple[int, ...] = (4, 8, 16, 32),
    preferred_lengths: tuple[int, ...] = (8, 16),
) -> list[PhraseBoundary]:
    bar_objects = _bars_from_starts(bars)
    scores = np.asarray(boundary_scores, dtype=np.float64)
    if len(scores) != len(bar_objects):
        fixed = np.zeros(len(bar_objects), dtype=np.float64)
        fixed[: min(len(fixed), len(scores))] = scores[: min(len(fixed), len(scores))]
        scores = fixed

    config = Config(allowed_phrase_bars=allowed_lengths, preferred_phrase_bars=preferred_lengths)
    reasons: list[dict[str, float]] = []
    for index, score in enumerate(scores):
        if index == 0:
            reasons.append({"initial_boundary": 1.0, "candidate_score": 1.0, "audio_score": 1.0})
            continue
        metrical = metrical_prior_for_bar_index(index, config)
        reasons.append(
            {
                "candidate_score": float(score),
                "audio_score": float(score),
                "metrical_prior": float(metrical),
                "multi_stem_agreement": 1.0 if float(score) >= config.strong_boundary_threshold else 0.0,
            }
        )

    boundaries, _, _ = _decode_phrase_boundaries(bar_objects, scores, reasons, config)
    return boundaries


def build_segments(boundaries: list[PhraseBoundary], bars: list[float]) -> list[PhraseSegment]:
    bar_objects = _bars_from_starts(bars)
    if not bar_objects:
        return []

    boundary_by_index = {max(0, int(boundary.bar_index) - 1): boundary for boundary in boundaries}
    selected = sorted(set(boundary_by_index) | {0})
    fallback_confidence = float(boundaries[0].confidence) if boundaries else 1.0
    segments: list[PhraseSegment] = []
    for index, boundary_index in enumerate(selected):
        next_boundary = selected[index + 1] if index + 1 < len(selected) else len(bar_objects)
        if boundary_index >= len(bar_objects) or next_boundary <= boundary_index:
            continue
        start_bar = bar_objects[boundary_index]
        end_bar = bar_objects[next_boundary - 1]
        next_confidence = boundary_by_index.get(next_boundary)
        current_confidence = boundary_by_index.get(boundary_index)
        confidence = (
            next_confidence.confidence
            if next_confidence is not None
            else current_confidence.confidence
            if current_confidence is not None
            else fallback_confidence
        )
        segments.append(
            PhraseSegment(
                start_s=float(start_bar.start_s),
                end_s=float(end_bar.end_s),
                start_bar=int(start_bar.index),
                end_bar=int(end_bar.index),
                length_bars=int(end_bar.index - start_bar.index + 1),
                confidence=float(confidence),
                label="unknown",
            )
        )
    return segments


def detect_phrases(track_dir: str | Path, backend: str = "auto") -> PhraseResult:
    from .cli import detect_track

    result, _, _, _, _ = detect_track(track_dir, Config(backend=backend))
    return result


def save_phrase_json(result: PhraseResult, out_path: str | Path) -> None:
    save_result(result, out_path)


def plot_phrase_result(
    stems: dict[str, np.ndarray],
    sr: int,
    result: PhraseResult,
    out_path: str | Path,
) -> None:
    _plot_phrase_result(result, out_path, stems=stems, sr=sr)
