# OctaveUp - clean octave-up pedal (bundled VST3)

Small two-knob VST for Rocksmith's `Pedal_OctaveUp`.

References:
- `pedals/octavup.png`: Octron-style octave-up reference.
- `pedals/octaveup_2.png`: EQD Tentacle-style phase-split diode rectifier.

The wet path is a fixed 2x phase-vocoder pitch shifter. The Tentacle/Octron
references still inform the two-path dry/octave topology, but the main
Rocksmith behavior is kept clean so the octave reads as pitch rather than
rectifier distortion.

It keeps the Rocksmith controls:

- `Tone`: octave-up brightness.
- `Mix`: true dry/octave blend, without makeup gain.

The local `pedals/octavup.png` reference is based on a Foxrox Octron-style
analog octave circuit. This is not a full circuit clone; the DSP keeps the
Rocksmith-facing behavior needed in a rig slot: a buffered dry path, a clean
octave-up wet path, and tone shaping so low settings are smoother while high
settings get brighter.

## Build (macOS arm64)

```sh
make DPF_PATH=/private/tmp/DPF \
  DPF_BUILD_DIR=/private/tmp/dpf-build/OctaveUp \
  DPF_TARGET_DIR=/private/tmp/dpf-bin \
  PKG_CONFIG=false vst3
codesign --force --sign - /private/tmp/dpf-bin/OctaveUp.vst3
```

Copy `OctaveUp.vst3` to `rig_builder/vst/`.
