# Fuzz Pedal Rework

Branch: `feat/amps-rework`

Goal: port the fuzz pedals to the same style as the amp rework: read the real
schematic, model each audible circuit block from component values, keep the
core DPF-free when possible, run strong nonlinear sections oversampled, and
validate with offline sweeps before installing the VST3 bundle.

The reusable implementation and validation rules are documented in
`docs/FUZZ_MODELING_GUIDE.md`.

Important licensing rule: Guitarix is useful as a technical reference for
topology, oversampling and validation ideas, but its GPL code must not be copied
into these bundled VSTs. Implement our own models from the schematic and public
electronics equations.

## Current Inventory

| RS gear | Bundle | Real reference | Source schematic | Status |
|---|---|---|---|---|
| `Pedal_CaptFuzzle` | `Buzz-Tone.vst3` | 1.5 V Captain Fuzzle / Maestro FZ-1A style, 3x 2N1305 | `pedals/captain fuzzle.gif` | Reworked and reference-calibrated in `BuzzToneCore.h`; 4x oversampled |
| `Pedal_FuzzWasHe` | `BZ-1.vst3` | Boss FZ-3 style silicon fuzz | `pedals/Fuzz Was He.pdf` / Aion Argent | Fuzz feedback retained; duplicate DC blocks and output clipping removed; fixed-branch tone network |
| `Pedal_BuzzToo` | `Big Buzz.vst3` | Big Muff V1 / triangle-era topology | `pedals/buzz 2.jpg` | Reworked with real Sustain/Tone/Volume controls |
| `Pedal_BuzzOne` | `Super-Buzz.vst3` | Univox Super-Fuzz octave fuzz | `pedals/buzz 1.gif` | OA90 full-wave octave retained; only post-rectifier DC removal remains; redundant output clipping removed |
| `Bass_Pedal_BassFuzz` | `Bass Big Buzz.vst3` | Bass Big Muff style | `pedals/Bass Big Muff schematics.png`; `pedals/bookmark-this-big-muff-schematics-explained-*.webp` | 4x oversampled; Bass Boost now reinjects filtered, clipped pre-tone signal before Q1 recovery; only Dry adds node-A clean signal at the U1C final sum; modes are statically level-matched |
| `Extra_FuzzRite` | `FuzzRite.vst3` | Mosrite Fuzz Rite style silicon fuzz | `pedals/fuzzrite/FuzzRiteV3.pdf` | Loaded Depth node and panel direction corrected; stronger Q1 clipping retained with stable C4 feedback and interstage DC blocking; full-DI dropout audit passes |

## Pattern Used For Buzz-Tone

- Core moved to `buzz_tone/BuzzToneCore.h`, with no DPF dependency.
- Real component anchors:
  - C1 10 nF, R1 100 k, R2 1 M input loading/coupling.
  - C2/C3 1 uF interstage coupling into low-k bias networks.
  - C4 10 nF into 50 k volume pot, which makes the high-pass corner part of the
    thin FZ-1A sound.
  - Q1/Q2/Q3 modeled as low-headroom 2N1305 germanium stages on a 1.5 V rail.
  - FUZZ pot changes Q2 bias/feedback texture rather than acting as a generic
    linear gain.
- Wrapper uses `rbshared::Oversampler4x` around the nonlinear core.
- Reference-derived static calibration handles level without changing note
  envelopes; the real Volume pot remains after the circuit.

## Next Steps

All current guitar fuzz pedals with local schematics have been reworked. The
latest component pass routes diode/transistor junction clipping through
`vst/src/_shared/semiconductors.hpp` where component PDFs are available. Leave
`BassFuzz` for the bass pass unless asked otherwise.

## Validation Checklist Per Fuzz

- Core compiles without DPF.
- Expose the real pedal controls from the schematic. Rocksmith knobs only map
  into those controls for preset compatibility.
- Sweep 48 kHz core at Gain/Fuzz 0, .25, .5, .75, 1.0: no NaN/Inf, peak under
  1.0 before wrapper makeup.
- Build VST3 using temp build dirs, not `/build`.
- Install macOS binary into `rig_builder/vst/pedals/<Bundle>.vst3/Contents/MacOS`.
- `codesign --force --deep --sign -` and `codesign --verify --deep --strict`.
- Update `rs_knob_to_vst_param.json` only when real panel controls differ from
  the previous generic mapping.
