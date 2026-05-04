from __future__ import annotations

from pathlib import Path
from typing import Any

import matplotlib.pyplot as plt
import numpy as np

from .alignment import peak_normalize
from .models import PhraseResult


def _plot_source(stems: dict[str, np.ndarray] | None) -> tuple[str, np.ndarray] | None:
    if not stems:
        return None
    for name in ("drums", "mix", "kick", "bass"):
        if name in stems:
            return name, stems[name]
    first = next(iter(stems))
    return first, stems[first]


def plot_phrase_result(
    result: PhraseResult,
    out: str | Path,
    stems: dict[str, np.ndarray] | None = None,
    sr: int | None = None,
    debug: dict[str, Any] | None = None,
) -> None:
    output = Path(out)
    output.parent.mkdir(parents=True, exist_ok=True)

    fig, axes = plt.subplots(2 if debug else 1, 1, figsize=(16, 6 if debug else 4), sharex=True)
    if not isinstance(axes, np.ndarray):
        axes = np.array([axes])
    ax = axes[0]

    source = _plot_source(stems)
    if source is not None and sr is not None:
        name, audio = source
        y = peak_normalize(audio)
        stride = max(1, len(y) // 20_000)
        times = np.arange(0, len(y), stride) / float(sr)
        ax.plot(times, y[::stride], color="#2bb7e5", linewidth=0.6, label=name)
    else:
        ax.axhline(0, color="#2bb7e5", linewidth=0.8)

    for beat in result.beats:
        ax.axvline(beat, color="#c8d8dc", alpha=0.16, linewidth=0.5)
    for bar in result.bars:
        ax.axvline(bar, color="#e4f2f4", alpha=0.35, linewidth=0.8)
    for boundary in result.boundaries:
        ax.axvline(boundary.time_s, color="#ff4f5f", alpha=0.9, linewidth=1.8)
        ax.text(boundary.time_s, 0.92, f"B{boundary.bar_index}", rotation=90, va="top", ha="right", fontsize=8)
    for segment in result.segments:
        center = (segment.start_s + segment.end_s) * 0.5
        ax.text(center, -0.92, segment.label, ha="center", va="bottom", fontsize=8)
    ax.set_title("EDM phrase detection")
    ax.set_ylabel("waveform")
    ax.legend(loc="upper right")

    if debug:
        score_ax = axes[1]
        scores = debug.get("candidate_boundary_scores", [])
        bars = result.bars[: len(scores)]
        if len(scores) and len(bars):
            score_ax.plot(bars, scores, color="#ffb02e", linewidth=1.2, label="boundary score")
        score_ax.set_ylabel("score")
        score_ax.set_ylim(0, 1.05)
        score_ax.legend(loc="upper right")

    axes[-1].set_xlabel("time (s)")
    ax.set_xlim(0, max(result.duration_s, 0.1))
    fig.tight_layout()
    fig.savefig(output, dpi=150)
    plt.close(fig)
