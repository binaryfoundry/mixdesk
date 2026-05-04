from __future__ import annotations

import numpy as np
from scipy.ndimage import median_filter


def sanitize_array(x: np.ndarray | list[float], length: int | None = None) -> np.ndarray:
    arr = np.asarray(x, dtype=np.float64)
    arr = np.nan_to_num(arr, nan=0.0, posinf=0.0, neginf=0.0)
    if length is not None and len(arr) != length:
        result = np.zeros(length, dtype=np.float64)
        result[: min(length, len(arr))] = arr[: min(length, len(arr))]
        return result
    return arr


def robust_zscore(x: np.ndarray | list[float]) -> np.ndarray:
    arr = sanitize_array(x)
    if len(arr) == 0:
        return arr
    median = float(np.median(arr))
    mad = float(np.median(np.abs(arr - median)))
    z = (arr - median) / ((1.4826 * mad) + 1e-8)
    return np.clip(z, -4.0, 4.0)


def normalize_feature(x: np.ndarray | list[float], smooth: bool = False) -> np.ndarray:
    z = robust_zscore(x)
    if len(z) == 0:
        return z
    positive = np.maximum(z, 0.0)
    scale = float(np.percentile(positive, 95)) + 1e-8
    normalized = np.clip(positive / scale, 0.0, 1.0)
    if smooth and len(normalized) >= 3:
        normalized = median_filter(normalized, size=3, mode="nearest")
    return np.nan_to_num(normalized, nan=0.0, posinf=0.0, neginf=0.0)


def presence_from_energy(energy: np.ndarray | list[float]) -> np.ndarray:
    e = sanitize_array(energy)
    if len(e) == 0:
        return e
    noise_floor = float(np.percentile(e, 20))
    active_level = float(np.percentile(e, 80))
    midpoint = (noise_floor + active_level) * 0.5
    scale = max((active_level - noise_floor) / 6.0, 1e-6)
    argument = np.clip((e - midpoint) / scale, -60.0, 60.0)
    return 1.0 / (1.0 + np.exp(-argument))


def positive_difference(current: np.ndarray, prior: np.ndarray) -> np.ndarray:
    return np.maximum(sanitize_array(current) - sanitize_array(prior), 0.0)


def previous_context_median(values: np.ndarray, index: int, context: int = 4) -> float:
    if index <= 0:
        return float(values[0]) if len(values) else 0.0
    start = max(0, index - context)
    return float(np.median(values[start:index])) if index > start else float(values[index])


def cosine_distance(a: np.ndarray, b: np.ndarray) -> float:
    aa = np.asarray(a, dtype=np.float64)
    bb = np.asarray(b, dtype=np.float64)
    denom = (float(np.linalg.norm(aa)) * float(np.linalg.norm(bb))) + 1e-8
    return float(1.0 - (np.dot(aa, bb) / denom))
