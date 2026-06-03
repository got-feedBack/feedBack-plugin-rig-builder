# RangeBooster - Rangemaster-style treble booster (bundled VST3)

Small one-knob VST for Rocksmith's `Pedal_RangeBooster`.

It keeps the Rocksmith control:

- `Boost`: emphasized frequency range and transistor color.

The local `pedals/range booster.png` schematic is a Rangemaster Treble Booster:
single OC44 transistor, small input capacitor, bright voicing, and one Boost
pot. This implementation is not a SPICE clone; it keeps the rig-slot cues that
matter: treble-focused input filtering, germanium-like asymmetric rounding, and
near-unity output so Rocksmith clean chains do not turn into accidental drive
presets.

## Build (macOS arm64)

```sh
make DPF_PATH=/private/tmp/DPF \
  DPF_BUILD_DIR=/private/tmp/dpf-build/RangeBooster \
  DPF_TARGET_DIR=/private/tmp/dpf-bin \
  PKG_CONFIG=false vst3
codesign --force --sign - /private/tmp/dpf-bin/RangeBooster.vst3
```

Copy `RangeBooster.vst3` to `rig_builder/vst/`.
