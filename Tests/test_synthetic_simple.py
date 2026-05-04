from __future__ import annotations

from pathlib import Path

import numpy as np
import soundfile as sf

from edm_phrase_detect.beatgrid import build_bars
from edm_phrase_detect.cli import detect_track
from edm_phrase_detect.config import Config
from edm_phrase_detect.models import BeatEvent


def make_config() -> Config:
    return Config(target_sr=22_050, backend="librosa", hop_length=512, frame_length=1024)


def _decay(length: int, amount: float = 7.0) -> np.ndarray:
    return np.exp(-np.linspace(0.0, amount, length, endpoint=False))


def _add(target: np.ndarray, start: int, wave: np.ndarray) -> None:
    end = min(len(target), start + len(wave))
    if end > start:
        target[start:end] += wave[: end - start]


def write_synthetic_track(
    root: Path,
    *,
    sr: int = 22_050,
    bpm: float = 128.0,
    bars: int = 32,
    bass_ranges: tuple[tuple[int, int], ...] = ((9, 24),),
    kick_ranges: tuple[tuple[int, int], ...] = ((1, 32),),
    synth_ranges: tuple[tuple[int, int], ...] = ((9, 32),),
    vocal_ranges: tuple[tuple[int, int], ...] = (),
    drum_fill_before: tuple[int, ...] = (),
    sidechain_bass: bool = False,
    stems: tuple[str, ...] = ("kick", "drums", "bass", "synth", "vocals", "mix"),
) -> dict[str, np.ndarray]:
    root.mkdir(parents=True, exist_ok=True)
    beat_s = 60.0 / bpm
    bar_s = beat_s * 4.0
    duration_s = bars * bar_s
    count = int(round(duration_s * sr))
    t = np.arange(count) / sr
    kick = np.zeros(count, dtype=np.float32)
    drums = np.zeros(count, dtype=np.float32)
    bass = np.zeros(count, dtype=np.float32)
    synth = np.zeros(count, dtype=np.float32)
    vocals = np.zeros(count, dtype=np.float32)

    def in_ranges(bar: int, ranges: tuple[tuple[int, int], ...]) -> bool:
        return any(start <= bar <= end for start, end in ranges)

    kick_wave_t = np.arange(int(0.18 * sr)) / sr
    kick_wave = (np.sin(2 * np.pi * 55 * kick_wave_t) * _decay(len(kick_wave_t), 8.0)).astype(np.float32)
    hat_wave = (np.random.default_rng(123).normal(0, 0.08, int(0.025 * sr)) * _decay(int(0.025 * sr), 6.0)).astype(np.float32)
    snare_wave = (np.random.default_rng(456).normal(0, 0.20, int(0.07 * sr)) * _decay(int(0.07 * sr), 5.0)).astype(np.float32)

    for bar in range(1, bars + 1):
        for beat in range(4):
            beat_time = ((bar - 1) * 4 + beat) * beat_s
            sample = int(round(beat_time * sr))
            if in_ranges(bar, kick_ranges):
                _add(kick, sample, 0.95 * kick_wave)
                _add(drums, sample, 0.75 * kick_wave)
            if beat in (1, 3):
                _add(drums, sample, snare_wave)
            for half in (0.0, 0.5):
                _add(drums, int(round((beat_time + half * beat_s) * sr)), hat_wave)

        if bar + 1 in drum_fill_before:
            fill_start = int(round(((bar - 1) * bar_s + bar_s * 0.75) * sr))
            for offset in np.linspace(0.0, bar_s * 0.22, 8):
                _add(drums, fill_start + int(round(offset * sr)), 0.8 * snare_wave)

        if in_ranges(bar, bass_ranges):
            start = int(round((bar - 1) * bar_s * sr))
            end = int(round(bar * bar_s * sr))
            note_t = t[start:end] - t[start]
            tone = 0.32 * np.sign(np.sin(2 * np.pi * 55 * note_t))
            if sidechain_bass and in_ranges(bar, kick_ranges):
                gain = np.ones_like(tone)
                for beat in range(4):
                    beat_start = int(round(beat * beat_s * sr))
                    duck_len = int(round(0.18 * sr))
                    duck_end = min(len(gain), beat_start + duck_len)
                    if duck_end > beat_start:
                        gain[beat_start:duck_end] *= np.linspace(0.18, 1.0, duck_end - beat_start)
                tone *= gain
            bass[start:end] += tone.astype(np.float32)

        if in_ranges(bar, synth_ranges):
            start = int(round((bar - 1) * bar_s * sr))
            end = int(round(bar * bar_s * sr))
            note_t = t[start:end]
            root_hz = 220.0 if ((bar - 1) // 8) % 2 == 0 else 246.94
            pad = 0.10 * np.sin(2 * np.pi * root_hz * note_t) + 0.08 * np.sin(2 * np.pi * root_hz * 1.5 * note_t)
            synth[start:end] += pad.astype(np.float32)

        if in_ranges(bar, vocal_ranges):
            start = int(round(((bar - 1) * bar_s + bar_s * 0.12) * sr))
            for phrase in range(2):
                burst_start = start + int(round(phrase * bar_s * 0.42 * sr))
                length = int(round(0.35 * sr))
                vt = np.arange(length) / sr
                burst = 0.18 * np.sin(2 * np.pi * 330 * vt) * _decay(length, 2.2)
                _add(vocals, burst_start, burst.astype(np.float32))

    mix = kick + drums + bass + synth + vocals
    rendered = {
        "kick": kick,
        "drums": drums,
        "bass": bass,
        "synth": synth,
        "vocals": vocals,
        "mix": mix,
    }
    for name in stems:
        sf.write(root / f"{name}.wav", rendered[name], sr)
    return rendered


def boundary_bars(result) -> set[int]:
    return {boundary.bar_index for boundary in result.boundaries}


def test_simple_structure(tmp_path: Path) -> None:
    track = tmp_path / "simple"
    write_synthetic_track(
        track,
        bars=32,
        bass_ranges=((9, 24),),
        kick_ranges=((1, 24),),
        synth_ranges=((9, 32),),
        vocal_ranges=((25, 32),),
    )
    result, _, _, _, _ = detect_track(track, make_config())
    bars = boundary_bars(result)
    assert result.downbeats == result.bars
    assert 1 in bars
    assert 9 in bars
    assert 25 in bars


def test_bar_grid_does_not_promote_closing_hit_to_extra_bar() -> None:
    config = make_config()
    beat_s = 0.5
    duration_s = 32 * 4 * beat_s
    beat_events = [
        BeatEvent(time_s=index * beat_s, beat_in_bar=(index % 4) + 1)
        for index in range((32 * 4) + 1)
    ]

    bars, warnings = build_bars(beat_events, duration_s, config)

    assert len(bars) == 32
    assert bars[-1].end_s == duration_s
    assert any("Discarded trailing downbeat" in warning for warning in warnings)

