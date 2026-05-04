from __future__ import annotations

from typing import Any

import numpy as np

from .config import Config
from .models import Bar


GROUP_FEATURES = {
    "drums": (
        "drums_fill_prev_bar",
        "drums_density_change",
        "drums_spectral_flux_change",
        "drums_roll_or_build",
    ),
    "kick": (
        "kick_return",
        "kick_dropout",
        "kick_pattern_change",
        "kick_onset_density_change",
    ),
    "bass": (
        "bass_entry",
        "bass_exit",
        "bass_change_corrected",
        "bass_note_onset_density_change",
    ),
    "harmonic": (
        "harmonic_novelty_local",
        "harmonic_novelty_contextual",
        "harmonic_foote_novelty",
        "harmonic_mfcc_delta_change",
        "harmonic_texture_change",
    ),
    "vocals": (
        "vocal_entry",
        "vocal_exit",
        "vocal_activity_change",
        "vocal_syllabic_onset_change",
        "vocal_phrase_pickup",
        "vocal_first_entry_flag",
    ),
}

GROUP_WEIGHTS = {
    "drums": (0.40, 0.25, 0.20, 0.15),
    "kick": (0.35, 0.30, 0.20, 0.15),
    "bass": (0.35, 0.30, 0.25, 0.10),
    "harmonic": (0.26, 0.26, 0.24, 0.14, 0.10),
    "vocals": (0.30, 0.18, 0.14, 0.16, 0.12, 0.10),
}


DEFAULT_ALLOWED_PHRASE_BARS = (4, 8, 16, 32)
DEFAULT_PREFERRED_PHRASE_BARS = (8, 16)


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


def _array(arrays: dict[str, np.ndarray], name: str, length: int) -> np.ndarray:
    value = arrays.get(name)
    if value is None:
        return np.zeros(length, dtype=np.float64)
    result = np.asarray(value, dtype=np.float64)
    if result.ndim != 1:
        return np.zeros(length, dtype=np.float64)
    if len(result) != length:
        fixed = np.zeros(length, dtype=np.float64)
        fixed[: min(length, len(result))] = result[: min(length, len(result))]
        return fixed
    return np.nan_to_num(result, nan=0.0, posinf=0.0, neginf=0.0)


def metrical_prior_for_bar_index(internal_bar_index: int, config: Config | None = None) -> float:
    if internal_bar_index == 0:
        return 1.0
    distance = internal_bar_index
    allowed, preferred = _phrase_lengths(config)
    strongest_preferred = max(preferred)
    if distance % strongest_preferred == 0:
        return 1.0
    if any(distance % length == 0 for length in preferred):
        return 0.85
    if any(distance % length == 0 for length in allowed):
        return 0.55
    return 0.0


def _available_fusion_weights(available_groups: list[str], config: Config) -> dict[str, float]:
    weighted = {
        group: float(config.fusion_weights[group])
        for group in available_groups
        if group in config.fusion_weights and group in GROUP_FEATURES
    }
    total = sum(weighted.values())
    if total <= 0:
        return {}
    return {group: weight / total for group, weight in weighted.items()}


def score_phrase_boundaries(
    features: dict[str, Any],
    bars: list[Bar],
    config: Config,
) -> tuple[np.ndarray, list[dict[str, float]], dict[str, np.ndarray]]:
    num_bars = len(bars)
    arrays: dict[str, np.ndarray] = features.get("feature_arrays", {})
    available_groups = [group for group in features.get("available_groups", []) if group in GROUP_FEATURES]
    fusion_weights = _available_fusion_weights(available_groups, config)
    group_scores: dict[str, np.ndarray] = {}

    for group in GROUP_FEATURES:
        score = np.zeros(num_bars, dtype=np.float64)
        if group in fusion_weights:
            for feature_name, weight in zip(GROUP_FEATURES[group], GROUP_WEIGHTS[group]):
                score += float(weight) * _array(arrays, feature_name, num_bars)
        group_scores[group] = np.clip(score, 0.0, 1.0)

    sidechain = _array(arrays, "sidechain_pumping_score", num_bars)
    instrumentation_change = np.maximum.reduce(
        [
            _array(arrays, "combined_presence_change", num_bars),
            _array(arrays, "instrumentation_entry_count", num_bars),
            _array(arrays, "instrumentation_exit_count", num_bars),
        ]
    )
    candidate_scores = np.zeros(num_bars, dtype=np.float64)
    reasons: list[dict[str, float]] = []

    for index in range(num_bars):
        if index == 0:
            candidate_scores[index] = 1.0
            reasons.append({"initial_boundary": 1.0, "candidate_score": 1.0, "audio_score": 1.0})
            continue

        available_values = [group_scores[group][index] for group in fusion_weights]
        agreement = float(np.mean([value > 0.5 for value in available_values])) if available_values else 0.0
        raw_audio_score = float(sum(fusion_weights[group] * group_scores[group][index] for group in fusion_weights))
        audio_score = float(min(1.0, raw_audio_score + 0.08 * instrumentation_change[index]))
        metrical = metrical_prior_for_bar_index(index, config)
        candidate = ((1.0 - config.metrical_prior_weight) * audio_score) + (config.metrical_prior_weight * metrical)
        if audio_score < 0.15 and agreement < 0.5:
            candidate = min(candidate, 0.40)

        candidate_scores[index] = float(np.clip(candidate, 0.0, 1.0))
        reasons.append(
            {
                "drums": float(group_scores["drums"][index]),
                "kick": float(group_scores["kick"][index]),
                "bass": float(group_scores["bass"][index]),
                "harmonic": float(group_scores["harmonic"][index]),
                "vocals": float(group_scores["vocals"][index]),
                "metrical_prior": float(metrical),
                "multi_stem_agreement": agreement,
                "instrumentation_change": float(instrumentation_change[index]),
                "audio_score": audio_score,
                "candidate_score": float(candidate_scores[index]),
                "sidechain_penalty": float(sidechain[index]) if len(sidechain) else 0.0,
            }
        )

    return candidate_scores, reasons, group_scores
