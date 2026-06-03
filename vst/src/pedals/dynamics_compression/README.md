# DynamicsCompression - Dyna Comp-style pedal compressor (bundled VST3)

Small VST for Rocksmith's `Pedal_Compression`.

It keeps the Rocksmith controls:

- `Comp`: sustain/compression amount.
- `Attack`: envelope attack speed.
- `Release`: envelope release time.

The local `pedals/dynamics compression*.jpg` references are MXR Dyna Comp /
Dynacomp-style CA3080 OTA compressors. The DSP is not a component-level clone;
it keeps the useful pedal behavior for a Rocksmith rig slot: fast OTA-like
leveling, sustain as the Comp knob rises, fixed output makeup, and a little
transistor/OTA rounding.

## Build (macOS arm64)

```sh
make DPF_PATH=/private/tmp/DPF \
  DPF_BUILD_DIR=/private/tmp/dpf-build/DynamicsCompression \
  DPF_TARGET_DIR=/private/tmp/dpf-bin \
  PKG_CONFIG=false vst3
codesign --force --sign - /private/tmp/dpf-bin/DynamicsCompression.vst3
```

Copy `DynamicsCompression.vst3` to `rig_builder/vst/`.
