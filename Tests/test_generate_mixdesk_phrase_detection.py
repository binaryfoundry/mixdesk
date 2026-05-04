from __future__ import annotations

from pathlib import Path

import numpy as np
import soundfile as sf

from edm_phrase_detect.beatgrid import infer_downbeat_phase
from edm_phrase_detect.config import Config
from edm_phrase_detect.models import PhraseBoundary, PhraseResult, PhraseSegment
from Tools.generate_mixdesk import (
    bar_beat_indices,
    boundary_candidates,
    build_mixdesk,
    detector_result_to_beat_grid,
    detector_result_to_phrases,
    seconds_per_beat,
    selected_boundaries,
)
from test_synthetic_simple import write_synthetic_track


def _build(track: Path):
    return build_mixdesk(track, backend="librosa", target_sr=22_050)


def _selected_bars(data) -> set[int]:
    return {int(boundary["bar_index"]) for boundary in data["phrase_analysis"]["selected_boundaries"]}


def test_seconds_per_beat_uses_fitted_grid_not_quantized_median() -> None:
    beats = [0.0]
    for frames in (42, 41) * 8:
        beats.append(beats[-1] + frames * 512 / 44_100)

    fitted = seconds_per_beat(beats)
    median_interval = 42 * 512 / 44_100

    assert abs(fitted - median_interval) > 0.001
    assert abs(fitted - (beats[-1] / (len(beats) - 1))) < 0.001


def test_downbeat_phase_prior_does_not_force_first_beat() -> None:
    sr = 22_050
    beat_s = 0.5
    beat_times = np.arange(0.0, 32 * beat_s, beat_s)
    audio = np.zeros(int(round((beat_times[-1] + beat_s) * sr)), dtype=np.float32)
    pulse = np.hanning(int(round(0.03 * sr))).astype(np.float32)
    for index, time_s in enumerate(beat_times):
        start = int(round(time_s * sr))
        amplitude = 1.0 if index % 4 == 2 else 0.35
        end = min(len(audio), start + len(pulse))
        if end > start:
            audio[start:end] += amplitude * pulse[: end - start]

    phase, debug = infer_downbeat_phase(
        beat_times,
        {"drums": audio, "kick": audio},
        sr,
        Config(target_sr=sr, hop_length=256, frame_length=1024),
    )

    assert phase == 2
    assert debug["phase_margin"] > 0.5


def test_generator_uses_downbeat_indices_for_offbeat_pickup_json() -> None:
    result = PhraseResult(
        bpm=120.0,
        duration_s=5.0,
        beats=[0.0, 0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 3.5],
        downbeats=[1.0, 3.0],
        bars=[1.0, 3.0],
        boundaries=[PhraseBoundary(time_s=1.0, bar_index=1, confidence=1.0, reason={"initial_boundary": 1.0})],
        segments=[PhraseSegment(start_s=1.0, end_s=3.0, start_bar=1, end_bar=1, length_bars=1, confidence=0.9)],
        metadata={"beats_per_bar": 4, "selected_backend": "fixture"},
    )
    debug = {
        "beat_events": [
            {"time_s": 0.0, "beat_index": 0, "beat_in_bar": 3},
            {"time_s": 0.5, "beat_index": 1, "beat_in_bar": 4},
            {"time_s": 1.0, "beat_index": 2, "beat_in_bar": 1},
            {"time_s": 1.5, "beat_index": 3, "beat_in_bar": 2},
            {"time_s": 2.0, "beat_index": 4, "beat_in_bar": 3},
            {"time_s": 2.5, "beat_index": 5, "beat_in_bar": 4},
            {"time_s": 3.0, "beat_index": 6, "beat_in_bar": 1},
            {"time_s": 3.5, "beat_index": 7, "beat_in_bar": 2},
        ],
        "candidate_boundary_scores": [1.0, 0.4],
        "candidate_reasons": [{"initial_boundary": 1.0}, {"candidate_score": 0.4}],
        "features": {"num_bars": 2, "feature_arrays": {}},
    }

    indices = bar_beat_indices(result, debug)
    candidates = boundary_candidates(result, debug)
    selected = selected_boundaries(result, candidates, indices)
    phrases = detector_result_to_phrases(result, debug, selected, indices)
    beat_grid = detector_result_to_beat_grid(result, debug)

    assert indices == [2, 6]
    assert beat_grid["first_beat_offset_seconds"] == 0.0
    assert beat_grid["first_bar_offset_seconds"] == 1.0
    assert beat_grid["bar_beat_indices"] == [2, 6]
    assert candidates[0]["beat_index"] == 2
    assert candidates[1]["beat_index"] == 6
    assert selected[-1]["beat_index"] == 10
    assert phrases[0]["beat_index"] == 2
    assert phrases[0]["beat_count"] == 4


def test_generator_detects_8_16_8_structure(tmp_path: Path) -> None:
    track = tmp_path / "simple"
    write_synthetic_track(
        track,
        bars=32,
        bass_ranges=((9, 24),),
        kick_ranges=((1, 24),),
        synth_ranges=((9, 32),),
        vocal_ranges=((25, 32),),
        stems=("kick", "drums", "bass", "synth", "vocals", "mix"),
    )
    data = _build(track)
    assert data["phrase_analysis"]["status"] == "stem_novelty_dp_v1"
    assert data["phrase_analysis"]["method"] == "offline_stem_barwise_v1"
    assert "naive" not in data["phrase_analysis"]["method"]
    assert {0, 8, 24}.issubset(_selected_bars(data))
    assert any(phrase["beat_count"] == 64 for phrase in data["phrases"])


def test_generator_sidechain_does_not_split_every_bar(tmp_path: Path) -> None:
    track = tmp_path / "sidechain"
    write_synthetic_track(track, bars=24, bass_ranges=((1, 24),), sidechain_bass=True, stems=("kick", "drums", "bass", "synth", "mix"))
    data = _build(track)
    internal = [bar for bar in _selected_bars(data) if bar not in {0, 24, 25}]
    assert len(internal) <= 2


def test_generator_drum_fill_raises_boundary_reason(tmp_path: Path) -> None:
    track = tmp_path / "fill"
    write_synthetic_track(track, bars=16, bass_ranges=((9, 16),), drum_fill_before=(9,), stems=("kick", "drums", "bass", "synth", "mix"))
    data = _build(track)
    candidate = data["phrase_analysis"]["boundary_candidates"][8]
    assert candidate["reason"]["drums"] > 0.20


def test_generator_vocal_entry_boundary_reason(tmp_path: Path) -> None:
    track = tmp_path / "vocal"
    write_synthetic_track(track, bars=24, bass_ranges=((1, 24),), vocal_ranges=((17, 24),), stems=("kick", "drums", "bass", "synth", "vocals", "mix"))
    data = _build(track)
    candidate = data["phrase_analysis"]["boundary_candidates"][16]
    assert candidate["reason"]["vocals"] > 0.20


def test_generator_missing_vocals_degrades_gracefully(tmp_path: Path) -> None:
    track = tmp_path / "missing_vocals"
    write_synthetic_track(track, bars=16, bass_ranges=((9, 16),), stems=("kick", "drums", "bass", "synth", "mix"))
    data = _build(track)
    assert data["phrases"]
    assert "vocals" in data["phrase_analysis"]["debug"]["missing_stems"]


def test_generator_missing_bass_degrades_gracefully(tmp_path: Path) -> None:
    track = tmp_path / "missing_bass"
    write_synthetic_track(track, bars=16, bass_ranges=(), stems=("kick", "drums", "synth", "vocals", "mix"))
    data = _build(track)
    assert data["phrases"]
    assert "bass" in data["phrase_analysis"]["debug"]["missing_stems"]


def test_generator_only_drums_and_mix_available(tmp_path: Path) -> None:
    track = tmp_path / "drums_mix"
    write_synthetic_track(track, bars=16, bass_ranges=((9, 16),), stems=("drums", "mix"))
    data = _build(track)
    assert data["beat_grid"]["beat_events"]
    assert data["phrases"]
    assert any(phrase["has_drums"] for phrase in data["phrases"])


def test_generator_slight_stem_length_mismatch_is_trimmed(tmp_path: Path) -> None:
    track = tmp_path / "length_mismatch"
    rendered = write_synthetic_track(track, bars=16, bass_ranges=((9, 16),), stems=("kick", "drums", "bass", "mix"))
    sr = 22_050
    sf.write(track / "bass.wav", rendered["bass"][:- int(0.05 * sr)], sr)
    data = _build(track)
    warnings = data["phrase_analysis"]["debug"]["warnings"]
    assert any("Trimmed stems" in warning for warning in warnings)


def test_generator_first_transient_is_not_forced_to_bar_one(tmp_path: Path) -> None:
    track = tmp_path / "pickup"
    rendered = write_synthetic_track(track, bars=16, bass_ranges=((9, 16),), stems=("kick", "drums", "bass", "mix"))
    sr = 22_050
    drums = rendered["drums"].copy()
    drums[int(0.08 * sr): int(0.09 * sr)] += 1.0
    sf.write(track / "drums.wav", drums, sr)
    data = _build(track)
    assert data["beat_grid"]["beat_events"][0]["beat_in_bar"] in {1, 2, 3, 4}
    assert data["beat_grid"]["bars"][0] > 0.0


def test_generator_allows_irregular_12_bar_phrase_with_evidence(tmp_path: Path) -> None:
    track = tmp_path / "irregular"
    write_synthetic_track(
        track,
        bars=28,
        bass_ranges=((13, 28),),
        kick_ranges=((1, 12), (13, 28)),
        synth_ranges=((13, 28),),
        stems=("kick", "drums", "bass", "synth", "mix"),
    )
    data = _build(track)
    assert 12 in _selected_bars(data)
