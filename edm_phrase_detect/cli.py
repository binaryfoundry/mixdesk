from __future__ import annotations

import argparse
from dataclasses import asdict
from pathlib import Path
from typing import Any

from .alignment import validate_and_align_stems
from .bar_features import extract_barwise_features
from .beatgrid import build_bars, detect_beats_downbeats
from .config import Config, parse_int_tuple
from .decoding import decode_phrase_boundaries
from .export import build_debug_json, save_debug, save_result
from .io import KNOWN_STEMS, load_stems
from .labels import label_segments
from .models import PhraseResult, StemInfo
from .plotting import plot_phrase_result
from .scoring import score_phrase_boundaries


def _estimate_grid_confidence(backend_name: str, beat_events: list[Any], backend_debug: dict[str, Any]) -> tuple[float, float]:
    beat_count = len(beat_events)
    beat_confidence = 0.72 if backend_name == "librosa" else 0.88
    downbeat_confidence = 0.62 if backend_name == "librosa" else 0.86
    if beat_count < 32:
        beat_confidence *= 0.75
        downbeat_confidence *= 0.75

    phase_scores = backend_debug.get("phase_scores", {})
    if isinstance(phase_scores, dict) and len(phase_scores) >= 2:
        totals = []
        for value in phase_scores.values():
            if isinstance(value, dict):
                value = value.get("total", 0.0)
            try:
                totals.append(float(value))
            except (TypeError, ValueError):
                continue
        if len(totals) >= 2:
            values = sorted(totals)
            spread = values[-1] - values[-2]
            downbeat_confidence = max(0.35, min(0.92, downbeat_confidence + 0.08 * spread))

    return float(max(0.0, min(1.0, beat_confidence))), float(max(0.0, min(1.0, downbeat_confidence)))


def detect_loaded_stems(
    stems: dict[str, Any],
    sr: int,
    config: Config,
    stem_info: dict[str, StemInfo] | None = None,
) -> tuple[PhraseResult, dict[str, Any], dict[str, Any], int, dict[str, StemInfo]]:
    stem_info = dict(stem_info or {})
    stems, duration_s, alignment_debug = validate_and_align_stems(stems, sr, config)
    beat_events, bpm, backend_name, backend_debug = detect_beats_downbeats(stems, sr, config)
    backend_debug["selected_backend"] = backend_name
    bars, bar_warnings = build_bars(beat_events, duration_s, config)
    alignment_debug.setdefault("warnings", [])
    alignment_debug["warnings"].extend(bar_warnings)

    features = extract_barwise_features(stems, sr, bars, beat_events, config)
    candidate_scores, reasons, group_scores = score_phrase_boundaries(features, bars, config)
    boundaries, segments, decoding_debug = decode_phrase_boundaries(bars, candidate_scores, reasons, config)
    segments = label_segments(segments, features)

    beats = [beat.time_s for beat in beat_events]
    bar_starts = [bar.start_s for bar in bars]
    downbeats = list(bar_starts)
    missing_stems = [name for name in KNOWN_STEMS if name not in stems]
    non_wav_inputs = sorted(
        name
        for name, info in stem_info.items()
        if Path(info.path).suffix.lower() not in {".wav", ".wave"}
    )
    if non_wav_inputs:
        alignment_debug["warnings"].append(
            "Non-WAV analysis inputs detected for "
            + ", ".join(non_wav_inputs)
            + "; compressed or container formats can add timing offsets, so WAV stems are preferred."
        )
    beat_confidence, downbeat_confidence = _estimate_grid_confidence(backend_name, beat_events, backend_debug)
    metadata = {
        "selected_backend": backend_name,
        "target_sr": sr,
        "beats_per_bar": config.beats_per_bar,
        "stems_used": sorted(stems.keys()),
        "missing_stems": missing_stems,
        "alignment_shifts_ms": alignment_debug.get("alignment_shifts_ms", {}),
        "warnings": alignment_debug.get("warnings", []),
        "beat_confidence": beat_confidence,
        "downbeat_confidence": downbeat_confidence,
        "grid_confidence": min(beat_confidence, downbeat_confidence),
    }
    result = PhraseResult(
        schema_version="1.0.0",
        bpm=float(bpm),
        duration_s=float(duration_s),
        beats=beats,
        downbeats=downbeats,
        bars=bar_starts,
        boundaries=boundaries,
        segments=segments,
        metadata=metadata,
    )
    debug = build_debug_json(
        config=config.to_dict(),
        stem_info=stem_info,
        backend_debug=backend_debug,
        alignment_debug=alignment_debug,
        bars=[asdict(bar) for bar in bars],
        features=features,
        group_scores=group_scores,
        candidate_scores=candidate_scores,
        reasons=reasons,
        decoding_debug=decoding_debug,
    )
    debug["beat_events"] = [
        {"time_s": float(beat.time_s), "beat_index": index, "beat_in_bar": int(beat.beat_in_bar), "confidence": None}
        for index, beat in enumerate(beat_events)
    ]
    debug["duration_s"] = float(duration_s)
    debug["bpm"] = float(bpm)
    debug["metadata"] = metadata
    return result, debug, stems, sr, stem_info


def detect_track(track_dir: str | Path, config: Config) -> tuple[PhraseResult, dict[str, Any], dict[str, Any], int, dict[str, Any]]:
    stems, sr, stem_info = load_stems(track_dir, config.target_sr)
    return detect_loaded_stems(stems, sr, config, stem_info)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Offline phrase detection for 4/4 electronic dance music.")
    parser.add_argument("--track-dir", required=True, type=Path, help="Folder containing stem wav files.")
    parser.add_argument("--out", required=True, type=Path, help="Output phrase JSON path.")
    parser.add_argument("--plot", type=Path, help="Optional output plot path.")
    parser.add_argument("--debug-json", type=Path, help="Optional debug JSON path.")
    parser.add_argument("--backend", default="auto", choices=("auto", "librosa", "madmom", "essentia"))
    parser.add_argument("--target-sr", default=44_100, type=int)
    parser.add_argument("--min-phrase-bars", default=4, type=int)
    parser.add_argument("--allowed-phrase-bars", default="4,8,16,32")
    parser.add_argument("--preferred-phrase-bars", default="8,16")
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    config = Config(
        target_sr=args.target_sr,
        backend=args.backend,
        min_phrase_bars=args.min_phrase_bars,
        allowed_phrase_bars=parse_int_tuple(args.allowed_phrase_bars),
        preferred_phrase_bars=parse_int_tuple(args.preferred_phrase_bars),
    )
    result, debug, stems, sr, _ = detect_track(args.track_dir, config)
    save_result(result, args.out)
    if args.debug_json:
        save_debug(debug, args.debug_json)
    if args.plot:
        plot_phrase_result(result, args.plot, stems=stems, sr=sr, debug=debug)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
