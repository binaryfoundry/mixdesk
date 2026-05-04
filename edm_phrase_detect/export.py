from __future__ import annotations

import json
from dataclasses import asdict, is_dataclass
from pathlib import Path
from typing import Any

import numpy as np

from .models import PhraseResult, StemInfo


def to_jsonable(value: Any) -> Any:
    if isinstance(value, np.ndarray):
        return to_jsonable(value.tolist())
    if isinstance(value, np.generic):
        return value.item()
    if is_dataclass(value):
        return to_jsonable(asdict(value))
    if isinstance(value, dict):
        return {str(key): to_jsonable(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [to_jsonable(item) for item in value]
    return value


def save_result(result: PhraseResult, path: str | Path) -> None:
    output = Path(path)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(to_jsonable(result.to_dict()), indent=2) + "\n", encoding="utf-8")


def build_debug_json(
    config: dict[str, Any],
    stem_info: dict[str, StemInfo],
    backend_debug: dict[str, Any],
    alignment_debug: dict[str, Any],
    bars: list[Any],
    features: dict[str, Any],
    group_scores: dict[str, np.ndarray],
    candidate_scores: np.ndarray,
    reasons: list[dict[str, float]],
    decoding_debug: Any,
) -> dict[str, Any]:
    return to_jsonable(
        {
            "config": config,
            "stem_metadata": stem_info,
            "selected_backend": backend_debug.get("selected_backend"),
            "backend": backend_debug,
            "alignment_shifts_ms": alignment_debug.get("alignment_shifts_ms", {}),
            "warnings": alignment_debug.get("warnings", []),
            "bar_table": bars,
            "features": features,
            "group_scores": group_scores,
            "candidate_boundary_scores": candidate_scores,
            "candidate_reasons": reasons,
            "dp_selected_boundary_indices": decoding_debug.selected_boundary_indices,
            "rejected_strong_candidates": decoding_debug.rejected_strong_candidates,
            "final_confidence_components": decoding_debug.confidence_components,
        }
    )


def save_debug(debug: dict[str, Any], path: str | Path) -> None:
    output = Path(path)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(to_jsonable(debug), indent=2) + "\n", encoding="utf-8")
