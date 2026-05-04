from __future__ import annotations

import importlib.util
import math
import tempfile
from typing import Any

import librosa
import numpy as np
import soundfile as sf

from .alignment import peak_normalize
from .config import Config
from .models import Bar, BeatEvent


def make_rhythm_mix(stems: dict[str, np.ndarray]) -> np.ndarray:
    if "drums" in stems and "kick" in stems:
        length = min(len(stems["drums"]), len(stems["kick"]))
        return 0.65 * peak_normalize(stems["drums"][:length]) + 0.35 * peak_normalize(stems["kick"][:length])
    if "drums" in stems:
        return stems["drums"].astype(np.float32, copy=False)
    if "kick" in stems:
        return stems["kick"].astype(np.float32, copy=False)
    if "mix" in stems:
        return stems["mix"].astype(np.float32, copy=False)

    weights = {
        "drums": 0.50,
        "kick": 0.40,
        "bass": 0.20,
        "synth": 0.10,
        "vocals": 0.05,
        "other": 0.10,
    }
    available = [name for name in weights if name in stems]
    if not available:
        raise ValueError("Cannot create rhythm mix without stems.")
    length = min(len(stems[name]) for name in available)
    result = np.zeros(length, dtype=np.float32)
    total = 0.0
    for name in available:
        result += weights[name] * peak_normalize(stems[name][:length])
        total += weights[name]
    return result / max(total, 1e-8)


def _float_tempo(value: Any) -> float:
    if isinstance(value, np.ndarray):
        return float(value.flat[0]) if value.size else 0.0
    if isinstance(value, (list, tuple)):
        return float(value[0]) if value else 0.0
    return float(value)


def _periodic_fallback_beats(onset_env: np.ndarray, sr: int, config: Config, duration_s: float) -> tuple[float, np.ndarray]:
    if len(onset_env) < 4:
        raise ValueError("Could not detect enough onset frames for beat fallback.")

    threshold = np.percentile(onset_env, 75)
    peak_frames = np.flatnonzero(onset_env >= threshold)
    if len(peak_frames) < 4:
        peak_frames = np.flatnonzero(onset_env >= np.percentile(onset_env, 60))
    peak_times = librosa.frames_to_time(peak_frames, sr=sr, hop_length=config.hop_length)

    diffs: list[float] = []
    for i, start in enumerate(peak_times[:-1]):
        for end in peak_times[i + 1 : i + 5]:
            delta = float(end - start)
            if 0.30 <= delta <= 0.80:
                diffs.append(delta)
    if not diffs:
        raise ValueError("Could not infer beat period from onset peaks.")

    beat_period = float(np.median(diffs))
    first = float(peak_times[0])
    while first - beat_period >= 0.0:
        first -= beat_period
    beat_times = np.arange(first, duration_s + beat_period * 0.5, beat_period, dtype=np.float64)
    beat_times = beat_times[beat_times >= 0.0]
    tempo = 60.0 / beat_period
    return tempo, beat_times


def _detect_librosa(stems: dict[str, np.ndarray], sr: int, config: Config) -> tuple[np.ndarray, float, dict[str, Any]]:
    rhythm = make_rhythm_mix(stems)
    duration_s = float(len(rhythm)) / float(sr)
    attempts: list[tuple[str, np.ndarray, int]] = [("rhythm_mix", rhythm, 100)]
    if "drums" in stems:
        attempts.append(("drums", stems["drums"], 80))
    if "mix" in stems:
        attempts.append(("mix", stems["mix"], 80))
    attempts.append(("rhythm_mix_loose", rhythm, 40))

    debug: dict[str, Any] = {"attempts": []}
    last_onset_env: np.ndarray | None = None
    for name, y, tightness in attempts:
        onset_env = librosa.onset.onset_strength(
            y=peak_normalize(y),
            sr=sr,
            hop_length=config.hop_length,
            aggregate=np.median,
        )
        last_onset_env = onset_env
        tempo, beat_frames = librosa.beat.beat_track(
            onset_envelope=onset_env,
            sr=sr,
            hop_length=config.hop_length,
            trim=False,
            tightness=tightness,
        )
        beat_times = librosa.frames_to_time(beat_frames, sr=sr, hop_length=config.hop_length)
        debug["attempts"].append({"source": name, "tightness": tightness, "beats": int(len(beat_times))})
        if len(beat_times) >= 8:
            return beat_times.astype(np.float64), _float_tempo(tempo), debug

    if last_onset_env is not None:
        tempo, beat_times = _periodic_fallback_beats(last_onset_env, sr, config, duration_s)
        debug["attempts"].append({"source": "periodic_fallback", "beats": int(len(beat_times))})
        if len(beat_times) >= 8:
            return beat_times.astype(np.float64), tempo, debug

    raise ValueError("Fewer than 8 beats detected.")


def _detect_madmom(stems: dict[str, np.ndarray], sr: int, config: Config) -> tuple[np.ndarray, list[int], float, dict[str, Any]]:
    if importlib.util.find_spec("madmom") is None:
        raise ImportError("madmom is not installed.")

    from madmom.features.downbeats import DBNDownBeatTrackingProcessor, RNNDownBeatProcessor

    rhythm = make_rhythm_mix(stems)
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=True) as temp:
        sf.write(temp.name, rhythm, sr)
        activations = RNNDownBeatProcessor()(temp.name)
        tracked = DBNDownBeatTrackingProcessor(beats_per_bar=[config.beats_per_bar], fps=100)(activations)

    if len(tracked) < 8:
        raise ValueError("madmom detected fewer than 8 beats.")
    beat_times = np.asarray(tracked[:, 0], dtype=np.float64)
    beat_numbers = [int(value) for value in tracked[:, 1]]
    intervals = np.diff(beat_times)
    bpm = 60.0 / float(np.median(intervals)) if len(intervals) else 0.0
    return beat_times, beat_numbers, bpm, {"beats": int(len(beat_times))}


def _detect_essentia(stems: dict[str, np.ndarray], sr: int, config: Config) -> tuple[np.ndarray, float, dict[str, Any]]:
    if importlib.util.find_spec("essentia") is None:
        raise ImportError("essentia is not installed.")

    import essentia.standard as es

    rhythm = make_rhythm_mix(stems).astype(np.float32)
    extractor = es.RhythmExtractor2013(method="multifeature")
    bpm, beat_times, _, _, _ = extractor(rhythm)
    beat_times = np.asarray(beat_times, dtype=np.float64)
    if len(beat_times) < 8:
        raise ValueError("essentia detected fewer than 8 beats.")
    return beat_times, float(bpm), {"beats": int(len(beat_times))}


def _window_energy(y: np.ndarray, sr: int, center_s: float, before_s: float, after_s: float) -> float:
    start = max(0, int(round((center_s - before_s) * sr)))
    end = min(len(y), int(round((center_s + after_s) * sr)))
    if end <= start:
        return 0.0
    return float(np.sqrt(np.mean(np.square(y[start:end]))))


def _bar_summary(y: np.ndarray, sr: int, beat_times: np.ndarray, start_index: int, beats_per_bar: int) -> float:
    if start_index + beats_per_bar >= len(beat_times):
        beat_period = float(np.median(np.diff(beat_times))) if len(beat_times) > 1 else 0.5
        end_s = beat_times[start_index] + beat_period * beats_per_bar
    else:
        end_s = beat_times[start_index + beats_per_bar]
    start = max(0, int(round(beat_times[start_index] * sr)))
    end = min(len(y), int(round(end_s * sr)))
    if end <= start:
        return 0.0
    segment = y[start:end]
    return float(np.sqrt(np.mean(np.square(segment))))


def infer_downbeat_phase(
    beat_times: np.ndarray,
    stems: dict[str, np.ndarray],
    sr: int,
    config: Config,
) -> tuple[int, dict[str, Any]]:
    rhythm = make_rhythm_mix(stems)
    kick = stems.get("kick", rhythm)
    harmonic = stems.get("synth", stems.get("other", stems.get("mix", rhythm)))
    phases: dict[int, dict[str, float]] = {}

    all_energies = np.array([_window_energy(rhythm, sr, float(t), 0.040, 0.120) for t in beat_times])
    energy_std = float(np.std(all_energies)) + 1e-8

    for phase in range(config.beats_per_bar):
        candidate_indices = np.arange(phase, len(beat_times), config.beats_per_bar)
        non_candidate_indices = np.array([i for i in range(len(beat_times)) if i % config.beats_per_bar != phase])
        if len(candidate_indices) < 2:
            phases[phase] = {
                "downbeat_accent": -1.0,
                "kick": -1.0,
                "bar_start_novelty": -1.0,
                "harmonic_reset": -1.0,
                "consistency": -1.0,
                "total": -1.0,
            }
            continue

        candidate_energy = all_energies[candidate_indices]
        non_candidate_energy = all_energies[non_candidate_indices] if len(non_candidate_indices) else all_energies
        downbeat_accent = (float(np.mean(candidate_energy)) - float(np.mean(non_candidate_energy))) / energy_std

        kick_energies = np.array([_window_energy(kick, sr, float(beat_times[i]), 0.040, 0.120) for i in candidate_indices])
        kick_score = (float(np.mean(kick_energies)) - float(np.mean(all_energies))) / energy_std

        bar_values = np.array([
            _bar_summary(rhythm, sr, beat_times, int(i), config.beats_per_bar)
            for i in candidate_indices
            if i < len(beat_times)
        ])
        bar_start_novelty = float(np.std(bar_values)) / (float(np.mean(bar_values)) + 1e-8) if len(bar_values) else 0.0

        harmonic_values = np.array([
            _bar_summary(harmonic, sr, beat_times, int(i), config.beats_per_bar)
            for i in candidate_indices
            if i < len(beat_times)
        ])
        harmonic_reset = float(np.std(np.diff(harmonic_values))) if len(harmonic_values) > 2 else 0.0

        intervals = np.diff(beat_times[candidate_indices])
        consistency = -float(np.std(intervals)) / (float(np.mean(intervals)) + 1e-8) if len(intervals) else -1.0
        begins_early_bonus = 0.50 if phase == 0 else (0.08 if phase == 1 else 0.0)
        total = (
            0.35 * downbeat_accent
            + 0.30 * kick_score
            + 0.15 * bar_start_novelty
            + 0.10 * harmonic_reset
            + 0.10 * consistency
            + begins_early_bonus
        )
        phases[phase] = {
            "downbeat_accent": float(downbeat_accent),
            "kick": float(kick_score),
            "bar_start_novelty": float(bar_start_novelty),
            "harmonic_reset": float(harmonic_reset),
            "consistency": float(consistency),
            "total": float(total),
        }

    best_phase = max(phases, key=lambda phase: phases[phase]["total"])
    totals = sorted((float(score["total"]) for score in phases.values()), reverse=True)
    phase_margin = totals[0] - totals[1] if len(totals) >= 2 else 0.0
    return int(best_phase), {"phase_scores": phases, "best_phase": int(best_phase), "phase_margin": float(phase_margin)}


def detect_beats_downbeats(
    stems: dict[str, np.ndarray],
    sr: int,
    config: Config,
) -> tuple[list[BeatEvent], float, str, dict[str, Any]]:
    backend = config.backend.lower()
    debug: dict[str, Any] = {}

    if backend == "madmom":
        beat_times, beat_numbers, bpm, backend_debug = _detect_madmom(stems, sr, config)
        events = [BeatEvent(float(t), int(n)) for t, n in zip(beat_times, beat_numbers)]
        return events, bpm, "madmom", backend_debug
    if backend == "essentia":
        beat_times, bpm, backend_debug = _detect_essentia(stems, sr, config)
        selected_backend = "essentia"
    elif backend == "auto":
        selected_backend = "librosa"
        for optional_backend in ("madmom", "essentia"):
            try:
                if optional_backend == "madmom":
                    beat_times, beat_numbers, bpm, backend_debug = _detect_madmom(stems, sr, config)
                    events = [BeatEvent(float(t), int(n)) for t, n in zip(beat_times, beat_numbers)]
                    return events, bpm, "madmom", backend_debug
                beat_times, bpm, backend_debug = _detect_essentia(stems, sr, config)
                selected_backend = "essentia"
                break
            except (ImportError, ValueError):
                continue
        else:
            beat_times, bpm, backend_debug = _detect_librosa(stems, sr, config)
    elif backend == "librosa":
        selected_backend = "librosa"
        beat_times, bpm, backend_debug = _detect_librosa(stems, sr, config)
    else:
        raise ValueError(f"Unknown beat backend: {config.backend}")

    phase, phase_debug = infer_downbeat_phase(beat_times, stems, sr, config)
    debug.update(backend_debug)
    debug.update(phase_debug)
    events = [
        BeatEvent(time_s=float(time_s), beat_in_bar=int(((index - phase) % config.beats_per_bar) + 1))
        for index, time_s in enumerate(beat_times)
    ]
    return events, float(bpm), selected_backend, debug


def build_bars(beat_events: list[BeatEvent], duration_s: float, config: Config) -> tuple[list[Bar], list[str]]:
    warnings: list[str] = []
    downbeat_indices = [index for index, beat in enumerate(beat_events) if beat.beat_in_bar == 1]
    if len(downbeat_indices) < 4:
        raise ValueError("Fewer than 4 bars were detected.")

    beat_times = [beat.time_s for beat in beat_events]
    bar_starts = [beat_events[index].time_s for index in downbeat_indices]
    bar_intervals = np.diff(bar_starts)
    median_bar_duration = float(np.median(bar_intervals)) if len(bar_intervals) else 0.0
    if median_bar_duration <= 0:
        raise ValueError("Invalid bar duration inferred from downbeats.")

    min_tail_bar_duration = median_bar_duration * 0.5
    while len(downbeat_indices) > 4:
        tail_duration = float(duration_s) - float(beat_events[downbeat_indices[-1]].time_s)
        if tail_duration >= min_tail_bar_duration:
            break
        removed = downbeat_indices.pop()
        warnings.append(
            "Discarded trailing downbeat at "
            f"{beat_events[removed].time_s:.3f}s because it leaves only "
            f"{max(0.0, tail_duration):.3f}s of audio, less than half a bar."
        )

    if len(downbeat_indices) < 4:
        raise ValueError("Fewer than 4 full bars were detected.")

    bar_starts = [beat_events[index].time_s for index in downbeat_indices]
    bar_intervals = np.diff(bar_starts)
    median_bar_duration = float(np.median(bar_intervals)) if len(bar_intervals) else median_bar_duration

    if len(bar_intervals) and float(np.max(bar_intervals)) > median_bar_duration * 1.75:
        warnings.append("Large gaps detected in downbeat sequence.")

    bars: list[Bar] = []
    for bar_number, beat_index in enumerate(downbeat_indices, start=1):
        start_s = beat_events[beat_index].time_s
        if bar_number < len(downbeat_indices):
            end_s = beat_events[downbeat_indices[bar_number]].time_s
        else:
            end_s = min(duration_s, start_s + median_bar_duration)
        inside = [time for time in beat_times if start_s <= time < end_s]
        bars.append(Bar(index=bar_number, start_s=float(start_s), end_s=float(end_s), beat_times=inside))

    if len(bars) < 4:
        raise ValueError("Fewer than 4 bars were constructed.")
    return bars, warnings
