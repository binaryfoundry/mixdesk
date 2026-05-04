#!/usr/bin/env python3
"""Generate mixdesk.json files for track stem folders."""

from __future__ import annotations

import argparse
import array
import json
import math
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any

from PIL import Image, ImageDraw


STEM_MATCHES: tuple[tuple[str, tuple[tuple[str, ...], ...]], ...] = (
    ("no_bass", (("no", "bass"), ("nobass",))),
    ("no_drum", (("no", "drum"), ("no", "drums"), ("nodrum",), ("nodrums",))),
    ("no_vocals", (("no", "vocals"), ("no", "vocal"), ("novocals",), ("novocal",))),
    ("instrumental", (("instrumental",), ("instrument",))),
    ("vocals", (("vocals",), ("vocal",))),
    ("drum", (("drum",), ("drums",))),
    ("bass", (("bass",),)),
)

ENTRY_STEMS = ("drum", "bass", "vocals", "no_bass", "no_drum", "no_vocals", "instrumental")

AUDIO_EXTENSIONS = {
    ".aac",
    ".aif",
    ".aiff",
    ".alac",
    ".flac",
    ".m4a",
    ".mp3",
    ".ogg",
    ".opus",
    ".wav",
    ".wma",
}

BPM_TAGS = ("BPM", "TBPM", "bpm", "tmpo")
KEY_TAGS = ("TKEY", "INITIAL_KEY", "initialkey", "initial_key", "KEY", "key")
TITLE_TAGS = ("title", "TITLE", "TIT2")
BEAT_ANALYSIS_SAMPLE_RATE = 11_025
LOW_PASS_FREQUENCY_HZ = 240.0
DEFAULT_BEATS_PER_BAR = 4
MINIMUM_NUMBER_OF_PEAKS = 30
BARS_PER_PHRASE = 8
PHRASE_ANALYSIS_IMAGE_NAME = "mixdesk_phrase_analysis.png"


def run_ffprobe(path: Path) -> dict[str, Any]:
    command = [
        "ffprobe",
        "-v",
        "error",
        "-show_entries",
        "format=duration:format_tags",
        "-of",
        "json",
        str(path),
    ]
    result = subprocess.run(command, check=True, capture_output=True, text=True)
    data: dict[str, Any] = json.loads(result.stdout or "{}")
    format_data = data.get("format", {})
    return format_data if isinstance(format_data, dict) else {}


def decode_mono_f32(path: Path, sample_rate: int = BEAT_ANALYSIS_SAMPLE_RATE) -> tuple[array.array, int]:
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
    samples = array.array("f")
    samples.frombytes(result.stdout)
    if sys.byteorder != "little":
        samples.byteswap()
    return samples, sample_rate


def get_tags(format_data: dict[str, Any]) -> dict[str, str]:
    tags = format_data.get("tags", {})
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
    if value is None:
        return None

    try:
        seconds = float(value)
    except (TypeError, ValueError):
        return None

    return int(seconds) if seconds.is_integer() else round(seconds, 3)


class BiquadLowPass:
    def __init__(self, sample_rate: float, frequency_hz: float, q: float = 1.0) -> None:
        omega = 2.0 * math.pi * frequency_hz / sample_rate
        sin_omega = math.sin(omega)
        cos_omega = math.cos(omega)
        alpha = sin_omega / (2.0 * q)

        raw_b0 = (1.0 - cos_omega) * 0.5
        raw_b1 = 1.0 - cos_omega
        raw_b2 = (1.0 - cos_omega) * 0.5
        raw_a0 = 1.0 + alpha
        raw_a1 = -2.0 * cos_omega
        raw_a2 = 1.0 - alpha

        self.b0 = raw_b0 / raw_a0
        self.b1 = raw_b1 / raw_a0
        self.b2 = raw_b2 / raw_a0
        self.a1 = raw_a1 / raw_a0
        self.a2 = raw_a2 / raw_a0
        self.x1 = 0.0
        self.x2 = 0.0
        self.y1 = 0.0
        self.y2 = 0.0

    def process(self, value: float) -> float:
        output = (self.b0 * value) + (self.b1 * self.x1) + (self.b2 * self.x2) - (self.a1 * self.y1) - (self.a2 * self.y2)
        self.x2 = self.x1
        self.x1 = value
        self.y2 = self.y1
        self.y1 = output
        return output


def get_peaks_at_threshold(channel_data: list[float], threshold: float, sample_rate: int) -> list[int]:
    peaks: list[int] = []
    last_value_was_above_threshold = False
    skip_samples = max(1, int(sample_rate / 4.0) - 1)
    index = 0

    while index < len(channel_data):
        if channel_data[index] > threshold:
            last_value_was_above_threshold = True
        elif last_value_was_above_threshold:
            last_value_was_above_threshold = False
            peaks.append(index - 1)
            index += skip_samples
        index += 1

    if last_value_was_above_threshold:
        peaks.append(len(channel_data) - 1)

    return peaks


def count_intervals_between_nearby_peaks(peaks: list[int]) -> dict[int, list[int]]:
    interval_buckets: dict[int, list[int]] = {}

    for peak_index, peak in enumerate(peaks):
        length = min(len(peaks) - peak_index, 10)
        for nearby_index in range(1, length):
            interval = peaks[peak_index + nearby_index] - peak
            interval_buckets.setdefault(interval, []).append(peak)

    return interval_buckets


def group_neighbors_by_tempo(
    interval_buckets: dict[int, list[int]],
    sample_rate: int,
    min_tempo: float = 90.0,
    max_tempo: float = 180.0,
) -> list[dict[str, Any]]:
    tempo_buckets: list[dict[str, Any]] = []

    for interval, peaks in interval_buckets.items():
        if interval <= 0:
            continue

        theoretical_tempo = 60.0 / (float(interval) / float(sample_rate))
        while theoretical_tempo < min_tempo:
            theoretical_tempo *= 2.0
        while theoretical_tempo > max_tempo and max_tempo > 0.0:
            theoretical_tempo /= 2.0
        if theoretical_tempo < min_tempo:
            continue

        found_tempo = False
        score = float(len(peaks))
        for tempo_bucket in tempo_buckets:
            if tempo_bucket["tempo"] == theoretical_tempo:
                tempo_bucket["score"] += float(len(peaks))
                tempo_bucket["peaks"].extend(peaks)
                found_tempo = True

            if theoretical_tempo - 0.5 < tempo_bucket["tempo"] < theoretical_tempo + 0.5:
                tempo_difference = abs(tempo_bucket["tempo"] - theoretical_tempo) * 2.0
                score += (1.0 - tempo_difference) * float(len(tempo_bucket["peaks"]))
                tempo_bucket["score"] += (1.0 - tempo_difference) * float(len(peaks))

        if not found_tempo:
            tempo_buckets.append({"tempo": theoretical_tempo, "score": score, "peaks": list(peaks)})

    return sorted(tempo_buckets, key=lambda bucket: bucket["score"], reverse=True)


def make_beat_times(offset_seconds: float, seconds_per_beat: float, duration_seconds: float) -> list[float]:
    beat_times: list[float] = []
    if seconds_per_beat <= 0.0 or duration_seconds <= 0.0:
        return beat_times

    beat_time = offset_seconds
    while beat_time <= duration_seconds:
        if beat_time >= 0.0:
            beat_times.append(round(beat_time, 6))
        beat_time += seconds_per_beat

    return beat_times


def average_sample_energy(samples: array.array | None, sample_rate: int, start_seconds: float, end_seconds: float) -> float:
    if samples is None or sample_rate <= 0 or end_seconds <= start_seconds:
        return 0.0

    start_index = min(len(samples), max(0, int(math.floor(start_seconds * sample_rate))))
    end_index = min(len(samples), max(0, int(math.ceil(end_seconds * sample_rate))))
    if end_index <= start_index:
        return 0.0

    total = 0.0
    for index in range(start_index, end_index):
        value = float(samples[index])
        total += value * value

    return math.sqrt(total / float(end_index - start_index))


def peak_for_pixel(samples: array.array, start_index: int, end_index: int) -> float:
    if end_index <= start_index or not samples:
        return 0.0

    end_index = min(end_index, len(samples))
    start_index = max(0, min(start_index, end_index))
    peak = 0.0
    step = max(1, (end_index - start_index) // 256)
    for index in range(start_index, end_index, step):
        peak = max(peak, abs(float(samples[index])))

    return min(1.0, peak)


def render_phrase_analysis_image(
    output_path: Path,
    primary_track: Path,
    stem_files: dict[str, Path],
    beat_grid: dict[str, Any] | None,
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
    decoded: list[tuple[str, array.array, int, tuple[int, int, int]]] = []
    for label, path, color in track_rows:
        if path is None or not path.exists():
            continue
        samples, sample_rate = decode_mono_f32(path)
        if samples:
            decoded.append((label, samples, sample_rate, color))

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
    seconds_per_beat = float(beat_grid.get("seconds_per_beat") or 0.0)
    first_beat_offset = float(beat_grid.get("first_beat_offset_seconds") or 0.0)
    beats_per_bar = int(beat_grid.get("beats_per_bar") or DEFAULT_BEATS_PER_BAR)

    draw.rectangle((0, 0, width, height), fill=(12, 28, 34, 255))
    draw.line((left, top - 12, right, top - 12), fill=(65, 205, 230, 210), width=2)
    draw.line((left, bottom + 10, right, bottom + 10), fill=(65, 205, 230, 210), width=2)

    if duration_seconds > 0.0 and seconds_per_beat > 0.0:
        beat = 0
        beat_time = first_beat_offset
        while beat_time <= duration_seconds:
            x = left + int((beat_time / duration_seconds) * timeline_width)
            if left <= x <= right:
                is_bar = beat % max(1, beats_per_bar) == 0
                color = (220, 235, 238, 175) if is_bar else (115, 145, 150, 80)
                draw.line((x, top - 4, x, bottom + 4), fill=color, width=2 if is_bar else 1)
            beat += 1
            beat_time += seconds_per_beat

    for row_index, (label, samples, sample_rate, color) in enumerate(decoded):
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
            y_top = center - int(peak * half_height)
            y_bottom = center + int(peak * half_height)
            points.append((x, y_top))
            lower_points.append((x, y_bottom))

        polygon = points + list(reversed(lower_points))
        draw.polygon(polygon, fill=(*color, 115))
        draw.line(points, fill=(*color, 230), width=1)
        draw.line(lower_points, fill=(*color, 230), width=1)
        draw.text((6, row_top + 6), label, fill=(210, 225, 228, 220))

    image.save(output_path)


def normalize_bar_member(bars: list[dict[str, float]], name: str) -> None:
    maximum = max((bar[name] for bar in bars), default=0.0)
    if maximum <= 0.0:
        return

    for bar in bars:
        bar[name] = max(0.0, min(1.0, bar[name] / maximum))


def average_bar_member(bars: list[dict[str, float]], start_bar: int, length_bars: int, name: str) -> float:
    if not bars or length_bars <= 0:
        return 0.0

    start = max(0, min(len(bars), start_bar))
    end = max(0, min(len(bars), start_bar + length_bars))
    if end <= start:
        return 0.0

    return sum(bar[name] for bar in bars[start:end]) / float(end - start)


def mean_window_member(windows: list[dict[str, float | int]], name: str) -> float:
    if not windows:
        return 0.0

    return sum(float(window[name]) for window in windows) / float(len(windows))


def contiguous_segments(flags: list[bool], beats_per_bar: int, label: str) -> list[dict[str, Any]]:
    segments: list[dict[str, Any]] = []
    start: int | None = None

    for index, flag in enumerate(flags + [False]):
        if flag and start is None:
            start = index
        elif not flag and start is not None:
            length_bars = index - start
            segments.append(
                {
                    "label": label,
                    "start_bar": start,
                    "length_bars": length_bars,
                    "beat_index": start * beats_per_bar,
                    "beat_count": length_bars * beats_per_bar,
                }
            )
            start = None

    return segments


def bar_boundaries(bar_count: int, beats_per_bar: int) -> list[int]:
    boundaries = [bar * beats_per_bar for bar in range(0, bar_count + 1)]
    final_boundary = bar_count * beats_per_bar
    if not boundaries or boundaries[-1] != final_boundary:
        boundaries.append(final_boundary)
    return boundaries


def phrase_grid_boundaries(bar_count: int, beats_per_bar: int, bars_per_phrase: int) -> list[int]:
    boundaries = [bar * beats_per_bar for bar in range(0, bar_count + 1, bars_per_phrase)]
    final_boundary = bar_count * beats_per_bar
    if not boundaries or boundaries[-1] != final_boundary:
        boundaries.append(final_boundary)
    return boundaries


def significant_change_candidates(
    bar_features: list[dict[str, Any]],
    beats_per_bar: int,
) -> list[dict[str, Any]]:
    candidates: list[dict[str, Any]] = []
    if len(bar_features) < 2:
        return candidates

    for index in range(1, len(bar_features)):
        previous = bar_features[index - 1]
        current = bar_features[index]
        next_bar = bar_features[index + 1] if index + 1 < len(bar_features) else current

        deltas = {
            "drums": float(current["drums"]) - float(previous["drums"]),
            "bass": float(current["bass"]) - float(previous["bass"]),
            "vocal": float(current["vocal"]) - float(previous["vocal"]),
            "total": float(current["total"]) - float(previous["total"]),
        }
        sustained_total_change = abs(float(next_bar["total"]) - float(previous["total"]))
        sustained_vocal_change = bool(previous["vocal_active"]) != bool(current["vocal_active"])
        dropout_resolves = bool(previous["dropout_or_silence"]) and not bool(current["dropout_or_silence"])
        dropout_starts = not bool(previous["dropout_or_silence"]) and bool(current["dropout_or_silence"])
        bass_or_drum_return = (deltas["bass"] > 0.30 or deltas["drums"] > 0.30) and float(current["total"]) > 0.30

        score = (
            abs(deltas["total"]) * 1.25
            + abs(deltas["vocal"]) * 1.10
            + abs(deltas["bass"]) * 0.75
            + abs(deltas["drums"]) * 0.55
            + sustained_total_change * 0.75
            + (0.45 if sustained_vocal_change else 0.0)
            + (0.40 if dropout_resolves else 0.0)
            + (0.22 if dropout_starts else 0.0)
            + (0.30 if bass_or_drum_return else 0.0)
        )

        reasons: list[str] = []
        if sustained_vocal_change:
            reasons.append("vocal_state_changed")
        if dropout_resolves:
            reasons.append("dropout_resolved")
        if dropout_starts:
            reasons.append("dropout_started")
        if bass_or_drum_return:
            reasons.append("drum_or_bass_return")
        if abs(deltas["total"]) > 0.18:
            reasons.append("large_energy_change")

        if score >= 0.58 or reasons:
            candidates.append(
                {
                    "bar": int(current["bar"]),
                    "beat_index": int(current["beat_index"]),
                    "score": round(score, 4),
                    "reasons": reasons,
                    "before": {
                        "drums": previous["drums"],
                        "bass": previous["bass"],
                        "vocal": previous["vocal"],
                        "total": previous["total"],
                        "vocal_active": previous["vocal_active"],
                        "dropout_or_silence": previous["dropout_or_silence"],
                    },
                    "after": {
                        "drums": current["drums"],
                        "bass": current["bass"],
                        "vocal": current["vocal"],
                        "total": current["total"],
                        "vocal_active": current["vocal_active"],
                        "dropout_or_silence": current["dropout_or_silence"],
                    },
                }
            )

    return sorted(candidates, key=lambda item: item["score"], reverse=True)[:32]


def extract_phrase_features(stem_files: dict[str, Path], beat_grid: dict[str, Any] | None) -> dict[str, Any]:
    if beat_grid is None:
        return {"status": "missing_beat_grid", "bars_per_phrase": BARS_PER_PHRASE, "windows": []}

    seconds_per_beat = float(beat_grid.get("seconds_per_beat") or 0.0)
    beats_per_bar = int(beat_grid.get("beats_per_bar") or DEFAULT_BEATS_PER_BAR)
    first_beat_offset = float(beat_grid.get("first_beat_offset_seconds") or 0.0)
    duration_seconds = float(beat_grid.get("duration_seconds") or 0.0)
    seconds_per_bar = seconds_per_beat * float(max(1, beats_per_bar))
    if seconds_per_bar <= 0.0 or duration_seconds <= 0.0:
        return {"status": "invalid_beat_grid", "bars_per_phrase": BARS_PER_PHRASE, "windows": []}

    decoded: dict[str, array.array | None] = {"drum": None, "bass": None, "vocals": None}
    sample_rate = BEAT_ANALYSIS_SAMPLE_RATE
    for stem_name in decoded:
        path = stem_files.get(stem_name)
        if path is None:
            continue
        decoded[stem_name], sample_rate = decode_mono_f32(path)

    bar_count = max(1, int(math.floor((duration_seconds - first_beat_offset) / seconds_per_bar)))
    bars: list[dict[str, float]] = []
    for bar_index in range(bar_count):
        start_seconds = first_beat_offset + (float(bar_index) * seconds_per_bar)
        end_seconds = min(duration_seconds, start_seconds + seconds_per_bar)
        bars.append(
            {
                "drums": average_sample_energy(decoded["drum"], sample_rate, start_seconds, end_seconds),
                "bass": average_sample_energy(decoded["bass"], sample_rate, start_seconds, end_seconds),
                "vocal": average_sample_energy(decoded["vocals"], sample_rate, start_seconds, end_seconds),
            }
        )

    for name in ("drums", "bass", "vocal"):
        normalize_bar_member(bars, name)

    for bar in bars:
        bar["total"] = (bar["drums"] * 0.38) + (bar["bass"] * 0.38) + (bar["vocal"] * 0.24)

    mean_total = sum(bar["total"] for bar in bars) / float(len(bars)) if bars else 0.0
    mean_vocal = sum(bar["vocal"] for bar in bars) / float(len(bars)) if bars else 0.0
    vocal_on_threshold = max(0.20, mean_vocal * 1.20)
    vocal_keep_threshold = max(0.12, vocal_on_threshold * 0.58)
    dropout_threshold = max(0.12, mean_total * 0.45)
    vocal_flags: list[bool] = []
    dropout_flags: list[bool] = []
    vocal_is_active = False

    bar_features: list[dict[str, Any]] = []
    for bar_index, bar in enumerate(bars):
        previous_total = bars[bar_index - 1]["total"] if bar_index > 0 else bar["total"]
        next_total = bars[bar_index + 1]["total"] if bar_index + 1 < len(bars) else bar["total"]
        vocal_is_active = bar["vocal"] >= (vocal_keep_threshold if vocal_is_active else vocal_on_threshold)
        is_dropout = (
            bar["total"] <= dropout_threshold
            or (bar["drums"] < 0.18 and bar["bass"] < 0.18 and bar["vocal"] < 0.18)
            or (previous_total - bar["total"] > 0.24 and next_total < previous_total - 0.18)
        )
        vocal_flags.append(vocal_is_active)
        dropout_flags.append(is_dropout)
        bar_features.append(
            {
                "bar": bar_index,
                "beat_index": bar_index * beats_per_bar,
                "beat_count": beats_per_bar,
                "drums": round(bar["drums"], 4),
                "bass": round(bar["bass"], 4),
                "vocal": round(bar["vocal"], 4),
                "total": round(bar["total"], 4),
                "delta_from_previous": round(bar["total"] - previous_total, 4),
                "vocal_active": vocal_is_active,
                "dropout_or_silence": is_dropout,
            }
        )

    vocal_segments = contiguous_segments(vocal_flags, beats_per_bar, "vocal_active")
    dropout_events = contiguous_segments(dropout_flags, beats_per_bar, "dropout_or_silence")

    bars_per_phrase = max(4, BARS_PER_PHRASE)
    legal_boundaries = bar_boundaries(bar_count, beats_per_bar)
    phrase_boundaries = phrase_grid_boundaries(bar_count, beats_per_bar, bars_per_phrase)
    windows: list[dict[str, Any]] = []
    for start_bar in range(0, bar_count, bars_per_phrase):
        length_bars = min(bars_per_phrase, bar_count - start_bar)
        if length_bars < 4:
            break

        drums = average_bar_member(bars, start_bar, length_bars, "drums")
        bass = average_bar_member(bars, start_bar, length_bars, "bass")
        vocal = average_bar_member(bars, start_bar, length_bars, "vocal")
        total = average_bar_member(bars, start_bar, length_bars, "total")
        window = {
            "beat_index": start_bar * beats_per_bar,
            "beat_count": length_bars * beats_per_bar,
            "start_bar": start_bar,
            "length_bars": length_bars,
            "drums": round(drums, 4),
            "bass": round(bass, 4),
            "vocal": round(vocal, 4),
            "total": round(total, 4),
            "previous_total": round(average_bar_member(bars, start_bar - bars_per_phrase, bars_per_phrase, "total"), 4),
            "next_total": round(average_bar_member(bars, start_bar + bars_per_phrase, bars_per_phrase, "total"), 4),
            "novelty": 0.0,
        }
        if windows:
            previous = windows[-1]
            window["novelty"] = (
                abs(float(window["drums"]) - float(previous["drums"])) * 0.32
                + abs(float(window["bass"]) - float(previous["bass"])) * 0.38
                + abs(float(window["vocal"]) - float(previous["vocal"])) * 0.30
            )
            window["novelty"] = round(float(window["novelty"]), 4)
        windows.append(window)

    change_candidates = significant_change_candidates(bar_features, beats_per_bar)
    phrase_candidates = [
        {
            "index": index,
            "beat_index": int(window["beat_index"]),
            "beat_count": int(window["beat_count"]),
            "start_bar": int(window["start_bar"]),
            "length_bars": int(window["length_bars"]),
            "drums": window["drums"],
            "bass": window["bass"],
            "vocal": window["vocal"],
            "total": window["total"],
            "novelty": window["novelty"],
        }
        for index, window in enumerate(windows)
    ]

    return {
        "status": "features_ready",
        "bars_per_phrase": bars_per_phrase,
        "beats_per_bar": beats_per_bar,
        "window_beat_count": bars_per_phrase * beats_per_bar,
        "feature_notes": (
            "Values are normalized RMS energy. windows are coarse 8-bar candidates; bar_features are 1-bar detail. "
            "A phrase is a coherent electronic music arrangement section; boundaries happen where musical roles change significantly. "
            "change_candidates are diagnostic only; generated phrases stay on the naive 8-bar grid."
        ),
        "vocal_thresholds": {
            "on": round(vocal_on_threshold, 4),
            "keep": round(vocal_keep_threshold, 4),
        },
        "dropout_threshold": round(dropout_threshold, 4),
        "bar_features": bar_features,
        "vocal_segments": vocal_segments,
        "dropout_events": dropout_events,
        "legal_boundaries": legal_boundaries,
        "phrase_boundaries": phrase_boundaries,
        "phrase_candidates": phrase_candidates,
        "change_candidates": change_candidates,
        "windows": windows,
    }


def is_core_energy(window: dict[str, Any], mean_total: float) -> bool:
    return (
        float(window.get("drums", 0.0)) > 0.48
        and float(window.get("bass", 0.0)) > 0.44
        and float(window.get("total", 0.0)) > max(0.50, mean_total + 0.04)
    )


def has_low_to_high_context(windows: list[dict[str, Any]], index: int, mean_total: float) -> bool:
    if index == 0:
        return False

    window = windows[index]
    previous = windows[index - 1]
    previous2 = windows[index - 2 if index >= 2 else index - 1]
    previous_was_sparse = (
        float(previous.get("total", 0.0)) < mean_total - 0.08
        or float(previous.get("drums", 0.0)) < 0.40
        or float(previous.get("bass", 0.0)) < 0.38
    )
    bass_return = (
        float(window.get("bass", 0.0)) > 0.46
        and (
            float(window.get("bass", 0.0)) > float(previous.get("bass", 0.0)) + 0.14
            or float(previous.get("bass", 0.0)) < 0.36
        )
    )
    drum_return = (
        float(window.get("drums", 0.0)) > 0.48
        and (
            float(window.get("drums", 0.0)) > float(previous.get("drums", 0.0)) + 0.12
            or float(previous.get("drums", 0.0)) < 0.38
        )
    )
    rising_into_here = (
        float(previous.get("total", 0.0)) > float(previous2.get("total", 0.0)) + 0.04
        and float(window.get("total", 0.0)) > float(previous.get("total", 0.0)) + 0.02
    )

    return (
        previous_was_sparse and (bass_return or drum_return or float(window.get("novelty", 0.0)) > 0.12)
    ) or (
        (bass_return or drum_return) and float(window.get("novelty", 0.0)) > 0.10
    ) or rising_into_here


def is_drop_candidate(
    windows: list[dict[str, Any]],
    index: int,
    mean_total: float,
    intro_window_count: int,
) -> bool:
    if index < intro_window_count or index >= len(windows):
        return False

    window = windows[index]
    previous = windows[index - 1]
    stem_return = (
        float(window.get("bass", 0.0)) > 0.46
        and (
            float(window.get("bass", 0.0)) > float(previous.get("bass", 0.0)) + 0.12
            or float(previous.get("bass", 0.0)) < 0.36
        )
    ) or (
        float(window.get("drums", 0.0)) > 0.48
        and (
            float(window.get("drums", 0.0)) > float(previous.get("drums", 0.0)) + 0.10
            or float(previous.get("drums", 0.0)) < 0.38
        )
    )

    return is_core_energy(window, mean_total) and stem_return and has_low_to_high_context(windows, index, mean_total)


def classify_windows(windows: list[dict[str, Any]]) -> list[str]:
    labels = ["groove"] * len(windows)
    if not windows:
        return labels

    mean_total = mean_window_member(windows, "total")
    intro_window_count = min(len(windows), 4 if len(windows) >= 6 else 1)
    outro_window_count = min(len(windows), 2 if len(windows) >= 8 else 1)
    drop_candidates = [False] * len(windows)
    previous_drop_index: int | None = None

    for index in range(len(windows)):
        far_enough_from_previous_drop = previous_drop_index is None or index > previous_drop_index + 1
        drop_candidates[index] = (
            far_enough_from_previous_drop
            and is_drop_candidate(windows, index, mean_total, intro_window_count)
        )

        if drop_candidates[index]:
            previous_drop_index = index

    for index, window in enumerate(windows):
        is_intro = index < intro_window_count or (
            index < intro_window_count + 2
            and float(window.get("vocal", 0.0)) < 0.28
            and not drop_candidates[index]
            and float(window.get("novelty", 0.0)) < 0.18
        )
        is_outro = index + outro_window_count >= len(windows) and float(window.get("vocal", 0.0)) < 0.38
        next_is_drop = index + 1 < len(windows) and drop_candidates[index + 1]
        next2_is_drop = index + 2 < len(windows) and drop_candidates[index + 2]
        is_rising = float(window.get("next_total", 0.0)) > float(window.get("total", 0.0)) + 0.08
        fell_from_previous = float(window.get("previous_total", 0.0)) > float(window.get("total", 0.0)) + 0.12

        if is_intro:
            labels[index] = "intro"
        elif is_outro:
            labels[index] = "outro"
        elif drop_candidates[index]:
            labels[index] = "drop"
        elif next_is_drop:
            labels[index] = "build"
        elif next2_is_drop and (
            float(window.get("drums", 0.0)) < 0.46
            or float(window.get("bass", 0.0)) < 0.38
            or float(window.get("vocal", 0.0)) > 0.34
        ):
            labels[index] = "breakdown"
        elif (
            float(window.get("drums", 0.0)) < 0.34
            or float(window.get("bass", 0.0)) < 0.30
        ) and (float(window.get("vocal", 0.0)) > 0.30 or fell_from_previous):
            labels[index] = "breakdown"
        elif is_rising and float(window.get("drums", 0.0)) > 0.34:
            labels[index] = "build"
        elif float(window.get("total", 0.0)) < max(0.20, mean_total - 0.22):
            labels[index] = "fx"
        elif (
            float(window.get("drums", 0.0)) > 0.52
            and float(window.get("bass", 0.0)) < 0.34
            and float(window.get("vocal", 0.0)) < 0.34
        ):
            labels[index] = "loop"
        else:
            labels[index] = "groove"

    return labels


def combined_energy(window: dict[str, Any]) -> float:
    energy = (
        float(window.get("drums", 0.0)) * 0.38
        + float(window.get("bass", 0.0)) * 0.38
        + float(window.get("vocal", 0.0)) * 0.24
    )
    return max(0.0, min(1.0, energy))


def build_naive_phrases(phrase_analysis: dict[str, Any]) -> list[dict[str, Any]]:
    windows = [
        window for window in phrase_analysis.get("windows", [])
        if isinstance(window, dict)
    ]
    labels = classify_windows(windows)
    phrases: list[dict[str, Any]] = []

    for index, window in enumerate(windows):
        phrase_type = labels[index] if index < len(labels) else "groove"
        phrases.append(
            {
                "type": phrase_type,
                "beat_index": int(window.get("beat_index", 0)),
                "beat_count": int(window.get("beat_count", 0)),
                "energy": round(combined_energy(window), 4),
                "has_drums": float(window.get("drums", 0.0)) > 0.32,
                "has_bass": float(window.get("bass", 0.0)) > 0.36,
                "has_vocal": float(window.get("vocal", 0.0)) > 0.28,
                "has_melody": phrase_type in {"build", "breakdown", "drop"},
            }
        )

    return phrases


def analyze_beats(path: Path, duration_seconds: float | None) -> dict[str, Any] | None:
    samples, sample_rate = decode_mono_f32(path)
    if not samples:
        return None

    low_pass = BiquadLowPass(float(sample_rate), LOW_PASS_FREQUENCY_HZ)
    channel_data = [low_pass.process(float(sample)) for sample in samples]
    maximum_value = max(channel_data, default=0.0)
    if maximum_value <= 0.25:
        return None

    minimum_threshold = maximum_value * 0.3
    threshold = maximum_value - (maximum_value * 0.05)
    peaks: list[int] = []
    while len(peaks) < MINIMUM_NUMBER_OF_PEAKS and threshold >= minimum_threshold:
        peaks = get_peaks_at_threshold(channel_data, threshold, sample_rate)
        threshold -= maximum_value * 0.05

    tempo_buckets = group_neighbors_by_tempo(count_intervals_between_nearby_peaks(peaks), sample_rate)
    if not tempo_buckets:
        return None

    best_bucket = tempo_buckets[0]
    tempo = max(1.0, float(best_bucket["tempo"]))
    seconds_per_beat = 60.0 / tempo
    sorted_peaks = sorted(best_bucket["peaks"])
    offset_seconds = (float(sorted_peaks[0]) / float(sample_rate)) if sorted_peaks else 0.0
    while offset_seconds > seconds_per_beat:
        offset_seconds -= seconds_per_beat

    if duration_seconds is None or duration_seconds <= 0.0:
        duration_seconds = float(len(samples)) / float(sample_rate)

    return {
        "tempo": round(tempo, 6),
        "bpm": max(1, round(tempo)),
        "first_beat_offset_seconds": round(offset_seconds, 6),
        "seconds_per_beat": round(seconds_per_beat, 9),
        "beats_per_bar": DEFAULT_BEATS_PER_BAR,
        "duration_seconds": round(duration_seconds, 3),
        "beat_times_seconds": make_beat_times(offset_seconds, seconds_per_beat, duration_seconds),
    }


def file_metadata(format_data: dict[str, Any]) -> dict[str, Any]:
    tags = get_tags(format_data)
    metadata: dict[str, Any] = {"tags": tags}
    duration = normalize_duration(format_data.get("duration"))
    if duration is not None:
        metadata["duration"] = duration
    return metadata


def filename_tokens(path: Path) -> tuple[str, ...]:
    normalized = path.stem.lower().replace("-", "_")
    return tuple(token for token in normalized.split("_") if token)


def contains_token_phrase(tokens: tuple[str, ...], phrase: tuple[str, ...]) -> bool:
    if not phrase or len(phrase) > len(tokens):
        return False

    for start_index in range(0, len(tokens) - len(phrase) + 1):
        if tokens[start_index : start_index + len(phrase)] == phrase:
            return True

    return False


def classify_stem(path: Path) -> str | None:
    tokens = filename_tokens(path)
    for stem_name, patterns in STEM_MATCHES:
        if any(contains_token_phrase(tokens, pattern) for pattern in patterns):
            return stem_name
    return None


def pick_primary_track_file(directory: Path) -> Path | None:
    audio_files = [
        path
        for path in sorted(directory.iterdir())
        if path.is_file() and path.suffix.lower() in AUDIO_EXTENSIONS
    ]
    for path in audio_files:
        if classify_stem(path) is None:
            return path
    return None


def pick_stem_files(directory: Path) -> dict[str, Path]:
    stems: dict[str, Path] = {}
    for path in sorted(directory.iterdir()):
        if not path.is_file() or path.suffix.lower() not in AUDIO_EXTENSIONS:
            continue

        stem_name = classify_stem(path)
        if stem_name and stem_name not in stems:
            stems[stem_name] = path

    return stems


def build_mixdesk(directory: Path) -> dict[str, Any] | None:
    stem_files = pick_stem_files(directory)
    if not stem_files:
        return None

    primary_track = pick_primary_track_file(directory)
    if primary_track is None:
        return None

    metadata: dict[str, dict[str, Any]] = {}
    entries: dict[str, dict[str, Any]] = {}
    primary_metadata = run_ffprobe(primary_track) if primary_track else {}
    primary_tags = get_tags(primary_metadata)
    track_name = get_first_tag(primary_tags, TITLE_TAGS) or directory.name
    duration = normalize_duration(primary_metadata.get("duration"))
    original = {
        "file": primary_track.name if primary_track else None,
        "metadata": file_metadata(primary_metadata),
    }

    for stem_name in ENTRY_STEMS:
        path = stem_files.get(stem_name)
        if path is None:
            entries[stem_name] = {"file": None, "key": None}
            continue

        format_data = run_ffprobe(path)
        tags = get_tags(format_data)
        metadata[stem_name] = format_data
        entries[stem_name] = {
            "file": path.name,
            "key": get_first_tag(tags, KEY_TAGS),
            "duration": normalize_duration(format_data.get("duration")),
        }

    drum_metadata = metadata.get("drum", {})
    drum_tags = get_tags(drum_metadata)
    bpm = normalize_bpm(get_first_tag(drum_tags, BPM_TAGS))
    if duration is None:
        duration = normalize_duration(drum_metadata.get("duration"))
    beat_grid = analyze_beats(stem_files["drum"], float(duration) if duration is not None else None) if "drum" in stem_files else None
    if bpm is None and beat_grid is not None:
        bpm = beat_grid["tempo"]
    phrase_analysis = extract_phrase_features(stem_files, beat_grid)
    phrase_image_path = directory / PHRASE_ANALYSIS_IMAGE_NAME
    render_phrase_analysis_image(phrase_image_path, primary_track, stem_files, beat_grid)
    if phrase_image_path.exists():
        phrase_analysis["image"] = phrase_image_path.name
    phrases = build_naive_phrases(phrase_analysis)
    if phrase_analysis.get("status") == "features_ready":
        phrase_analysis["status"] = "naive_python"
    phrase_analysis["method"] = "ported_cpp_phrase_analyzer"

    return {
        "track_name": track_name,
        "duration": duration,
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
    parser = argparse.ArgumentParser(
        description="Create mixdesk.json in each subdirectory that contains audio stems."
    )
    parser.add_argument(
        "root",
        nargs="?",
        default=".",
        type=Path,
        help="Root folder to scan. Defaults to the current directory.",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print files that would be written without changing them.",
    )
    args = parser.parse_args()

    if shutil.which("ffprobe") is None or shutil.which("ffmpeg") is None:
        print("ffmpeg and ffprobe are required on PATH. Install FFmpeg or add it to PATH.", file=sys.stderr)
        return 1

    root = args.root.resolve()
    written = 0

    for directory in iter_target_directories(root):
        mixdesk = build_mixdesk(directory)
        if mixdesk is None:
            continue

        output_path = directory / "mixdesk.json"
        if args.dry_run:
            print(f"would write {output_path}")
            print(json.dumps(mixdesk, indent=2))
        else:
            output_path.write_text(json.dumps(mixdesk, indent=2) + "\n", encoding="utf-8")
            print(f"wrote {output_path}")
        written += 1

    if written == 0:
        print(f"No stem folders found under {root}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
