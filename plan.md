# EDM Phrase Detection Plan

## Goal

Build an offline, deterministic phrase detector for mostly 4/4 electronic dance music. The detector should generate stable JSON from already-available track stems. It should detect beats/downbeats first, construct a bar grid, score phrase candidates only at bar starts, and decode phrase boundaries with a phrase-length prior.

## Current Status

- [x] Create standalone `edm_phrase_detect` package skeleton.
- [x] Implement stem loading, validation, and alignment.
- [x] Implement rhythm mix, beat detection, downbeat inference, and bar grid construction.
- [x] Implement barwise stem features and novelty normalization.
- [x] Implement explainable phrase-boundary scoring.
- [x] Implement dynamic-programming phrase decoding and confidence calibration.
- [x] Implement JSON export, debug JSON, plotting, and evaluation helpers.
- [x] Implement deterministic synthetic tests.
- [x] Install/check Python dependencies and run pytest.
- [x] Refactor `Tools/generate_mixdesk.py` to use the stem/bar/DP detector while preserving its CLI.
- [x] Add pluggable generator backend options: `auto`, `librosa`, `madmom`, and `essentia`.
- [x] Update `mixdesk.json` output with `boundary_candidates`, `selected_boundaries`, richer `segments`, phrase confidence, and reasons.
- [x] Update `mixdesk_phrase_analysis.png` with bar markers, boundary candidates, selected boundaries, labels, and score curve.
- [x] Add generator-level synthetic tests for known structure, sidechain, drum fills, vocal entry, missing stems, length mismatch, first-transient phase, and irregular 12-bar phrase.

## Implementation Notes

- First pass is librosa-backed, with optional madmom/essentia backend hooks.
- `Tools/generate_mixdesk.py` now routes beat/downbeat detection, barwise features, boundary scoring, and phrase decoding through `edm_phrase_detect`; the old local peak detector and naive phrase path have been removed.
- Boundaries are never proposed at arbitrary frames; all phrase candidates are bar starts.
- Metrical priors help choose among plausible boundaries but are capped when audio evidence is weak.
- Missing stems are handled by renormalizing fusion weights over available evidence groups.
- Sidechain pumping is explicitly measured so bass ducking does not create false phrase boundaries.

## Verification

- `python -m pytest`
- `python Tools\generate_mixdesk.py D:\tracks --backend auto --target-sr 22050`
- CLI smoke test with a synthetic or local track directory:
  `python -m edm_phrase_detect.cli --track-dir ./track --out ./phrases.json --debug-json ./debug.json --plot ./phrases.png`
