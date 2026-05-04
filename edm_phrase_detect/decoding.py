from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from .config import Config
from .models import Bar, PhraseBoundary, PhraseSegment


DEFAULT_ALLOWED_PHRASE_BARS = (4, 8, 16, 32)
DEFAULT_PREFERRED_PHRASE_BARS = (8, 16)


@dataclass(slots=True)
class DecodingDebug:
    selected_boundary_indices: list[int]
    rejected_strong_candidates: list[int]
    confidence_components: dict[int, dict[str, float]]


def _phrase_lengths(config: Config | None) -> tuple[tuple[int, ...], tuple[int, ...]]:
    if config is None:
        return DEFAULT_ALLOWED_PHRASE_BARS, DEFAULT_PREFERRED_PHRASE_BARS

    allowed = tuple(sorted({int(length) for length in config.allowed_phrase_bars if int(length) > 0}))
    if not allowed:
        allowed = DEFAULT_ALLOWED_PHRASE_BARS

    preferred = tuple(sorted({int(length) for length in config.preferred_phrase_bars if int(length) > 0}))
    if not preferred:
        preferred = tuple(length for length in DEFAULT_PREFERRED_PHRASE_BARS if length in allowed) or (max(allowed),)

    return allowed, preferred


def length_prior(length_bars: int, config: Config | None = None) -> float:
    allowed, preferred = _phrase_lengths(config)
    if length_bars in preferred:
        return 1.00 if length_bars == max(preferred) else 0.95
    if length_bars in allowed:
        if length_bars < min(preferred):
            return 0.65
        if length_bars > max(preferred):
            return 0.70
        return 0.80
    nearest = min(abs(length_bars - allowed_length) for allowed_length in allowed)
    return max(0.0, 0.45 - 0.07 * nearest)


def _transition_score(i: int, j: int, candidate_scores: np.ndarray, reasons: list[dict[str, float]], config: Config) -> float:
    length = j - i
    if length < config.min_phrase_bars:
        return -np.inf
    boundary_evidence = float(candidate_scores[j])
    length_evidence = length_prior(length, config)
    agreement = float(reasons[j].get("multi_stem_agreement", 0.0))
    penalty = 0.0
    irregular_bonus = 0.0
    allowed_lengths = _phrase_lengths(config)[0]
    if length == 4 and boundary_evidence < max(0.0, config.strong_boundary_threshold - 0.05):
        penalty += 0.65
    if length not in allowed_lengths:
        nearest = min(abs(length - allowed) for allowed in allowed_lengths)
        penalty += config.irregular_length_penalty * min(nearest, 8) / 8.0
        if boundary_evidence > max(0.0, config.strong_boundary_threshold - 0.15) and agreement >= 0.5:
            penalty *= 0.20
            irregular_bonus = 0.30
    return (
        config.novelty_weight * boundary_evidence
        + config.length_prior_weight * length_evidence
        + config.multi_stem_agreement_weight * agreement
        + irregular_bonus
        - penalty
    )


def _tail_score(i: int, n: int, config: Config) -> float:
    length = n - i
    if length <= 0:
        return -np.inf
    if length < config.min_phrase_bars and i != 0:
        return -np.inf
    return config.length_prior_weight * length_prior(length, config)


def _remove_close_boundaries(boundaries: list[int], candidate_scores: np.ndarray, config: Config) -> list[int]:
    if len(boundaries) <= 1:
        return boundaries
    kept = [boundaries[0]]
    for boundary in boundaries[1:]:
        if boundary - kept[-1] >= config.min_phrase_bars:
            kept.append(boundary)
            continue
        if kept[-1] == 0:
            continue
        if candidate_scores[boundary] > candidate_scores[kept[-1]]:
            kept[-1] = boundary
    return kept


def _cleanup_boundaries(
    boundaries: list[int],
    candidate_scores: np.ndarray,
    reasons: list[dict[str, float]],
    n: int,
    config: Config,
) -> list[int]:
    boundaries = _remove_close_boundaries(sorted(set(boundaries)), candidate_scores, config)
    while len(boundaries) > 1 and n - boundaries[-1] < config.min_phrase_bars:
        boundaries.pop()

    changed = True
    while changed:
        changed = False
        for index in range(1, len(boundaries)):
            boundary = boundaries[index]
            next_boundary = boundaries[index + 1] if index + 1 < len(boundaries) else n
            previous_boundary = boundaries[index - 1]
            merged_length = next_boundary - previous_boundary
            reason = reasons[boundary] if boundary < len(reasons) else {}
            weak_audio = reason.get("audio_score", 0.0) < 0.42 and reason.get("multi_stem_agreement", 0.0) < 0.5
            weak_candidate = candidate_scores[boundary] < config.boundary_threshold
            max_allowed_length = max(_phrase_lengths(config)[0])
            if (weak_candidate or weak_audio) and merged_length <= max_allowed_length:
                del boundaries[index]
                changed = True
                break
    return boundaries


def _insert_skipped_strong_candidates(
    boundaries: list[int],
    candidate_scores: np.ndarray,
    reasons: list[dict[str, float]],
    n: int,
    config: Config,
) -> tuple[list[int], list[int]]:
    rejected: list[int] = []
    selected = sorted(boundaries)
    for candidate in range(1, n):
        if candidate in selected:
            continue
        if candidate_scores[candidate] <= 0.85 or reasons[candidate].get("multi_stem_agreement", 0.0) < 0.6:
            continue
        previous = max(boundary for boundary in selected if boundary < candidate)
        following_values = [boundary for boundary in selected if boundary > candidate]
        following = following_values[0] if following_values else n
        if candidate - previous >= config.min_phrase_bars and following - candidate >= config.min_phrase_bars:
            selected.append(candidate)
            selected.sort()
        else:
            rejected.append(candidate)
    return selected, rejected


def _confidence_for_boundary(
    boundary: int,
    previous: int,
    candidate_scores: np.ndarray,
    reasons: list[dict[str, float]],
    config: Config,
) -> tuple[float, dict[str, float]]:
    if boundary == 0:
        return 1.0, {"initial_boundary": 1.0}
    reason = reasons[boundary]
    local = float(candidate_scores[boundary])
    agreement = float(reason.get("multi_stem_agreement", 0.0))
    metrical = float(reason.get("metrical_prior", 0.0))
    length_plausibility = length_prior(boundary - previous, config)
    confidence = 0.45 * local + 0.25 * agreement + 0.20 * length_plausibility + 0.10 * metrical

    if reason.get("audio_score", 0.0) < 0.2:
        confidence = min(confidence, 0.45)
    if reason.get("kick", 0.0) > 0.45 and reason.get("harmonic", 0.0) > 0.35:
        confidence += 0.05
    if reason.get("vocals", 0.0) > 0.45 and reason.get("harmonic", 0.0) > 0.35:
        confidence += 0.05
    if reason.get("sidechain_penalty", 0.0) > 0.6 and reason.get("bass", 0.0) >= max(reason.get("kick", 0.0), reason.get("drums", 0.0), reason.get("harmonic", 0.0)):
        confidence -= 0.10

    components = {
        "local": local,
        "agreement": agreement,
        "length_plausibility": float(length_plausibility),
        "metrical": metrical,
        "audio_score": float(reason.get("audio_score", 0.0)),
    }
    return float(np.clip(confidence, 0.0, 1.0)), components


def decode_phrase_boundaries(
    bars: list[Bar],
    candidate_scores: np.ndarray,
    reasons: list[dict[str, float]],
    config: Config,
) -> tuple[list[PhraseBoundary], list[PhraseSegment], DecodingDebug]:
    n = len(bars)
    if n == 0:
        return [], [], DecodingDebug([], [], {})

    dp = np.full(n, -np.inf, dtype=np.float64)
    previous = np.full(n, -1, dtype=np.int64)
    dp[0] = 0.0

    for j in range(config.min_phrase_bars, n):
        best_score = -np.inf
        best_previous = -1
        for i in range(0, j):
            if not np.isfinite(dp[i]):
                continue
            transition = _transition_score(i, j, candidate_scores, reasons, config)
            if not np.isfinite(transition):
                continue
            score = dp[i] + transition
            if score > best_score:
                best_score = score
                best_previous = i
        dp[j] = best_score
        previous[j] = best_previous

    best_final_score = -np.inf
    best_last = 0
    for i in range(n):
        if not np.isfinite(dp[i]):
            continue
        score = dp[i] + _tail_score(i, n, config)
        if score > best_final_score:
            best_final_score = score
            best_last = i

    boundaries = []
    current = best_last
    while current >= 0:
        boundaries.append(int(current))
        current = int(previous[current])
    boundaries = sorted(set(boundaries + [0]))
    boundaries = _cleanup_boundaries(boundaries, candidate_scores, reasons, n, config)
    boundaries, rejected = _insert_skipped_strong_candidates(boundaries, candidate_scores, reasons, n, config)

    confidence_components: dict[int, dict[str, float]] = {}
    phrase_boundaries: list[PhraseBoundary] = []
    for index, boundary in enumerate(boundaries):
        previous_boundary = boundaries[index - 1] if index > 0 else 0
        confidence, components = _confidence_for_boundary(boundary, previous_boundary, candidate_scores, reasons, config)
        confidence_components[boundary] = components
        reason = dict(reasons[boundary])
        if boundary == 0:
            reason = {"initial_boundary": 1.0}
        phrase_boundaries.append(
            PhraseBoundary(
                time_s=float(bars[boundary].start_s),
                bar_index=int(bars[boundary].index),
                confidence=confidence,
                reason={key: float(value) for key, value in reason.items()},
            )
        )

    segments: list[PhraseSegment] = []
    for index, boundary in enumerate(boundaries):
        next_boundary = boundaries[index + 1] if index + 1 < len(boundaries) else n
        start_bar = bars[boundary]
        end_bar = bars[next_boundary - 1]
        if index + 1 < len(phrase_boundaries):
            confidence = phrase_boundaries[index + 1].confidence
        else:
            confidence = float(np.mean([phrase_boundaries[index].confidence, 0.7]))
        segments.append(
            PhraseSegment(
                start_s=float(start_bar.start_s),
                end_s=float(end_bar.end_s),
                start_bar=int(start_bar.index),
                end_bar=int(end_bar.index),
                length_bars=int(end_bar.index - start_bar.index + 1),
                confidence=float(np.clip(confidence, 0.0, 1.0)),
                label="unknown",
            )
        )

    debug = DecodingDebug(boundaries, rejected, confidence_components)
    return phrase_boundaries, segments, debug
