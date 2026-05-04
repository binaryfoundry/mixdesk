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

    return {
        "track_name": track_name,
        "duration": duration,
        "bpm": bpm,
        "beat_grid": beat_grid,
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
