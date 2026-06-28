# Input Calibration Contract — bass/guitar amp reference level

**Status:** defined 2026-06-23. Target chosen by the user (Nacho).

## The problem this solves

The amp VST cores model real circuits whose distortion onset depends on the
**absolute input level**. Every player's rig delivers a different digital level
(passive vs active bass, interface gain, pickup output), so without a fixed
reference an amp that is clean for one player clips for another. Earlier the bass
amps were (mis)calibrated against quiet reference recordings (the harness
multitone ≈ −32 dBFS RMS, the `ui_public_inputs` WAVs ≈ −20…−28 dBFS) → a real,
hotter bass over-drove them ("muy distorsionado").

The host's **input calibrator** (the note_detect "Calibration Wizard", invoked by
`plugins/input_setup` via `window.noteDetect.launchCalibration`) normalizes the
player's DI to a fixed level by setting the engine source `input_gain`. As of
notedetect v1.10.0 the *level* calibration is unimplemented (only a manual
**Input Gain** slider 0.1–5.0× and an A/V-sync latency auto-calibrate exist). This
document defines the level it must hit so the amps can be built against it.

## The contract

```
instrument → interface → engine source → [input_gain] → ┬→ note detector
                                                          └→ amp chain (rig_builder VSTs)
```

**Reference level: a hard-played DI peaks at −12 dBFS after `input_gain`.**

- −12 dBFS peak ≈ −18…−21 dBFS RMS for normal playing (bass crest ~6–10 dB).
- Leaves 12 dB of headroom for harder-than-calibration transients (no clipping).
- 0.25 linear peak.

### Calibrator method (for the note_detect dev)

1. Prompt: *"Play your hardest for ~3–4 s (dig in / strum)."*
2. Measure the **95th-percentile of per-block peak** over the window (95th, not
   max, to ignore a single stray transient).
3. `input_gain = 10^((−12 − peak95_dBFS) / 20)`, clamped to the slider range
   **[0.1, 5.0]×**. Persist it as the source `input_gain` (same field the manual
   slider writes — `input_gain` in the notedetect settings / engine source).
4. Optionally show a confirm meter: "play normally → should sit in the green
   (≈ −18 dBFS RMS)."

Until the wizard ships, a user can hit the same target by hand: set the **Input
Gain** slider so their hardest notes just touch −12 dBFS on the input meter.

## Amp calibration reference (rig_builder)

All bass amps are built so that, at **−12 dBFS-peak input** and default knobs,
the core output is **clean with headroom** — peak ≈ 0.4–0.55, crest ≈ 9, NOT
pinned to the ±0.99 `rbAmpLvl` ceiling. Loudness is NOT set here: the rig's final
chain normalizer (target −14, gate −45 in screen.js) lifts the clean output to a
consistent level, so "clean with headroom" never means "too quiet". Pushing an
amp's own Gain/Drive/Volume knobs is what brings breakup.

Verify with the harness `calibrated_di` section (input −12 dBFS peak bass
fundamentals 41/55/73/82/110/220/440 Hz):

```
python3 vst/src/amps/tools/calibrate_amp_core.py <amp>   # see the calibrated_di block
```

Reference numbers at default gain (0.5), 2026-06-23:

| amp                | out peak | crest | clips? |
|--------------------|----------|-------|--------|
| sampleg_sbtcl (SVT)| 0.39     | 9.3   | no     |
| fk800rb (GK)       | 0.49     | 8.9   | no     |
| cs75b_v4b (V-4B)   | 0.52     | 9.4   | no     |
| sharke_hb3500/5000 | 0.55     | 8.7   | no     |

(Sharke clips only when BOTH Tube + Solid preamps are pushed past ~0.6 — a
deliberately hot blend, i.e. real breakup, not a default-level fault.)

### Makeup values that realize this (post the 2026-06-23 re-anchor)

- SVT: `outLevel = 10^(0.05·(17+15·Gain))`, `kSvtMakeup 0.45` (unchanged — the
  6550 tube power amp self-compresses, so it already had headroom).
- GK FK: `mkDb = 11.0 − 14.0·Volume` (was 49.4−25.1; the old base was a quiet-
  input / collision-era patch — see [[project_bass_amp_rework_svt]]).
- V-4B: `outLevel = 10^(0.05·(39 − 17·Gain))` (was 52).
- Sharke HB3500/HB5000: `kHbMakeup 1.0` (was 4.0).

**Rule for future bass amps:** calibrate against the `calibrated_di` check
(−12 dBFS peak), NOT the quiet multitone. "Clean at default on the calibrated DI"
= "clean live". The multitone/THD probes stay useful for voicing regressions only.
