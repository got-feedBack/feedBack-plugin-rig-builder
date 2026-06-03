# Tremolo

Bundled DPF/VST3 tremolo for Rocksmith `Pedal_Tremolo`.

Reference: local `pedals/tremolo.jpg`, a Colorsound Tremolo/Tremola version 1
schematic with a transistor audio stage and simple transistor LFO. Rocksmith
exposes:

- `Speed`: LFO rate.
- `Mix`: tremolo depth/intensity.

This is intentionally different from `AmpTrem` and `MultiTrem`: it has a
slightly lopsided transistor pulse, no stereo panning, and no waveform control.

Build:

```sh
make DPF_PATH=/private/tmp/DPF \
  DPF_BUILD_DIR=/private/tmp/dpf-build/Tremolo \
  DPF_TARGET_DIR=/private/tmp/dpf-bin
codesign --force --sign - /private/tmp/dpf-bin/Tremolo.vst3
```
