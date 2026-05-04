from __future__ import annotations

import math
from typing import Any

import librosa
import numpy as np
from scipy import signal

from .alignment import peak_normalize
from .config import Config
from .models import Bar, BeatEvent
from .novelty import cosine_distance, normalize_feature, presence_from_energy, previous_context_median


def _zeros(num_bars: int) -> np.ndarray:
    return np.zeros(num_bars, dtype=np.float64)


def _safe_filter(y: np.ndarray, sr: int, low_hz: float | None = None, high_hz: float | None = None) -> np.ndarray:
    if len(y) < 32:
        return y.astype(np.float32, copy=True)
    nyquist = sr * 0.5
    if low_hz is not None and high_hz is not None:
        sos = signal.butter(4, [low_hz / nyquist, high_hz / nyquist], btype="bandpass", output="sos")
    elif high_hz is not None:
        sos = signal.butter(4, high_hz / nyquist, btype="lowpass", output="sos")
    elif low_hz is not None:
        sos = signal.butter(4, low_hz / nyquist, btype="highpass", output="sos")
    else:
        return y.astype(np.float32, copy=True)
    try:
        return signal.sosfiltfilt(sos, y).astype(np.float32)
    except ValueError:
        return signal.sosfilt(sos, y).astype(np.float32)


def _frame_rms(y: np.ndarray, config: Config) -> np.ndarray:
    if not len(y):
        return np.zeros(0, dtype=np.float64)
    rms = librosa.feature.rms(y=y, frame_length=config.frame_length, hop_length=config.hop_length, center=True)[0]
    return np.log1p(np.nan_to_num(rms.astype(np.float64)))


def _onset_env(y: np.ndarray, sr: int, config: Config) -> np.ndarray:
    if not len(y):
        return np.zeros(0, dtype=np.float64)
    env = librosa.onset.onset_strength(
        y=peak_normalize(y),
        sr=sr,
        hop_length=config.hop_length,
        aggregate=np.median,
    )
    return np.nan_to_num(env.astype(np.float64))


def _frame_times(count: int, sr: int, config: Config) -> np.ndarray:
    return librosa.frames_to_time(np.arange(count), sr=sr, hop_length=config.hop_length)


def _aggregate_frames(values: np.ndarray, times: np.ndarray, bars: list[Bar], agg: str = "mean") -> np.ndarray:
    output = np.zeros(len(bars), dtype=np.float64)
    for index, bar in enumerate(bars):
        mask = (times >= bar.start_s) & (times < bar.end_s)
        selected = values[mask]
        if len(selected) == 0:
            output[index] = 0.0
        elif agg == "median":
            output[index] = float(np.median(selected))
        elif agg == "max":
            output[index] = float(np.max(selected))
        elif agg == "std":
            output[index] = float(np.std(selected))
        else:
            output[index] = float(np.mean(selected))
    return output


def _onset_density_per_bar(y: np.ndarray, sr: int, bars: list[Bar], config: Config) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    env = _onset_env(y, sr, config)
    times = _frame_times(len(env), sr, config)
    if len(env) == 0:
        return _zeros(len(bars)), env, times
    threshold = float(np.percentile(env, 70))
    peaks = np.zeros_like(env)
    if len(env) >= 3:
        local_max = (env[1:-1] >= env[:-2]) & (env[1:-1] >= env[2:]) & (env[1:-1] > threshold)
        peaks[1:-1] = local_max.astype(np.float64)
    density = _aggregate_frames(peaks, times, bars, agg="mean")
    return normalize_feature(density), env, times


def _context_abs_change(values: np.ndarray, context: int = 4) -> np.ndarray:
    output = np.zeros(len(values), dtype=np.float64)
    for index in range(1, len(values)):
        output[index] = abs(float(values[index]) - previous_context_median(values, index, context))
    return normalize_feature(output)


def _entry_exit(presence: np.ndarray, lookback: int = 4) -> tuple[np.ndarray, np.ndarray]:
    entry = np.zeros(len(presence), dtype=np.float64)
    exit_ = np.zeros(len(presence), dtype=np.float64)
    for index in range(1, len(presence)):
        start = max(0, index - lookback)
        prior = float(np.mean(presence[start:index])) if index > start else float(presence[index - 1])
        entry[index] = max(0.0, float(presence[index]) - prior)
        exit_[index] = max(0.0, prior - float(presence[index]))
    return normalize_feature(entry), normalize_feature(exit_)


def _bar_subdivision_profile(y: np.ndarray, sr: int, bars: list[Bar], subdivisions: int = 16) -> np.ndarray:
    profiles = np.zeros((len(bars), subdivisions), dtype=np.float64)
    for bar_index, bar in enumerate(bars):
        duration = max(bar.end_s - bar.start_s, 1e-6)
        for subdivision in range(subdivisions):
            start_s = bar.start_s + duration * (subdivision / subdivisions)
            end_s = bar.start_s + duration * ((subdivision + 1) / subdivisions)
            start = max(0, int(round(start_s * sr)))
            end = min(len(y), int(round(end_s * sr)))
            if end > start:
                profiles[bar_index, subdivision] = float(np.sqrt(np.mean(np.square(y[start:end]))))
    norms = np.linalg.norm(profiles, axis=1, keepdims=True) + 1e-8
    return profiles / norms


def _profile_change(profiles: np.ndarray) -> np.ndarray:
    output = np.zeros(profiles.shape[0], dtype=np.float64)
    for index in range(1, profiles.shape[0]):
        output[index] = cosine_distance(profiles[index], profiles[index - 1])
    return normalize_feature(output)


def _spectral_flux(y: np.ndarray, config: Config) -> np.ndarray:
    if not len(y):
        return np.zeros(0, dtype=np.float64)
    stft = np.abs(librosa.stft(peak_normalize(y), n_fft=config.frame_length, hop_length=config.hop_length))
    if stft.shape[1] < 2:
        return np.zeros(stft.shape[1], dtype=np.float64)
    norm = stft / (np.linalg.norm(stft, axis=0, keepdims=True) + 1e-8)
    diff = np.diff(norm, axis=1)
    flux = np.concatenate([[0.0], np.sqrt(np.sum(np.square(np.maximum(diff, 0.0)), axis=0))])
    return normalize_feature(flux)


def _temporal_delta(feature: np.ndarray) -> np.ndarray:
    delta = np.zeros_like(feature, dtype=np.float64)
    if feature.shape[1] >= 2:
        delta[:, 1:] = np.diff(feature, axis=1)
        delta[:, 0] = delta[:, 1]
    return np.nan_to_num(delta, nan=0.0, posinf=0.0, neginf=0.0)


def _drum_fill_to_boundary(y: np.ndarray, sr: int, bars: list[Bar], config: Config) -> np.ndarray:
    env = _onset_env(y, sr, config)
    times = _frame_times(len(env), sr, config)
    output = np.zeros(len(bars), dtype=np.float64)
    beat_density = np.zeros((len(bars), 4), dtype=np.float64)
    for bar_index, bar in enumerate(bars):
        if len(bar.beat_times) >= 4:
            beat_starts = bar.beat_times[:4]
            beat_ends = bar.beat_times[1:4] + [bar.end_s]
        else:
            step = (bar.end_s - bar.start_s) / 4.0
            beat_starts = [bar.start_s + step * i for i in range(4)]
            beat_ends = [bar.start_s + step * (i + 1) for i in range(4)]
        for beat in range(4):
            mask = (times >= beat_starts[beat]) & (times < beat_ends[beat])
            beat_density[bar_index, beat] = float(np.max(env[mask]) + np.mean(env[mask])) if np.any(mask) else 0.0

    for boundary_index in range(1, len(bars)):
        previous_bar = boundary_index - 1
        final_beat = beat_density[previous_bar, 3]
        start = max(0, previous_bar - 4)
        normal = np.median(beat_density[start : previous_bar + 1, :3])
        output[boundary_index] = max(0.0, final_beat - float(normal))
    return normalize_feature(output)


def _roll_or_build(values: np.ndarray) -> np.ndarray:
    output = np.zeros(len(values), dtype=np.float64)
    x = np.arange(4, dtype=np.float64)
    for index in range(1, len(values)):
        start = max(0, index - 4)
        window = values[start:index]
        if len(window) >= 2:
            xx = x[-len(window) :]
            slope = float(np.polyfit(xx, window, 1)[0])
            output[index] = max(0.0, slope)
    return normalize_feature(output)


def _beat_median_bass_envelope(
    bass_low: np.ndarray,
    sr: int,
    bars: list[Bar],
    beat_events: list[BeatEvent],
    ignore_after_kick_s: float = 0.12,
) -> np.ndarray:
    beat_times = [beat.time_s for beat in beat_events]
    beat_duration = float(np.median(np.diff(beat_times))) if len(beat_times) > 1 else 0.5
    per_bar: list[list[float]] = [[] for _ in bars]
    for beat_time in beat_times:
        start_s = beat_time + ignore_after_kick_s
        end_s = beat_time + beat_duration
        start = max(0, int(round(start_s * sr)))
        end = min(len(bass_low), int(round(end_s * sr)))
        if end <= start:
            continue
        value = float(np.median(np.abs(bass_low[start:end])))
        for bar_index, bar in enumerate(bars):
            if bar.start_s <= beat_time < bar.end_s:
                per_bar[bar_index].append(value)
                break
    return np.array([float(np.median(values)) if values else 0.0 for values in per_bar], dtype=np.float64)


def _sidechain_score(bass_low: np.ndarray, stems: dict[str, np.ndarray], sr: int, bars: list[Bar], config: Config) -> np.ndarray:
    kick_source = stems.get("kick", stems.get("drums"))
    if kick_source is None or not len(bass_low):
        return _zeros(len(bars))

    bass_env = _frame_rms(bass_low, config)
    kick_env = _onset_env(kick_source, sr, config)
    count = min(len(bass_env), len(kick_env))
    if count < 4:
        return _zeros(len(bars))

    bass_dips = np.maximum(float(np.percentile(bass_env[:count], 80)) - bass_env[:count], 0.0)
    kick_norm = kick_env[:count] / (float(np.max(kick_env[:count])) + 1e-8)
    times = _frame_times(count, sr, config)
    frame_score = bass_dips * kick_norm
    return np.clip(_aggregate_frames(frame_score, times, bars, agg="mean") / (np.percentile(frame_score, 95) + 1e-8), 0.0, 1.0)


def _harmonic_source(stems: dict[str, np.ndarray]) -> tuple[str | None, np.ndarray | None]:
    for name in ("synth", "other", "mix"):
        if name in stems:
            return name, stems[name]
    candidates = [name for name in ("bass", "vocals") if name in stems]
    if not candidates:
        return None, None
    length = min(len(stems[name]) for name in candidates)
    mix = np.zeros(length, dtype=np.float32)
    for name in candidates:
        mix += peak_normalize(stems[name][:length])
    return "weighted_harmonic", mix / max(len(candidates), 1)


def _bar_vectors(feature: np.ndarray, times: np.ndarray, bars: list[Bar]) -> np.ndarray:
    vectors = np.zeros((len(bars), feature.shape[0]), dtype=np.float64)
    for index, bar in enumerate(bars):
        mask = (times >= bar.start_s) & (times < bar.end_s)
        if np.any(mask):
            vectors[index] = np.mean(feature[:, mask], axis=1)
    norms = np.linalg.norm(vectors, axis=1, keepdims=True) + 1e-8
    return vectors / norms


def _harmonic_features(y: np.ndarray, sr: int, bars: list[Bar], config: Config) -> dict[str, np.ndarray]:
    y_norm = peak_normalize(y)
    try:
        chroma = librosa.feature.chroma_cqt(y=y_norm, sr=sr, hop_length=config.hop_length)
    except Exception:
        chroma = librosa.feature.chroma_stft(y=y_norm, sr=sr, hop_length=config.hop_length)
    mfcc = librosa.feature.mfcc(y=y_norm, sr=sr, n_mfcc=13, hop_length=config.hop_length)
    centroid = librosa.feature.spectral_centroid(y=y_norm, sr=sr, hop_length=config.hop_length)
    try:
        contrast = librosa.feature.spectral_contrast(y=y_norm, sr=sr, hop_length=config.hop_length)
    except Exception:
        contrast = np.zeros((1, mfcc.shape[1]), dtype=np.float64)
    min_frames = min(chroma.shape[1], mfcc.shape[1], centroid.shape[1], contrast.shape[1])
    chroma = chroma[:, :min_frames]
    mfcc = mfcc[:, :min_frames]
    mfcc_delta = _temporal_delta(mfcc)
    centroid = centroid[:, :min_frames]
    contrast = contrast[:, :min_frames]
    combined = np.vstack([chroma, mfcc, mfcc_delta, centroid, contrast])
    times = _frame_times(min_frames, sr, config)
    vectors = _bar_vectors(combined, times, bars)
    mfcc_delta_vectors = _bar_vectors(mfcc_delta, times, bars)

    local = np.zeros(len(bars), dtype=np.float64)
    contextual = np.zeros(len(bars), dtype=np.float64)
    mfcc_delta_change = np.zeros(len(bars), dtype=np.float64)
    for index in range(1, len(bars)):
        start = max(0, index - 4)
        local[index] = cosine_distance(vectors[index], np.median(vectors[start:index], axis=0))
        mfcc_delta_change[index] = cosine_distance(mfcc_delta_vectors[index], np.median(mfcc_delta_vectors[start:index], axis=0))
    k = max(1, config.novelty_context_bars)
    for index in range(1, len(bars) - 1):
        prev = vectors[max(0, index - k) : index]
        nxt = vectors[index : min(len(bars), index + k)]
        if len(prev) and len(nxt):
            contextual[index] = cosine_distance(np.mean(prev, axis=0), np.mean(nxt, axis=0))

    similarity = np.clip(vectors @ vectors.T, -1.0, 1.0)
    foote = np.zeros(len(bars), dtype=np.float64)
    kernel = max(1, min(config.foote_kernel_bars, max(1, len(bars) // 4)))
    for index in range(1, len(bars) - 1):
        left = similarity[max(0, index - kernel) : index, max(0, index - kernel) : index]
        right = similarity[index : min(len(bars), index + kernel), index : min(len(bars), index + kernel)]
        cross = similarity[max(0, index - kernel) : index, index : min(len(bars), index + kernel)]
        if left.size and right.size and cross.size:
            foote[index] = float(np.mean(left) + np.mean(right) - 2.0 * np.mean(cross))

    harmonic_change = (
        0.30 * normalize_feature(local)
        + 0.30 * normalize_feature(contextual)
        + 0.25 * normalize_feature(foote)
        + 0.15 * normalize_feature(mfcc_delta_change)
    )
    rms = _frame_rms(y, config)
    rms_times = _frame_times(len(rms), sr, config)
    energy = _aggregate_frames(rms, rms_times, bars)
    return {
        "harmonic_presence": presence_from_energy(energy),
        "harmonic_novelty_local": normalize_feature(local),
        "harmonic_novelty_contextual": normalize_feature(contextual),
        "harmonic_foote_novelty": normalize_feature(foote),
        "harmonic_mfcc_delta_change": normalize_feature(mfcc_delta_change),
        "harmonic_texture_change": _context_abs_change(energy),
        "harmonic_change": harmonic_change,
        "chroma_bar": vectors[:, :12] if vectors.shape[1] >= 12 else np.zeros((len(bars), 12)),
        "mfcc_bar": vectors[:, 12:25] if vectors.shape[1] >= 25 else np.zeros((len(bars), 13)),
        "mfcc_delta_bar": vectors[:, 25:38] if vectors.shape[1] >= 38 else np.zeros((len(bars), 13)),
    }


def extract_barwise_features(
    stems: dict[str, np.ndarray],
    sr: int,
    bars: list[Bar],
    beat_events: list[BeatEvent],
    config: Config,
) -> dict[str, Any]:
    num_bars = len(bars)
    features: dict[str, Any] = {
        "num_bars": num_bars,
        "available_groups": [],
        "feature_arrays": {},
    }
    arrays: dict[str, np.ndarray] = features["feature_arrays"]

    if "kick" in stems:
        features["available_groups"].append("kick")
        kick_low = _safe_filter(stems["kick"], sr, low_hz=30.0, high_hz=160.0)
        rms = _frame_rms(kick_low, config)
        times = _frame_times(len(rms), sr, config)
        arrays["kick_low_rms"] = _aggregate_frames(rms, times, bars)
        arrays["kick_presence"] = presence_from_energy(arrays["kick_low_rms"])
        arrays["kick_onset_density"], _, _ = _onset_density_per_bar(kick_low, sr, bars, config)
        arrays["kick_onset_density_change"] = _context_abs_change(arrays["kick_onset_density"])
        arrays["kick_dropout"] = np.concatenate([[0.0], np.maximum(arrays["kick_presence"][:-1] - arrays["kick_presence"][1:], 0.0)])
        kick_return = np.zeros(num_bars, dtype=np.float64)
        for index in range(1, num_bars):
            prior = float(np.mean(arrays["kick_presence"][max(0, index - 4) : index]))
            kick_return[index] = max(0.0, float(arrays["kick_presence"][index]) - prior)
        arrays["kick_return"] = normalize_feature(kick_return)
        arrays["kick_pattern_change"] = _profile_change(_bar_subdivision_profile(kick_low, sr, bars))

    if "drums" in stems:
        features["available_groups"].append("drums")
        drums = stems["drums"]
        rms = _frame_rms(drums, config)
        times = _frame_times(len(rms), sr, config)
        arrays["drums_rms"] = _aggregate_frames(rms, times, bars)
        arrays["drums_presence"] = presence_from_energy(arrays["drums_rms"])
        arrays["drums_onset_density"], _, _ = _onset_density_per_bar(drums, sr, bars, config)
        flux = _spectral_flux(drums, config)
        flux_times = _frame_times(len(flux), sr, config)
        arrays["drums_spectral_flux"] = _aggregate_frames(flux, flux_times, bars)
        arrays["drums_density_change"] = _context_abs_change(arrays["drums_onset_density"])
        arrays["drums_spectral_flux_change"] = _context_abs_change(arrays["drums_spectral_flux"])
        arrays["drums_fill_prev_bar"] = _drum_fill_to_boundary(drums, sr, bars, config)
        arrays["drums_roll_or_build"] = _roll_or_build(arrays["drums_onset_density"])

    if "bass" in stems:
        features["available_groups"].append("bass")
        bass_low = _safe_filter(stems["bass"], sr, low_hz=30.0, high_hz=250.0)
        rms = _frame_rms(bass_low, config)
        times = _frame_times(len(rms), sr, config)
        arrays["bass_low_rms_raw"] = _aggregate_frames(rms, times, bars)
        arrays["bass_de_pumped_envelope"] = _beat_median_bass_envelope(bass_low, sr, bars, beat_events)
        arrays["bass_presence"] = presence_from_energy(arrays["bass_de_pumped_envelope"])
        arrays["bass_entry"], arrays["bass_exit"] = _entry_exit(arrays["bass_presence"])
        raw_change = _profile_change(_bar_subdivision_profile(bass_low, sr, bars))
        arrays["bass_change"] = raw_change
        note_density, _, _ = _onset_density_per_bar(bass_low, sr, bars, config)
        arrays["bass_note_onset_density"] = note_density
        arrays["bass_note_onset_density_change"] = _context_abs_change(note_density)
        arrays["sidechain_pumping_score"] = _sidechain_score(bass_low, stems, sr, bars, config)
        arrays["bass_change_corrected"] = raw_change * (1.0 - 0.6 * arrays["sidechain_pumping_score"])

    harmonic_name, harmonic = _harmonic_source(stems)
    if harmonic is not None:
        features["available_groups"].append("harmonic")
        features["harmonic_source"] = harmonic_name
        arrays.update(_harmonic_features(harmonic, sr, bars, config))

    if "vocals" in stems:
        features["available_groups"].append("vocals")
        vocals = stems["vocals"]
        rms = _frame_rms(vocals, config)
        times = _frame_times(len(rms), sr, config)
        arrays["vocal_rms"] = _aggregate_frames(rms, times, bars)
        arrays["vocal_presence"] = presence_from_energy(arrays["vocal_rms"])
        active_threshold = float(np.percentile(rms, 70)) if len(rms) else 0.0
        active = (rms > active_threshold).astype(np.float64)
        arrays["vocal_activity_ratio"] = _aggregate_frames(active, times, bars)
        arrays["vocal_entry"], arrays["vocal_exit"] = _entry_exit(arrays["vocal_presence"], lookback=8)
        arrays["vocal_syllabic_onset_density"], _, _ = _onset_density_per_bar(vocals, sr, bars, config)
        arrays["vocal_syllabic_onset_change"] = _context_abs_change(arrays["vocal_syllabic_onset_density"])
        first_entry_flag = np.zeros(num_bars, dtype=np.float64)
        if num_bars and np.max(arrays["vocal_entry"]) > 0:
            strong_entries = np.flatnonzero(arrays["vocal_entry"] > 0.5)
            first_entry = int(strong_entries[0]) if len(strong_entries) else int(np.argmax(arrays["vocal_entry"]))
            if first_entry > 0:
                first_entry_flag[first_entry] = 1.0
                arrays["vocal_entry"][first_entry] = min(1.0, arrays["vocal_entry"][first_entry] + 0.25)
        arrays["vocal_first_entry_flag"] = first_entry_flag
        arrays["vocal_activity_change"] = _context_abs_change(arrays["vocal_activity_ratio"])
        pickup = np.zeros(num_bars, dtype=np.float64)
        for boundary_index in range(1, num_bars):
            previous = bars[boundary_index - 1]
            start_s = previous.start_s + (previous.end_s - previous.start_s) * 0.75
            mask = (times >= start_s) & (times < previous.end_s)
            pickup[boundary_index] = float(np.mean(active[mask])) if np.any(mask) else 0.0
        arrays["vocal_phrase_pickup"] = normalize_feature(pickup)

    presence_names = [
        "kick_presence",
        "drums_presence",
        "bass_presence",
        "harmonic_presence",
        "vocal_presence",
    ]
    presence_vectors = []
    for name in presence_names:
        presence_vectors.append(arrays.get(name, _zeros(num_bars)))
    presence_matrix = np.vstack(presence_vectors).T if presence_vectors else np.zeros((num_bars, 0))
    arrays["stem_presence_vector"] = presence_matrix
    combined_change = np.zeros(num_bars, dtype=np.float64)
    entry_count = np.zeros(num_bars, dtype=np.float64)
    exit_count = np.zeros(num_bars, dtype=np.float64)
    for index in range(1, num_bars):
        prior = np.median(presence_matrix[max(0, index - 4) : index], axis=0)
        delta = presence_matrix[index] - prior
        combined_change[index] = float(np.mean(np.abs(delta))) if len(delta) else 0.0
        entry_count[index] = float(np.sum(delta > 0.35))
        exit_count[index] = float(np.sum(delta < -0.35))
    arrays["combined_presence_change"] = normalize_feature(combined_change)
    arrays["instrumentation_entry_count"] = np.clip(entry_count / 5.0, 0.0, 1.0)
    arrays["instrumentation_exit_count"] = np.clip(exit_count / 5.0, 0.0, 1.0)

    features["available_groups"] = sorted(set(features["available_groups"]))
    return features
