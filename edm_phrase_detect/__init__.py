"""Offline phrase detection for 4/4 electronic dance music."""

from .api import (
    build_bar_grid,
    build_segments,
    decode_phrase_boundaries,
    detect_beats_downbeats,
    detect_phrases,
    extract_barwise_features,
    load_stems,
    plot_phrase_result,
    save_phrase_json,
    score_phrase_boundaries,
    validate_alignment,
)
from .config import Config
from .evaluation import evaluate_phrase_boundaries_beats, evaluate_phrase_boundaries_seconds
from .models import Bar, BeatEvent, PhraseBoundary, PhraseResult, PhraseSegment, StemInfo

__all__ = [
    "Bar",
    "BeatEvent",
    "Config",
    "PhraseBoundary",
    "PhraseResult",
    "PhraseSegment",
    "StemInfo",
    "build_bar_grid",
    "build_segments",
    "decode_phrase_boundaries",
    "detect_beats_downbeats",
    "detect_phrases",
    "evaluate_phrase_boundaries_beats",
    "evaluate_phrase_boundaries_seconds",
    "extract_barwise_features",
    "load_stems",
    "plot_phrase_result",
    "save_phrase_json",
    "score_phrase_boundaries",
    "validate_alignment",
]
