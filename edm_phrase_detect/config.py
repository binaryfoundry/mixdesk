from __future__ import annotations

from dataclasses import dataclass, field


def _default_fusion_weights() -> dict[str, float]:
    return {
        "drums": 0.30,
        "kick": 0.25,
        "harmonic": 0.20,
        "bass": 0.15,
        "vocals": 0.10,
    }


@dataclass(slots=True)
class Config:
    target_sr: int = 44_100
    backend: str = "auto"
    beats_per_bar: int = 4
    hop_length: int = 512
    frame_length: int = 2048
    min_phrase_bars: int = 4
    allowed_phrase_bars: tuple[int, ...] = (4, 8, 16, 32)
    preferred_phrase_bars: tuple[int, ...] = (8, 16)
    max_alignment_shift_ms: float = 30.0
    auto_align: bool = True
    boundary_threshold: float = 0.45
    strong_boundary_threshold: float = 0.70
    novelty_context_bars: int = 4
    foote_kernel_bars: int = 8
    fusion_weights: dict[str, float] = field(default_factory=_default_fusion_weights)
    metrical_prior_weight: float = 0.15
    irregular_length_penalty: float = 0.25
    novelty_weight: float = 1.0
    length_prior_weight: float = 0.55
    multi_stem_agreement_weight: float = 0.35

    def to_dict(self) -> dict[str, object]:
        return {
            "target_sr": self.target_sr,
            "backend": self.backend,
            "beats_per_bar": self.beats_per_bar,
            "hop_length": self.hop_length,
            "frame_length": self.frame_length,
            "min_phrase_bars": self.min_phrase_bars,
            "allowed_phrase_bars": list(self.allowed_phrase_bars),
            "preferred_phrase_bars": list(self.preferred_phrase_bars),
            "max_alignment_shift_ms": self.max_alignment_shift_ms,
            "auto_align": self.auto_align,
            "boundary_threshold": self.boundary_threshold,
            "strong_boundary_threshold": self.strong_boundary_threshold,
            "novelty_context_bars": self.novelty_context_bars,
            "foote_kernel_bars": self.foote_kernel_bars,
            "fusion_weights": dict(self.fusion_weights),
            "metrical_prior_weight": self.metrical_prior_weight,
            "irregular_length_penalty": self.irregular_length_penalty,
            "novelty_weight": self.novelty_weight,
            "length_prior_weight": self.length_prior_weight,
            "multi_stem_agreement_weight": self.multi_stem_agreement_weight,
        }


def parse_int_tuple(value: str) -> tuple[int, ...]:
    items = [item.strip() for item in value.split(",") if item.strip()]
    if not items:
        raise ValueError("Expected a comma-separated list of integers.")
    return tuple(int(item) for item in items)
