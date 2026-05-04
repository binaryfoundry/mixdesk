from __future__ import annotations

from typing import Any

import numpy as np

from .models import PhraseSegment


def _mean(arrays: dict[str, np.ndarray], name: str, start: int, end: int) -> float:
    value = arrays.get(name)
    if value is None:
        return 0.0
    data = np.asarray(value)
    if data.ndim != 1:
        return 0.0
    selected = data[start:end]
    return float(np.mean(selected)) if len(selected) else 0.0


def label_segments(segments: list[PhraseSegment], features: dict[str, Any]) -> list[PhraseSegment]:
    arrays: dict[str, np.ndarray] = features.get("feature_arrays", {})
    num_bars = int(features.get("num_bars", 0))
    labelled: list[PhraseSegment] = []

    for segment in segments:
        start = max(0, segment.start_bar - 1)
        end = max(start + 1, segment.end_bar)
        kick = _mean(arrays, "kick_presence", start, end)
        drums = _mean(arrays, "drums_presence", start, end)
        bass = _mean(arrays, "bass_presence", start, end)
        harmonic = _mean(arrays, "harmonic_presence", start, end)
        vocal = _mean(arrays, "vocal_activity_ratio", start, end)
        entry = _mean(arrays, "instrumentation_entry_count", start, end)
        exit_ = _mean(arrays, "instrumentation_exit_count", start, end)
        drum_build = _mean(arrays, "drums_roll_or_build", start, end)
        near_start = segment.start_bar <= max(4, int(num_bars * 0.15))
        near_end = segment.end_bar >= max(1, int(num_bars * 0.85))

        label = "unknown"
        if near_start and vocal < 0.30 and (entry > 0.05 or bass < 0.45):
            label = "intro"
        elif near_end and exit_ > 0.05 and vocal < 0.45:
            label = "outro"
        elif kick < 0.35 and bass < 0.35 and (harmonic > 0.35 or vocal > 0.25):
            label = "breakdown"
        elif drum_build > 0.35 or entry > 0.12:
            label = "build"
        elif kick > 0.60 and drums > 0.55 and bass > 0.55:
            label = "drop"
        elif vocal > 0.50:
            label = "vocal"

        labelled.append(
            PhraseSegment(
                start_s=segment.start_s,
                end_s=segment.end_s,
                start_bar=segment.start_bar,
                end_bar=segment.end_bar,
                length_bars=segment.length_bars,
                confidence=segment.confidence,
                label=label,
            )
        )

    return labelled
