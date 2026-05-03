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

    return {
        "track_name": track_name,
        "duration": duration,
        "bpm": bpm,
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

    if shutil.which("ffprobe") is None:
        print("ffprobe was not found on PATH. Install FFmpeg or add ffprobe to PATH.", file=sys.stderr)
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
