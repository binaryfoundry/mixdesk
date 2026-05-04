from __future__ import annotations

import numpy as np

from edm_phrase_detect.config import Config
from edm_phrase_detect.decoding import decode_phrase_boundaries, length_prior
from edm_phrase_detect.models import Bar
from edm_phrase_detect.scoring import metrical_prior_for_bar_index


def test_configured_preferred_phrase_lengths_drive_priors() -> None:
    config = Config(allowed_phrase_bars=(4, 8, 12, 16), preferred_phrase_bars=(12,))

    assert length_prior(12, config) > length_prior(8, config)
    assert length_prior(12, config) > length_prior(16, config)
    assert metrical_prior_for_bar_index(12, config) > metrical_prior_for_bar_index(8, config)


def test_decoder_does_not_select_boundary_that_creates_short_final_phrase() -> None:
    config = Config(min_phrase_bars=4)
    bars = [Bar(index=index + 1, start_s=float(index), end_s=float(index + 1), beat_times=[]) for index in range(8)]
    scores = np.zeros(len(bars), dtype=np.float64)
    scores[7] = 1.0
    reasons = [
        {"initial_boundary": 1.0}
        if index == 0
        else {
            "candidate_score": float(scores[index]),
            "audio_score": float(scores[index]),
            "multi_stem_agreement": 1.0 if scores[index] else 0.0,
            "metrical_prior": metrical_prior_for_bar_index(index, config),
        }
        for index in range(len(bars))
    ]

    boundaries, segments, _ = decode_phrase_boundaries(bars, scores, reasons, config)

    assert 8 not in {boundary.bar_index for boundary in boundaries}
    assert all(segment.length_bars >= config.min_phrase_bars for segment in segments)
