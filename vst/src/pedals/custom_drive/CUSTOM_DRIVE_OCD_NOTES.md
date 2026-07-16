# CustomDrive OCD implementation notes

Source: https://www.analogisnotdead.com/article34/circuit-analysis-fulltone-s-ocd
Local schematics: `pedals/custom drive.png`, `pedals/OCD.pdf`

## Target circuit

Use this as a Fulltone OCD-style circuit, not a generic overdrive. The source
matches the local schematic shape: two TL082 non-inverting op-amp stages with a
2N7000 MOSFET / germanium clipping network between them, followed by a
passive treble-bleed tone section and HP/LP bandwidth switch.

Treat this as the local OCD-style drawing plus the PCB Guitar Mania `OCD.pdf`
pot/value reference. The PDF says it is based on version III and confirms
Drive A1M, Tone B10K and Volume A100K. It also lists version alternatives where
D1 can be a jumper or 1N34A; the local `custom drive.png` keeps D1 in the
MOSFET clipping network. The DSP uses the available OA90 as the documented
germanium substitute for that branch.

## Component anchors

- Power: 9 V nominal Boss-style supply, can run at 18 V for more op-amp
  headroom. R1/C2/C3 filter the supply, R4/R7/C5 create Vref at half supply.
- Input: C1 22 nF into R2 1 M reference, R3 10 k current/input isolation, R6
  470 k bias to Vref. Source analysis estimates input impedance near 330 k.
- First gain stage: TL082 half, non-inverting. Drive pot X2 is 1 M in the
  feedback/gain network with R8 18 k, R5 2.2 k, C4 100 nF bass-shaping to
  ground, and C6 220 pF stability/high-frequency limiting.
- Pre-clip shaping: R9 10 k limits current into the clipping network. C7 10 nF
  rolls off high frequencies around the clipper node.
- Clipper: two 2N7000 MOSFETs plus one OA90 germanium diode produce
  asymmetrical hard clipping with softer MOSFET edges. Model this with a
  branch/network solver, not only a tanh threshold.
- Second gain stage: TL082 half, non-inverting recovery gain. Local schematic
  shows R11 39 k / C8 100 nF on the ground leg, R13 150 k feedback, and C9
  220 pF feedback stability cap.
- Tone/output: C10 10 uF coupling into passive tone/output section. X4 Tone is
  B10K with C11 47 nF to ground. HP/LP switch changes the output bandwidth by
  selecting/paralleling R14 22 k and R15 33 k before X5 Volume A100K.

## Component data now available

These files are present in `componentes/` and should be used by the DSP:

- `componentes/CI/TL082.pdf`
- `componentes/transistores/2N7000.pdf`
- `componentes/diode/0A90.pdf`

## Still unresolved

- Exact named OCD revision if we want to advertise a specific Fulltone revision
  instead of the local schematic hybrid.
- Whether D1 should be jumper-only for a strict version III build. Current DSP
  follows `custom drive.png` and keeps the OA90 substitute branch.

## DSP implementation status

- `CustomDriveCore.h` now uses `tl082Spec()` for both op-amp stages.
- Drive uses an A1M-style audio taper, Tone uses B10K linear behavior, and
  Volume uses A100K-style audio taper.
- The main clipping block uses a nonlinear 2N7000/OA90 network solver instead
  of a static `tanh` threshold.
- HP/LP remains a real binary switch mapped from Rocksmith `Voice`.
- Real panel controls remain Drive, Tone, HP/LP, Volume; Rocksmith values map
  into those controls for preset compatibility.
- Current implementation is fixed at a 9 V-style operating point. If a later
  OCD revision target needs 18 V behavior, treat that as an internal calibration
  constant that mainly changes op-amp headroom.
- `test logic/ocd/` supplies seven aligned 32-second renders. They are quieter
  than the project's pedal level, so they calibrate Drive, Tone and clipping
  shape but not absolute output. At noon Drive/Tone/Volume the model is instead
  level-matched to the RAT reference (`-25.9` versus `-25.7` dB RMS).
- The reference gain sweep rises 3.6 dB from half to maximum Drive; the model
  reproduces that rise. The relative Tone response matches within 0.1 dB.
- Validation done in this pass: silence, impulse, seven reference renders and
  clean temp-dir VST3 build.
