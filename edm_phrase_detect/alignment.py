from __future__ import annotations

import math

import librosa
import numpy as np

from .config import Config


REFERENCE_PRIORITY = ("drums", "kick", "mix")
REFERENCE_WEIGHTS = {
    "drums": 0.50,
    "kick": 0.40,
    "bass": 0.20,
    "synth": 0.10,
    "vocals": 0.05,
    "other": 0.10,
    "mix": 0.30,
}


def peak_normalize(y: np.ndarray) -> np.ndarray:
    peak = float(np.max(np.abs(y))) if len(y) else 0.0
    return y.astype(np.float32, copy=False) / max(peak, 1e-8)


def _weighted_sum(stems: dict[str, np.ndarray]) -> np.ndarray:
    if not stems:
        return np.zeros(0, dtype=np.float32)

    length = min(len(stem) for stem in stems.values())
    result = np.zeros(length, dtype=np.float32)
    total_weight = 0.0
    for name, stem in stems.items():
        weight = REFERENCE_WEIGHTS.get(name, 0.05)
        result += float(weight) * peak_normalize(stem[:length])
        total_weight += float(weight)
    return result / max(total_weight, 1e-8)


def choose_timing_reference(stems: dict[str, np.ndarray]) -> tuple[str, np.ndarray]:
    for name in REFERENCE_PRIORITY:
        if name in stems:
            return name, stems[name]
    return "weighted_sum", _weighted_sum(stems)


def _onset_envelope(y: np.ndarray, sr: int, config: Config) -> np.ndarray:
    if not len(y):
        return np.zeros(0, dtype=np.float32)
    env = librosa.onset.onset_strength(
        y=peak_normalize(y),
        sr=sr,
        hop_length=config.hop_length,
        aggregate=np.median,
    )
    return np.nan_to_num(env.astype(np.float32), copy=False)


def _energy_envelope(y: np.ndarray, config: Config) -> np.ndarray:
    if not len(y):
        return np.zeros(0, dtype=np.float32)
    rms = librosa.feature.rms(
        y=peak_normalize(y),
        frame_length=config.frame_length,
        hop_length=config.hop_length,
        center=True,
    )[0]
    return np.nan_to_num(np.log1p(rms).astype(np.float32), copy=False)


def _activity_ratio(y: np.ndarray, config: Config) -> float:
    env = _energy_envelope(y, config)
    if len(env) == 0:
        return 0.0
    high = float(np.percentile(env, 80))
    low = float(np.percentile(env, 20))
    threshold = low + (high - low) * 0.45
    if high <= low + 1e-8:
        return 0.0
    return float(np.mean(env > threshold))


def _best_lag_frames(reference: np.ndarray, target: np.ndarray, max_frames: int) -> tuple[int, float]:
    count = min(len(reference), len(target))
    if count < 3:
        return 0, 0.0

    ref = reference[:count].astype(np.float64)
    tgt = target[:count].astype(np.float64)
    ref = ref - np.mean(ref)
    tgt = tgt - np.mean(tgt)
    ref_std = float(np.std(ref))
    tgt_std = float(np.std(tgt))
    if ref_std <= 1e-8 or tgt_std <= 1e-8:
        return 0, 0.0

    best_lag = 0
    best_score = -np.inf
    for lag in range(-max_frames, max_frames + 1):
        if lag > 0:
            a = ref[:-lag]
            b = tgt[lag:]
        elif lag < 0:
            a = ref[-lag:]
            b = tgt[:lag]
        else:
            a = ref
            b = tgt
        if len(a) < 3 or len(b) < 3:
            continue
        denom = (float(np.linalg.norm(a)) * float(np.linalg.norm(b))) + 1e-8
        score = float(np.dot(a, b) / denom)
        if score > best_score:
            best_score = score
            best_lag = lag

    return best_lag, best_score


def _shift_to_align(y: np.ndarray, lag_samples: int) -> np.ndarray:
    if lag_samples == 0:
        return y.copy()

    result = np.zeros_like(y)
    if lag_samples > 0:
        result[: max(0, len(y) - lag_samples)] = y[lag_samples:]
    else:
        shift = -lag_samples
        result[shift:] = y[: max(0, len(y) - shift)]
    return result


def validate_and_align_stems(
    stems: dict[str, np.ndarray],
    sr: int,
    config: Config,
) -> tuple[dict[str, np.ndarray], float, dict[str, object]]:
    if not stems:
        raise ValueError("No stems were loaded.")

    warnings: list[str] = []
    shifts_ms: dict[str, float] = {}
    lengths = {name: len(stem) for name, stem in stems.items()}
    min_length = min(lengths.values())
    max_length = max(lengths.values())
    mismatch_s = float(max_length - min_length) / float(sr)
    if mismatch_s > 0.25:
        raise ValueError(f"Stem length mismatch is {mismatch_s:.3f}s, which exceeds 0.25s.")

    aligned = {name: stem[:min_length].astype(np.float32, copy=True) for name, stem in stems.items()}
    if mismatch_s > 0:
        warnings.append(f"Trimmed stems by up to {mismatch_s * 1000.0:.1f}ms to match shortest stem.")

    reference_name, reference = choose_timing_reference(aligned)
    reference = reference[:min_length]
    reference_onset = _onset_envelope(reference, sr, config)
    reference_energy = _energy_envelope(reference, config)

    if config.auto_align:
        allowed_frames = max(1, int(math.ceil((config.max_alignment_shift_ms / 1000.0) * sr / config.hop_length)))
        search_ms = max(config.max_alignment_shift_ms * 3.0, 100.0)
        search_frames = max(allowed_frames, int(math.ceil((search_ms / 1000.0) * sr / config.hop_length)))

        for name, stem in list(aligned.items()):
            if name == reference_name:
                shifts_ms[name] = 0.0
                continue

            sparse = name == "vocals" and _activity_ratio(stem, config) <= 0.10
            if sparse:
                warnings.append(f"Skipped alignment for sparse stem '{name}'.")
                shifts_ms[name] = 0.0
                continue

            target_env = _energy_envelope(stem, config)
            ref_env = reference_energy
            lag_frames, score = _best_lag_frames(ref_env, target_env, search_frames)
            lag_ms = float(lag_frames * config.hop_length) / float(sr) * 1000.0

            if score < 0.55:
                warnings.append(f"Skipped alignment for stem '{name}' because correlation was weak ({score:.3f}).")
                shifts_ms[name] = 0.0
                continue

            if abs(lag_frames) > allowed_frames:
                raise ValueError(
                    f"Stem '{name}' appears misaligned by {lag_ms:.1f}ms "
                    f"(correlation {score:.3f}), exceeding {config.max_alignment_shift_ms:.1f}ms."
                )

            lag_samples = int(round((lag_ms / 1000.0) * sr))
            aligned[name] = _shift_to_align(stem, lag_samples)
            shifts_ms[name] = round(lag_ms, 3)
    else:
        shifts_ms = {name: 0.0 for name in aligned}

    duration_s = float(min_length) / float(sr)
    debug = {
        "reference_stem": reference_name,
        "alignment_shifts_ms": shifts_ms,
        "warnings": warnings,
        "duration_s": duration_s,
    }
    return aligned, duration_s, debug
