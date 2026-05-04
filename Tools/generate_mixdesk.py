#!/usr/bin/env python3
"""Generate mixdesk.json files for track stem folders."""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any

import numpy as np
from PIL import Image, ImageDraw

from edm_phrase_detect.cli import detect_loaded_stems
from edm_phrase_detect.config import Config
from edm_phrase_detect.export import to_jsonable
from edm_phrase_detect.models import PhraseResult, StemInfo


STEM_MATCHES: tuple[tuple[str, tuple[tuple[str, ...], ...]], ...] = (
    ("no_bass", (("no", "bass"), ("nobass",))),
    ("no_drum", (("no", "drum"), ("no", "drums"), ("nodrum",), ("nodrums",))),
    ("no_vocals", (("no", "vocals"), ("no", "vocal"), ("novocals",), ("novocal",))),
    ("kick", (("kick",),)),
    ("synth", (("synth",), ("pad",), ("keys",))),
    ("other", (("other",),)),
    ("mix", (("mix",), ("full", "mix"), ("master",))),
    ("instrumental", (("instrumental",), ("instrument",))),
    ("vocals", (("vocals",), ("vocal",))),
    ("drum", (("drum",), ("drums",))),
    ("bass", (("bass",),)),
)

ENTRY_STEMS = ("kick", "drum", "bass", "synth", "vocals", "other", "mix", "no_bass", "no_drum", "no_vocals", "instrumental")
AUDIO_EXTENSIONS = {".aac", ".aif", ".aiff", ".alac", ".flac", ".m4a", ".mp3", ".ogg", ".opus", ".wav", ".wma"}
BPM_TAGS = ("BPM", "TBPM", "bpm", "tmpo")
KEY_TAGS = ("TKEY", "INITIAL_KEY", "initialkey", "initial_key", "KEY", "key")
TITLE_TAGS = ("title", "TITLE", "TIT2")

DEFAULT_BEATS_PER_BAR = 4
DEFAULT_ANALYSIS_BACKEND = "auto"
DEFAULT_ANALYSIS_SAMPLE_RATE = 44_100
DEFAULT_WAVEFORM_SAMPLE_RATE = 11_025
MIN_PHRASE_BARS = 4
ALLOWED_PHRASE_LENGTHS = (4, 8, 16, 32)
PREFERRED_PHRASE_LENGTHS = (8, 16)
PHRASE_ANALYSIS_IMAGE_NAME = "mixdesk_phrase_analysis.png"

# MixDesk JSON indexing convention:
# - start_bar is 0-based inclusive.
# - end_bar is 0-based exclusive.
# - beat_index is 0-based.
# - final selected boundary uses bar_index == len(bars).
# Display-only 1-based bar numbers are emitted separately as display_* fields.

DECODE_CACHE: dict[tuple[str, int, int, int], np.ndarray] = {}


def round_float(value: Any, digits: int = 6) -> float:
    try:
        return round(float(value), digits)
    except (TypeError, ValueError):
        return 0.0


def run_ffprobe(path: Path) -> dict[str, Any]:
    command = [
        "ffprobe",
        "-v",
        "error",
        "-show_entries",
        "format=duration:format_tags:stream=codec_type,sample_rate,channels,duration",
        "-of",
        "json",
        str(path),
    ]
    result = subprocess.run(command, check=True, capture_output=True, text=True)
    data = json.loads(result.stdout or "{}")
    return data if isinstance(data, dict) else {}


def format_section(probe: dict[str, Any]) -> dict[str, Any]:
    section = probe.get("format", {})
    return section if isinstance(section, dict) else {}


def audio_stream(probe: dict[str, Any]) -> dict[str, Any]:
    for stream in probe.get("streams", []):
        if isinstance(stream, dict) and stream.get("codec_type") == "audio":
            return stream
    return {}


def decode_mono_np(path: Path, sample_rate: int) -> np.ndarray:
    stat = path.stat()
    cache_key = (str(path.resolve()), sample_rate, int(stat.st_mtime_ns), int(stat.st_size))
    cached = DECODE_CACHE.get(cache_key)
    if cached is not None:
        return cached.copy()

    command = [
        "ffmpeg",
        "-v",
        "error",
        "-i",
        str(path),
        "-ac",
        "1",
        "-ar",
        str(sample_rate),
        "-f",
        "f32le",
        "-",
    ]
    result = subprocess.run(command, check=True, capture_output=True)
    audio = np.frombuffer(result.stdout, dtype="<f4").astype(np.float32, copy=True)
    DECODE_CACHE[cache_key] = audio
    return audio.copy()


def get_tags(probe: dict[str, Any]) -> dict[str, str]:
    tags = format_section(probe).get("tags", {})
    return tags if isinstance(tags, dict) else {}


def get_first_tag(tags: dict[str, str], names: tuple[str, ...]) -> str | None:
    lowered = {key.lower(): value for key, value in tags.items()}
    for name in names:
        value = tags.get(name)
        if value not in (None, ""):
            return str(value)
        value = lowered.get(name.lower())
        if value not in (None, ""):
            return str(value)
    return None


def normalize_bpm(value: str | None) -> int | float | None:
    if value is None:
        return None
    try:
        number = float(value)
    except ValueError:
        return None
    return int(number) if number.is_integer() else number


def normalize_duration(value: Any) -> int | float | None:
    if value in (None, ""):
        return None
    try:
        number = float(value)
    except (TypeError, ValueError):
        return None
    return int(number) if number.is_integer() else number


def file_metadata(probe: dict[str, Any]) -> dict[str, Any]:
    section = format_section(probe)
    return {
        "duration": normalize_duration(section.get("duration")),
        "tags": get_tags(probe),
    }


def filename_tokens(path: Path) -> tuple[str, ...]:
    cleaned = "".join(character.lower() if character.isalnum() else " " for character in path.stem)
    return tuple(token for token in cleaned.split() if token)


def contains_token_phrase(tokens: tuple[str, ...], phrase: tuple[str, ...]) -> bool:
    if not tokens:
        return False
    compact = "".join(tokens)
    if len(phrase) == 1:
        return phrase[0] in tokens or phrase[0] == compact
    phrase_len = len(phrase)
    return any(tokens[index : index + phrase_len] == phrase for index in range(0, len(tokens) - phrase_len + 1))


def classify_stem(path: Path) -> str | None:
    tokens = filename_tokens(path)
    for stem_name, phrases in STEM_MATCHES:
        if any(contains_token_phrase(tokens, phrase) for phrase in phrases):
            return stem_name
    return None


def package_stem_name(stem_name: str) -> str | None:
    if stem_name == "drum":
        return "drums"
    if stem_name in {"kick", "bass", "synth", "vocals", "other", "mix"}:
        return stem_name
    return None


def pick_stem_files(directory: Path) -> dict[str, Path]:
    stems: dict[str, Path] = {}
    for path in sorted(directory.iterdir()):
        if not path.is_file() or path.suffix.lower() not in AUDIO_EXTENSIONS:
            continue
        stem_name = classify_stem(path)
        if stem_name is not None and stem_name not in stems:
            stems[stem_name] = path
    return stems


def pick_primary_track_file(directory: Path) -> Path | None:
    stem_files = pick_stem_files(directory)
    if "mix" in stem_files:
        return stem_files["mix"]

    audio_files = [path for path in sorted(directory.iterdir()) if path.is_file() and path.suffix.lower() in AUDIO_EXTENSIONS]
    if not audio_files:
        return None

    classified = set(stem_files.values())
    unclassified = [path for path in audio_files if path not in classified]
    candidates = unclassified or [stem_files[name] for name in ("instrumental", "no_vocals", "no_drum", "no_bass") if name in stem_files]
    if not candidates:
        candidates = audio_files
    return max(candidates, key=lambda path: path.stat().st_size)


def make_stem_info(name: str, path: Path, audio: np.ndarray, target_sr: int) -> StemInfo:
    probe = run_ffprobe(path)
    stream = audio_stream(probe)
    duration = normalize_duration(format_section(probe).get("duration"))
    original_sample_rate = int(stream.get("sample_rate") or target_sr)
    channels = int(stream.get("channels") or 1)
    original_num_samples = int(round(float(duration) * original_sample_rate)) if duration is not None else int(len(audio))
    return StemInfo(
        name=name,
        path=str(path),
        original_sample_rate=original_sample_rate,
        target_sample_rate=target_sr,
        original_num_samples=original_num_samples,
        resampled_num_samples=int(len(audio)),
        duration_s=float(len(audio)) / float(target_sr),
        channels=channels,
    )


def add_detector_stem(
    stems: dict[str, np.ndarray],
    stem_info: dict[str, StemInfo],
    name: str,
    path: Path,
    target_sr: int,
) -> None:
    if name in stems:
        return
    audio = decode_mono_np(path, target_sr)
    stems[name] = audio
    stem_info[name] = make_stem_info(name, path, audio, target_sr)


def load_detector_stems(stem_files: dict[str, Path], primary_track: Path | None, target_sr: int) -> tuple[dict[str, np.ndarray], dict[str, StemInfo]]:
    stems: dict[str, np.ndarray] = {}
    stem_info: dict[str, StemInfo] = {}

    if primary_track is not None and primary_track.exists():
        add_detector_stem(stems, stem_info, "mix", primary_track, target_sr)

    for source_name, path in stem_files.items():
        target_name = package_stem_name(source_name)
        if target_name is not None:
            add_detector_stem(stems, stem_info, target_name, path, target_sr)

    harmonic_sources = [
        stem_files.get("synth"),
        stem_files.get("other"),
        stem_files.get("instrumental"),
        stem_files.get("no_vocals"),
        stem_files.get("no_drum"),
        stem_files.get("no_bass"),
    ]
    for path in harmonic_sources:
        if path is not None and path.exists() and "synth" not in stems:
            add_detector_stem(stems, stem_info, "synth", path, target_sr)
            break
    synth_path = Path(stem_info["synth"].path) if "synth" in stem_info else None
    for path in harmonic_sources:
        if path is not None and path.exists() and "other" not in stems and path != synth_path:
            add_detector_stem(stems, stem_info, "other", path, target_sr)
            break

    if not stems:
        raise ValueError("No stems could be prepared for edm_phrase_detect.")
    return stems, stem_info


def make_config(backend: str, target_sr: int) -> Config:
    return Config(
        target_sr=target_sr,
        backend=backend,
        beats_per_bar=DEFAULT_BEATS_PER_BAR,
        min_phrase_bars=MIN_PHRASE_BARS,
        allowed_phrase_bars=ALLOWED_PHRASE_LENGTHS,
        preferred_phrase_bars=PREFERRED_PHRASE_LENGTHS,
    )


def seconds_per_beat(beats: list[float]) -> float:
    if len(beats) < 2:
        return 0.0
    beat_times = np.asarray(beats, dtype=np.float64)
    beat_indices = np.arange(len(beat_times), dtype=np.float64)
    centered_indices = beat_indices - float(np.mean(beat_indices))
    centered_times = beat_times - float(np.mean(beat_times))
    denominator = float(np.dot(centered_indices, centered_indices))
    if denominator > 0.0:
        slope = float(np.dot(centered_indices, centered_times) / denominator)
        if slope > 0.0:
            return slope
    return float((beat_times[-1] - beat_times[0]) / max(1, len(beat_times) - 1))


def bar_beat_indices(result: PhraseResult, debug: dict[str, Any], beats_per_bar: int = DEFAULT_BEATS_PER_BAR) -> list[int]:
    beat_events = debug.get("beat_events", [])
    if isinstance(beat_events, list):
        downbeat_indices: list[int] = []
        for event in beat_events:
            if not isinstance(event, dict):
                continue
            try:
                if int(event.get("beat_in_bar", 0)) == 1:
                    downbeat_indices.append(int(event.get("beat_index", len(downbeat_indices) * beats_per_bar)))
            except (TypeError, ValueError):
                continue
        if len(downbeat_indices) >= len(result.bars):
            return downbeat_indices[: len(result.bars)]

    beats = [float(value) for value in result.beats]
    indices: list[int] = []
    for fallback_index, bar_time in enumerate(result.bars):
        if beats:
            closest = min(range(len(beats)), key=lambda beat_index: abs(beats[beat_index] - float(bar_time)))
            if abs(beats[closest] - float(bar_time)) <= max(0.05, seconds_per_beat(beats) * 0.5):
                indices.append(int(closest))
                continue
        indices.append(int(fallback_index * beats_per_bar))
    return indices


def beat_index_for_bar(bar_indices: list[int], bar_index: int, beats_per_bar: int = DEFAULT_BEATS_PER_BAR) -> int:
    if 0 <= bar_index < len(bar_indices):
        return int(bar_indices[bar_index])
    if bar_index >= len(bar_indices) and bar_indices:
        return int(bar_indices[-1] + ((bar_index - len(bar_indices) + 1) * beats_per_bar))
    return int(max(0, bar_index) * beats_per_bar)


def detector_result_to_beat_grid(result: PhraseResult, debug: dict[str, Any]) -> dict[str, Any]:
    metadata = result.metadata
    beats = [float(value) for value in result.beats]
    beats_per_bar = int(metadata.get("beats_per_bar") or DEFAULT_BEATS_PER_BAR)
    bar_indices = bar_beat_indices(result, debug, beats_per_bar)
    return {
        "tempo": round_float(result.bpm, 6),
        "bpm": max(1, round(float(result.bpm))),
        "first_beat_offset_seconds": round_float(beats[0] if beats else 0.0),
        "first_bar_offset_seconds": round_float(result.bars[0] if result.bars else (beats[0] if beats else 0.0)),
        "seconds_per_beat": round_float(seconds_per_beat(beats), 9),
        "beats_per_bar": beats_per_bar,
        "duration_seconds": round_float(result.duration_s, 3),
        "beat_times_seconds": [round_float(value) for value in beats],
        "downbeats": [round_float(value) for value in result.downbeats],
        "bars": [round_float(value) for value in result.bars],
        "bar_beat_indices": bar_indices,
        "beat_events": debug.get("beat_events", []),
        "beat_confidence": round_float(metadata.get("beat_confidence", 0.0), 4),
        "downbeat_confidence": round_float(metadata.get("downbeat_confidence", 0.0), 4),
        "backend": metadata.get("selected_backend", debug.get("selected_backend", "unknown")),
    }


def feature_arrays(debug: dict[str, Any]) -> dict[str, Any]:
    features = debug.get("features", {})
    arrays = features.get("feature_arrays", {}) if isinstance(features, dict) else {}
    return arrays if isinstance(arrays, dict) else {}


def one_dimensional(values: Any, expected_len: int) -> list[float] | None:
    if not isinstance(values, list) or len(values) != expected_len:
        return None
    if any(isinstance(value, list) for value in values):
        return None
    return [float(value) for value in values]


def feature_slice(debug: dict[str, Any], name: str, start_bar: int, end_bar: int) -> list[float]:
    values = one_dimensional(feature_arrays(debug).get(name), max(0, int(debug.get("features", {}).get("num_bars", 0))))
    if values is None:
        return []
    start = max(0, min(len(values), start_bar))
    end = max(start, min(len(values), end_bar))
    return values[start:end]


def mean_feature(debug: dict[str, Any], name: str, start_bar: int, end_bar: int) -> float:
    values = feature_slice(debug, name, start_bar, end_bar)
    return float(np.mean(values)) if values else 0.0


def max_feature(debug: dict[str, Any], name: str, start_bar: int, end_bar: int) -> float:
    values = feature_slice(debug, name, start_bar, end_bar)
    return max(values) if values else 0.0


def phrase_role_flags(debug: dict[str, Any], start_bar: int, end_bar: int) -> dict[str, bool]:
    drums = max(mean_feature(debug, "drums_presence", start_bar, end_bar), mean_feature(debug, "kick_presence", start_bar, end_bar))
    return {
        "has_drums": drums > 0.35,
        "has_bass": mean_feature(debug, "bass_presence", start_bar, end_bar) > 0.35,
        "has_vocal": max(mean_feature(debug, "vocal_presence", start_bar, end_bar), mean_feature(debug, "vocal_activity_ratio", start_bar, end_bar)) > 0.30,
        "has_melody": mean_feature(debug, "harmonic_presence", start_bar, end_bar) > 0.30,
    }


def segment_energy(debug: dict[str, Any], start_bar: int, end_bar: int) -> float:
    values = [
        mean_feature(debug, "kick_presence", start_bar, end_bar),
        mean_feature(debug, "drums_presence", start_bar, end_bar),
        mean_feature(debug, "bass_presence", start_bar, end_bar),
        mean_feature(debug, "harmonic_presence", start_bar, end_bar),
        mean_feature(debug, "vocal_presence", start_bar, end_bar),
    ]
    present = [value for value in values if value > 0.0]
    return round_float(float(np.mean(present)) if present else 0.0, 4)


def label_reason(debug: dict[str, Any], start_bar: int, end_bar: int) -> dict[str, float]:
    return {
        "kick_presence": round_float(mean_feature(debug, "kick_presence", start_bar, end_bar), 4),
        "drums_presence": round_float(mean_feature(debug, "drums_presence", start_bar, end_bar), 4),
        "bass_presence": round_float(mean_feature(debug, "bass_presence", start_bar, end_bar), 4),
        "harmonic_presence": round_float(mean_feature(debug, "harmonic_presence", start_bar, end_bar), 4),
        "vocal_activity": round_float(mean_feature(debug, "vocal_activity_ratio", start_bar, end_bar), 4),
        "entry_count": round_float(mean_feature(debug, "instrumentation_entry_count", start_bar, end_bar), 4),
        "exit_count": round_float(mean_feature(debug, "instrumentation_exit_count", start_bar, end_bar), 4),
    }


def label_confidence(reason: dict[str, float], confidence: float) -> float:
    spread = max(reason.values(), default=0.0) - min(reason.values(), default=0.0)
    return round_float(max(0.25, min(1.0, 0.55 * confidence + 0.45 * spread)), 4)


def scalar_bar_features(result: PhraseResult, debug: dict[str, Any]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    bar_table = debug.get("bar_table", [])
    arrays = feature_arrays(debug)
    num_bars = len(result.bars)
    bar_indices = bar_beat_indices(result, debug)
    for index, start_s in enumerate(result.bars):
        source = bar_table[index] if isinstance(bar_table, list) and index < len(bar_table) and isinstance(bar_table[index], dict) else {}
        row: dict[str, Any] = {
            "bar": index,
            "display_bar_number": index + 1,
            "beat_index": beat_index_for_bar(bar_indices, index),
            "start_s": round_float(start_s),
            "end_s": round_float(source.get("end_s", result.bars[index + 1] if index + 1 < num_bars else result.duration_s)),
        }
        for name, values in arrays.items():
            one_d = one_dimensional(values, num_bars)
            if one_d is not None:
                row[name] = round_float(one_d[index], 4)
        rows.append(row)
    return rows


def boundary_candidates(result: PhraseResult, debug: dict[str, Any]) -> list[dict[str, Any]]:
    scores = debug.get("candidate_boundary_scores", [])
    reasons = debug.get("candidate_reasons", [])
    bar_indices = bar_beat_indices(result, debug)
    candidates: list[dict[str, Any]] = []
    for index, bar_time in enumerate(result.bars):
        reason = reasons[index] if isinstance(reasons, list) and index < len(reasons) and isinstance(reasons[index], dict) else {}
        score = scores[index] if isinstance(scores, list) and index < len(scores) else 0.0
        candidates.append(
            {
                "bar_index": index,
                "display_bar_number": index + 1,
                "beat_index": beat_index_for_bar(bar_indices, index),
                "time_s": round_float(bar_time),
                "score": round_float(score, 4),
                "reason": {str(key): round_float(value, 4) for key, value in reason.items()},
            }
        )
    return candidates


def selected_boundaries(result: PhraseResult, candidates: list[dict[str, Any]], bar_indices: list[int]) -> list[dict[str, Any]]:
    selected: list[dict[str, Any]] = []
    for boundary in result.boundaries:
        internal_bar = max(0, int(boundary.bar_index) - 1)
        reason = {str(key): round_float(value, 4) for key, value in boundary.reason.items()}
        selected.append(
            {
                "bar_index": internal_bar,
                "display_bar_number": internal_bar + 1,
                "beat_index": beat_index_for_bar(bar_indices, internal_bar),
                "time_s": round_float(boundary.time_s),
                "score": round_float(candidates[internal_bar]["score"] if internal_bar < len(candidates) else boundary.confidence, 4),
                "confidence": round_float(boundary.confidence, 4),
                "reason": reason,
            }
        )

    final_bar = len(result.bars)
    if not selected or selected[-1]["bar_index"] != final_bar:
        selected.append(
            {
                "bar_index": final_bar,
                "display_bar_number": final_bar + 1,
                "beat_index": beat_index_for_bar(bar_indices, final_bar),
                "time_s": round_float(result.duration_s),
                "score": 1.0,
                "confidence": 1.0,
                "reason": {"final_boundary": 1.0},
            }
        )
    return selected


def phrase_reason_for_end(selected: list[dict[str, Any]], end_bar: int, start_bar: int) -> dict[str, float]:
    for boundary in selected:
        if int(boundary.get("bar_index", -1)) == end_bar:
            return dict(boundary.get("reason", {}))
    for boundary in selected:
        if int(boundary.get("bar_index", -1)) == start_bar:
            return dict(boundary.get("reason", {}))
    return {}


def detector_result_to_phrases(result: PhraseResult, debug: dict[str, Any], selected: list[dict[str, Any]], bar_indices: list[int]) -> list[dict[str, Any]]:
    phrases: list[dict[str, Any]] = []
    for segment in result.segments:
        start_bar = max(0, int(segment.start_bar) - 1)
        end_bar = max(start_bar + 1, int(segment.end_bar))
        beat_index = beat_index_for_bar(bar_indices, start_bar)
        beat_count = max(1, beat_index_for_bar(bar_indices, end_bar) - beat_index)
        reason = phrase_reason_for_end(selected, end_bar, start_bar)
        label_data = label_reason(debug, start_bar, end_bar)
        confidence = round_float(segment.confidence, 4)
        phrase_type = "groove" if segment.label == "unknown" else segment.label
        phrases.append(
            {
                "type": phrase_type,
                "beat_index": beat_index,
                "beat_count": beat_count,
                "start_bar": start_bar,
                "end_bar": end_bar,
                "display_start_bar_number": start_bar + 1,
                "display_end_bar_number": end_bar,
                "start_s": round_float(segment.start_s),
                "end_s": round_float(segment.end_s),
                "energy": segment_energy(debug, start_bar, end_bar),
                "confidence": confidence,
                "label_confidence": label_confidence(label_data, confidence),
                "label_reason": label_data,
                **phrase_role_flags(debug, start_bar, end_bar),
                "reason": reason,
            }
        )
    return phrases


def detector_result_to_phrase_analysis(result: PhraseResult, debug: dict[str, Any]) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    bar_indices = bar_beat_indices(result, debug)
    candidates = boundary_candidates(result, debug)
    selected = selected_boundaries(result, candidates, bar_indices)
    phrases = detector_result_to_phrases(result, debug, selected, bar_indices)
    metadata = result.metadata
    missing_stems = list(metadata.get("missing_stems", []))
    warnings = list(metadata.get("warnings", []))
    if missing_stems:
        warnings.append("Missing stems: " + ", ".join(missing_stems))

    phrase_analysis = {
        "status": "stem_novelty_dp_v1",
        "method": "offline_stem_barwise_v1",
        "backend": metadata.get("selected_backend", debug.get("selected_backend", "unknown")),
        "degraded": bool(missing_stems) or float(metadata.get("grid_confidence", 1.0)) < 0.5,
        "schema_version": result.schema_version,
        "beats_per_bar": int(metadata.get("beats_per_bar") or DEFAULT_BEATS_PER_BAR),
        "bars_per_phrase_prior": list(ALLOWED_PHRASE_LENGTHS),
        "preferred_phrase_lengths": list(PREFERRED_PHRASE_LENGTHS),
        "bar_index_convention": "0_based_start_inclusive_end_exclusive",
        "available_stems": list(metadata.get("stems_used", [])),
        "missing_stems": missing_stems,
        "warnings": warnings,
        "bar_features": scalar_bar_features(result, debug),
        "boundary_candidates": candidates,
        "selected_boundaries": selected,
        "segments": [dict(phrase) for phrase in phrases],
        "debug": {
            "backend": metadata.get("selected_backend", debug.get("selected_backend", "unknown")),
            "available_stems": list(metadata.get("stems_used", [])),
            "missing_stems": missing_stems,
            "warnings": warnings,
            "beat_confidence": round_float(metadata.get("beat_confidence", 0.0), 4),
            "downbeat_confidence": round_float(metadata.get("downbeat_confidence", 0.0), 4),
            "grid_confidence": round_float(metadata.get("grid_confidence", 0.0), 4),
            "alignment_shifts_ms": metadata.get("alignment_shifts_ms", {}),
            "dp_selected_boundary_indices": debug.get("dp_selected_boundary_indices", []),
            "rejected_strong_candidates": debug.get("rejected_strong_candidates", []),
            "final_confidence_components": debug.get("final_confidence_components", {}),
        },
    }
    return phrase_analysis, phrases


def peak_for_pixel(samples: np.ndarray, start_index: int, end_index: int) -> float:
    if end_index <= start_index:
        end_index = start_index + 1
    window = samples[max(0, start_index) : min(len(samples), end_index)]
    if len(window) == 0:
        return 0.0
    return float(min(1.0, np.max(np.abs(window))))


def render_phrase_analysis_image(
    output_path: Path,
    primary_track: Path,
    stem_files: dict[str, Path],
    beat_grid: dict[str, Any] | None,
    phrase_analysis: dict[str, Any] | None = None,
    width: int = 1800,
    height: int = 720,
) -> None:
    if beat_grid is None:
        return

    track_rows = [
        ("full", primary_track, (44, 195, 238)),
        ("drums", stem_files.get("drum"), (245, 185, 45)),
        ("bass", stem_files.get("bass"), (105, 210, 105)),
        ("vocals", stem_files.get("vocals"), (235, 95, 165)),
    ]
    decoded: list[tuple[str, np.ndarray, tuple[int, int, int]]] = []
    for label, path, color in track_rows:
        if path is None or not path.exists():
            continue
        samples = decode_mono_np(path, DEFAULT_WAVEFORM_SAMPLE_RATE)
        if len(samples):
            peak = max(1e-8, float(np.max(np.abs(samples))))
            decoded.append((label, samples / peak, color))

    if not decoded:
        return

    image = Image.new("RGB", (width, height), (12, 28, 34))
    draw = ImageDraw.Draw(image, "RGBA")
    left = 36
    right = width - 16
    top = 28
    bottom = height - 42
    timeline_width = right - left
    row_gap = 12
    row_height = (bottom - top - (row_gap * (len(decoded) - 1))) // len(decoded)
    duration_seconds = float(beat_grid.get("duration_seconds") or 0.0)
    beats_per_bar = int(beat_grid.get("beats_per_bar") or DEFAULT_BEATS_PER_BAR)

    draw.rectangle((0, 0, width, height), fill=(12, 28, 34, 255))
    draw.line((left, top - 12, right, top - 12), fill=(65, 205, 230, 210), width=2)
    draw.line((left, bottom + 10, right, bottom + 10), fill=(65, 205, 230, 210), width=2)

    if duration_seconds > 0.0:
        for index, beat_time in enumerate(beat_grid.get("beat_times_seconds", [])):
            x = left + int((float(beat_time) / duration_seconds) * timeline_width)
            if left <= x <= right:
                is_bar = index % max(1, beats_per_bar) == 0
                color = (220, 235, 238, 175) if is_bar else (115, 145, 150, 80)
                draw.line((x, top - 4, x, bottom + 4), fill=color, width=2 if is_bar else 1)

    for row_index, (label, samples, color) in enumerate(decoded):
        row_top = top + row_index * (row_height + row_gap)
        row_bottom = row_top + row_height
        center = (row_top + row_bottom) // 2
        half_height = max(1, (row_height // 2) - 8)
        draw.rectangle((left, row_top, right, row_bottom), outline=(60, 100, 110, 120), fill=(10, 25, 31, 120))
        draw.line((left, center, right, center), fill=(90, 115, 120, 100), width=1)

        duration_samples = max(1, len(samples))
        points: list[tuple[int, int]] = []
        lower_points: list[tuple[int, int]] = []
        for x in range(left, right):
            normalized_start = (x - left) / float(timeline_width)
            normalized_end = (x + 1 - left) / float(timeline_width)
            start_index = int(normalized_start * duration_samples)
            end_index = int(normalized_end * duration_samples)
            peak = peak_for_pixel(samples, start_index, end_index)
            points.append((x, center - int(peak * half_height)))
            lower_points.append((x, center + int(peak * half_height)))

        draw.polygon(points + list(reversed(lower_points)), fill=(*color, 115))
        draw.line(points, fill=(*color, 230), width=1)
        draw.line(lower_points, fill=(*color, 230), width=1)
        draw.text((6, row_top + 6), label, fill=(210, 225, 228, 220))

    if duration_seconds > 0.0 and phrase_analysis is not None:
        for bar_time in beat_grid.get("bars", []):
            x = left + int((float(bar_time) / duration_seconds) * timeline_width)
            if left <= x <= right:
                draw.line((x, top - 8, x, bottom + 8), fill=(230, 245, 245, 135), width=1)

        for candidate in phrase_analysis.get("boundary_candidates", []):
            x = left + int((float(candidate.get("time_s", 0.0)) / duration_seconds) * timeline_width)
            if left <= x <= right:
                alpha = int(60 + 120 * max(0.0, min(1.0, float(candidate.get("score", 0.0)))))
                draw.line((x, top, x, bottom), fill=(255, 210, 80, alpha), width=1)

        for boundary in phrase_analysis.get("selected_boundaries", []):
            x = left + int((float(boundary.get("time_s", 0.0)) / duration_seconds) * timeline_width)
            if left <= x <= right:
                draw.line((x, top - 12, x, bottom + 12), fill=(255, 70, 90, 245), width=3)

        for segment in phrase_analysis.get("segments", []):
            start_s = float(segment.get("start_s", 0.0))
            end_s = float(segment.get("end_s", start_s))
            label = str(segment.get("type", ""))
            if label:
                x = left + int((((start_s + end_s) * 0.5) / duration_seconds) * timeline_width)
                if left <= x <= right:
                    draw.text((x - 18, bottom + 16), label, fill=(245, 250, 252, 245))

        candidates = phrase_analysis.get("boundary_candidates", [])
        if candidates:
            curve_top = bottom + 24
            curve_bottom = height - 10
            previous_point: tuple[int, int] | None = None
            for candidate in candidates:
                x = left + int((float(candidate.get("time_s", 0.0)) / duration_seconds) * timeline_width)
                score = max(0.0, min(1.0, float(candidate.get("score", 0.0))))
                y = curve_bottom - int(score * max(1, curve_bottom - curve_top))
                if previous_point is not None:
                    draw.line((previous_point[0], previous_point[1], x, y), fill=(255, 185, 48, 235), width=2)
                previous_point = (x, y)

    image.save(output_path)


def build_detector_outputs(
    stem_files: dict[str, Path],
    primary_track: Path,
    backend: str,
    target_sr: int,
) -> tuple[dict[str, Any], dict[str, Any], list[dict[str, Any]]]:
    stems, stem_info = load_detector_stems(stem_files, primary_track, target_sr)
    result, debug, _, _, _ = detect_loaded_stems(stems, target_sr, make_config(backend, target_sr), stem_info)
    beat_grid = detector_result_to_beat_grid(result, debug)
    phrase_analysis, phrases = detector_result_to_phrase_analysis(result, debug)
    return beat_grid, phrase_analysis, phrases


def build_mixdesk(
    directory: Path,
    backend: str = DEFAULT_ANALYSIS_BACKEND,
    target_sr: int = DEFAULT_ANALYSIS_SAMPLE_RATE,
) -> dict[str, Any] | None:
    stem_files = pick_stem_files(directory)
    if not stem_files:
        return None

    primary_track = pick_primary_track_file(directory) or stem_files.get("mix")
    if primary_track is None:
        return None

    metadata: dict[str, dict[str, Any]] = {}
    entries: dict[str, dict[str, Any]] = {}
    primary_probe = run_ffprobe(primary_track)
    primary_tags = get_tags(primary_probe)
    track_name = get_first_tag(primary_tags, TITLE_TAGS) or directory.name
    duration = normalize_duration(format_section(primary_probe).get("duration"))
    original = {
        "file": primary_track.name,
        "metadata": file_metadata(primary_probe),
    }

    for stem_name in ENTRY_STEMS:
        path = stem_files.get(stem_name)
        if path is None:
            entries[stem_name] = {"file": None, "key": None}
            continue

        probe = run_ffprobe(path)
        tags = get_tags(probe)
        metadata[stem_name] = probe
        entries[stem_name] = {
            "file": path.name,
            "key": get_first_tag(tags, KEY_TAGS),
            "duration": normalize_duration(format_section(probe).get("duration")),
        }

    drum_tags = get_tags(metadata.get("drum", {}))
    bpm = normalize_bpm(get_first_tag(drum_tags, BPM_TAGS))
    if duration is None and "drum" in metadata:
        duration = normalize_duration(format_section(metadata["drum"]).get("duration"))

    beat_grid, phrase_analysis, phrases = build_detector_outputs(stem_files, primary_track, backend, target_sr)
    if bpm is None:
        bpm = beat_grid["tempo"]

    phrase_image_path = directory / PHRASE_ANALYSIS_IMAGE_NAME
    render_phrase_analysis_image(phrase_image_path, primary_track, stem_files, beat_grid, phrase_analysis)
    if phrase_image_path.exists():
        phrase_analysis["image"] = phrase_image_path.name

    return {
        "track_name": track_name,
        "duration": duration if duration is not None else beat_grid.get("duration_seconds"),
        "bpm": bpm,
        "beat_grid": beat_grid,
        "phrase_analysis": phrase_analysis,
        "phrases": phrases,
        "original": original,
        "entries": entries,
    }


def iter_target_directories(root: Path) -> list[Path]:
    directories = [path for path in root.rglob("*") if path.is_dir()]
    if root.is_dir():
        directories.insert(0, root)
    return directories


def main() -> int:
    parser = argparse.ArgumentParser(description="Create mixdesk.json in each subdirectory that contains audio stems.")
    parser.add_argument("root", nargs="?", default=".", type=Path, help="Root folder to scan. Defaults to the current directory.")
    parser.add_argument("--dry-run", action="store_true", help="Print files that would be written without changing them.")
    parser.add_argument(
        "--backend",
        default=DEFAULT_ANALYSIS_BACKEND,
        choices=("auto", "librosa", "madmom", "essentia"),
        help="Beat/downbeat backend. Defaults to auto.",
    )
    parser.add_argument(
        "--target-sr",
        default=DEFAULT_ANALYSIS_SAMPLE_RATE,
        type=int,
        help=f"Analysis sample rate for offline stem features. Defaults to {DEFAULT_ANALYSIS_SAMPLE_RATE}.",
    )
    args = parser.parse_args()

    if shutil.which("ffprobe") is None or shutil.which("ffmpeg") is None:
        print("ffmpeg and ffprobe are required on PATH. Install FFmpeg or add it to PATH.", file=sys.stderr)
        return 1

    root = args.root.resolve()
    written = 0

    for directory in iter_target_directories(root):
        mixdesk = build_mixdesk(directory, backend=args.backend, target_sr=args.target_sr)
        if mixdesk is None:
            continue

        output_path = directory / "mixdesk.json"
        serializable = to_jsonable(mixdesk)
        if args.dry_run:
            print(f"would write {output_path}")
            print(json.dumps(serializable, indent=2))
        else:
            output_path.write_text(json.dumps(serializable, indent=2) + "\n", encoding="utf-8")
            print(f"wrote {output_path}")
        written += 1

    if written == 0:
        print(f"No stem folders found under {root}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
