# BZ-1 - Chief-style silicon fuzz (bundled VST3)

Small two-knob VST for Rocksmith's `Pedal_FuzzWasHe`.

It models only the controls Rocksmith exposes:

- `Gain`: fuzz amount into the middle silicon transistor gain stage.
- `Tone`: dark/bright balance after the fuzz core.

The local `pedals/Fuzz Was He.pdf` schematic is used as the character
reference: silicon transistor stages, a classic fuzz core, and a muff-style
tone network. This implementation is not a SPICE clone; it keeps the audible
cues needed in a Rocksmith rig slot while omitting the pedal's Volume control.

## Build (macOS arm64)

```sh
make DPF_PATH=/private/tmp/DPF \
  DPF_BUILD_DIR=/private/tmp/dpf-build/BZ1 \
  DPF_TARGET_DIR=/private/tmp/dpf-bin \
  PKG_CONFIG=false vst3
codesign --force --sign - /private/tmp/dpf-bin/BZ1.vst3
```

Copy `BZ-1.vst3` to `rig_builder/vst/`.
