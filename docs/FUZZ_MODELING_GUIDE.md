# Fuzz Modeling Guide

Estado: 2026-07-19

This guide records the circuit and calibration rules learned while matching the
Big Buzz and Buzz-Tone plugins to the supplied Big Muff and vintage-fuzz renders.
It applies to guitar and bass fuzzes, but it is not a shared voicing preset: each
pedal must retain the clipping mechanism, filtering and controls in its schematic.

Apply [`REFERENCE_MATCHING_WORKFLOW.md`](REFERENCE_MATCHING_WORKFLOW.md) first.
That document owns DI identity, alignment, calibration order, acceptance gates
and deployment. This guide adds the topology-specific requirements for fuzz.

## 1. Preserve The Actual Nonlinear Topology

- Model the stage that creates the fuzz. A diode pair in a transistor feedback
  loop is not equivalent to a diode shunt at the output. A phase splitter plus
  rectifier is not equivalent to adding even harmonics. A biased transistor
  pair is not equivalent to a final `tanh`.
- Let later transistors act as recovery or buffering stages when the schematic
  gives them that role. Re-clipping an already-fuzzed signal at every stage turns
  pick attack and note decay into generic broken distortion.
- Keep asymmetry, bias collapse and gating inside the transistor or diode stage
  that causes them. Do not recreate them with a broadband output limiter.
- Oversample the nonlinear circuit, then downsample before final output gain.

## 2. Model Coupling Once

- A coupling capacitor and its effective load already remove DC. Do not place an
  extra `DcBlock` immediately before or after its RC high-pass model.
- Duplicate DC removal thins low mids, adds phase rotation and creates excess
  zero crossings. This was a major source of the brittle, interrupted sound.
- A DC blocker is still appropriate after full-wave rectification when the real
  following coupling network must remove the rectifier's DC component.
- Derive each corner from the capacitor plus the source/load impedance seen at
  that node, including pot tracks and the next stage's input resistance.

## 3. Controls Must Change The Right Quantity

- Sustain/Fuzz should drive or bias the documented nonlinear stage. It must not
  be implemented as a generic output gain.
- Use the real pot taper. Audio, linear, reverse-log, W and C tapers produce very
  different useful ranges.
- Do not cancel the knob with inverse makeup. The Big Buzz originally became
  quieter as Sustain rose because two inverse trims overcompensated the circuit.
- If a game preset requires controlled loudness, use a measured static curve
  based on complete renders. Avoid sample-by-sample wet/dry makeup on fuzz: it
  changes the decay, pumps between notes and can erase the intended compression.
- Apply the real Volume control after circuit calibration.

## 4. Tone Networks Are Circuit Branches

- Implement the fixed low-pass and high-pass branches from their R/C values,
  then model the pot as the electrical blend between those branch voltages.
- Do not move both filter corners with the tone knob unless the schematic really
  uses variable resistors in those branches.
- Account for loading at each end of the pot. Loaded resistance explained the
  difference between the nominal and measured Big Muff tone corners.
- A passive two-branch network creates its own mid scoop. Do not add a synthetic
  notch unless the schematic has another notch path.

## 5. Output Stages And Safety

- Remove redundant final waveshapers when transistor/diode stages already bound
  the signal. They reduce crest factor and make unlike fuzzes sound identical.
- A safety rail may remain transparent inside `[-1, 1]` and act only on invalid
  overs. Normal renders should never reach it.
- Recovery stages should preserve pick transients. Calibrate their passive loss
  after matching their headroom, rather than driving them into another clipper.

## 6. Reference Calibration

Always render the same DI through the plugin and reference at every supplied
control position. At minimum measure:

- RMS, peak and crest factor.
- Envelope correlation and gain spread over 50 ms windows.
- DI/output coherence by band, matched to the reference rather than minimized.
- Spectral centroid plus octave/band energy.
- Zero-crossing rate as a warning for duplicated high-pass filtering or fizz.
- Sustain/Fuzz sweep direction and the change from minimum to noon to maximum.
- Silence, impulse tail, finite output and maximum peak over an 11-point sweep.

The 2026-07-19 Big Buzz pass used all nine combinations of Tone and Sustain at
minimum/noon/maximum with the exact Brit DI. The final circuit uses:

- DC-biased nodal 2N5133 models for Q4, both clipping cells and Q1.
- C6/C7-coupled 1N914 pairs inside the Q3/Q2 collector-to-base feedback loops,
  rather than output shunt clippers.
- The schematic's R/C values in both tone branches. Do not add a second global
  C9 high-pass before the tone network; that duplicate removed up to 7 dB below
  180 Hz.
- A 0.3% Sustain floor. The former 1.5% floor kept Q3/Q2 nonlinear at the panel
  minimum and produced up to 12.1 dB excess high-frequency energy. At 0.3%,
  DI/output coherence is within 0.01 of the minimum-Sustain references.
- A branch-voltage tone approximation with measured centre loading. A fixed
  three-node 70 k Q1 load was tested and rejected: it leaked the opposite branch
  at both endpoints and made the noon notch 3 dB too deep. Keep the endpoint RC
  values fixed and calibrate only their wiper interaction from all three renders.

After post-circuit output calibration, eight of nine RMS errors are between
-0.27 and +0.34 dB. The original dark/minimum interaction was corrected as a
2D static trim instead of altering a nonlinear stage. RMS agreement alone is
not sufficient: the accepted half/max settings also keep broad bands generally
within 2 dB, crest within about 1.2 dB and mid/high coherence within 0.011.

The 2026-07-17 Fuzz-Tone/FuzzRite reference set added two stronger anchors:

- **Fuzz-Tone / FZ-1A:** the real control is `Attack`, not a generic output
  gain. Minimum leaves Q2 near cutoff and preserves pick crest; maximum moves
  Q2 into conduction and compression. On the 32 s DI, the min/noon/max target
  RMS values are -24.07/-16.08/-12.57 dBFS. The calibrated model reaches
  -24.07/-16.09/-12.57 dBFS with no active-window dropout. Do not invert Q2
  bias or use envelope makeup to obtain that 11.5 dB sweep.
- **FuzzRite:** Depth is a passive wiper between the direct C2 side and the
  loaded C3/R8 side. Solving only 500 k against 22 k over-attenuates the loaded
  endpoint and produces a dark, interrupted result because it ignores the
  finite collector, coupling and Volume-pot impedances. The measured min/noon/
  max RMS values are -9.57/-13.88/-14.15 dBFS; the calibrated model reaches
  -9.61/-13.88/-14.15 dBFS. Keep C4 feedback below the sustained-note
  cancellation point and verify complete active windows, not only finite output.

The follow-up audit established topology regression anchors using a 220 Hz sine
at 0.03 amplitude (110 Hz at 0.05 for bass). These are not universal fidelity
targets, but a future edit should explain a large change:

- Fuzz Rite: 30.8% THD at the default Depth and 40.4% at maximum, predominantly
  odd harmonics. Depth must increase harmonic energy monotonically; the extra
  top-end saturation starts above 0.70 and a clockwise sweep must not turn into
  a dark attenuation control. Validate active DI windows as well as finite
  samples: an over-strong C4 loop can produce audible 40-50 dB dropouts without
  NaNs or exact zeros.
- BZ-1: 44.2% THD at the default Fuzz setting.
- Super-Buzz: second harmonic 0.8 dB above the fundamental, confirming that the
  rectifier produces a real octave rather than generic clipping.
- Bass Fuzz: 35.7% THD; Normal, Bass Boost and Dry remain within 0.5 dB through
  the measured Sustain sweep without envelope-driven makeup. Bass Boost raises
  30-160 Hz energy and Dry has a clearly higher crest factor than Normal.

## 7. Topology-Specific Checks

- **Big Muff family:** two feedback-diode clipping stages, fixed passive tone
  branches and a recovery transistor. Sustain rises early and then compresses.
- **Fuzz Rite:** two grounded-emitter stages coupled through the collector
  feedback path. Depth reads from the direct C2 endpoint toward the loaded
  Q2-input endpoint; verify its panel direction, taper and complete source/load
  network rather than treating 500 k and 22 k as an isolated divider. C4
  feedback must remain audible.
- **Fuzz Face / FZ-3 family:** transistor bias and shunt feedback create bloom
  and gating. Do not replace that behavior with output clipping.
- **Super-Fuzz:** preserve the anti-phase splitter and germanium full-wave
  rectifier. The octave must track input pitch before the tone switch. Balance
  is the 50 kB output-level pot, not a clean/fuzz blend; use its audio taper
  after the Q6/output network. Any safety limiter must be continuous at the
  rail or it can make the Balance sweep sound stuck and broken.
- **Bass Big Muff:** retain the two clipping cells and the three-way SW2 routing.
  Bass Boost takes the already-clipped node 1 through R2/C4/U1A and C9/R7, then
  injects it before the Q1 recovery stage; it must never use clean input. Dry is
  the separate node-A/U1B/R31 path added at U1C after the fuzz Volume path. Do
  not use an envelope-driven makeup stage to force constant RMS.

## 8. Completion Checklist

1. Read the schematic and component documents.
2. Identify every nonlinear, coupling, tone and recovery block.
3. Verify controls and tapers against the real panel.
4. Render the complete DI at minimum, noon and maximum settings.
5. Compare coherence, harmonic growth, crest windows and note decay to reference.
6. Correct circuit errors before adding any calibration curve.
7. Run parameter sweeps and impulse/silence stability tests.
8. Build in a fresh temporary directory.
9. Install canonical and alias VST3 bundles, sign them and verify signatures.
