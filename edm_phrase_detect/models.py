from __future__ import annotations

from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any


@dataclass(slots=True)
class StemInfo:
    name: str
    path: str
    original_sample_rate: int
    target_sample_rate: int
    original_num_samples: int
    resampled_num_samples: int
    duration_s: float
    channels: int


@dataclass(slots=True)
class BeatEvent:
    time_s: float
    beat_in_bar: int


@dataclass(slots=True)
class Bar:
    index: int
    start_s: float
    end_s: float
    beat_times: list[float]


@dataclass(slots=True)
class PhraseBoundary:
    time_s: float
    bar_index: int
    confidence: float
    reason: dict[str, float]


@dataclass(slots=True)
class PhraseSegment:
    start_s: float
    end_s: float
    start_bar: int
    end_bar: int
    length_bars: int
    confidence: float
    label: str = "unknown"


@dataclass(slots=True)
class PhraseResult:
    bpm: float = 0.0
    beats: list[float] = field(default_factory=list)
    downbeats: list[float] = field(default_factory=list)
    bars: list[float] = field(default_factory=list)
    boundaries: list[PhraseBoundary] = field(default_factory=list)
    segments: list[PhraseSegment] = field(default_factory=list)
    duration_s: float = 0.0
    schema_version: str = "1.0.0"
    metadata: dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> dict[str, Any]:
        return {
            "schema_version": self.schema_version,
            "bpm": float(self.bpm),
            "duration_s": float(self.duration_s),
            "beats": [float(value) for value in self.beats],
            "downbeats": [float(value) for value in self.downbeats],
            "bars": [float(value) for value in self.bars],
            "boundaries": [asdict(boundary) for boundary in self.boundaries],
            "segments": [asdict(segment) for segment in self.segments],
            "metadata": self.metadata,
        }


def path_to_string(path: str | Path) -> str:
    return str(Path(path))
