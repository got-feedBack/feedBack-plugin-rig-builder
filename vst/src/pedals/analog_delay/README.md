# AnalogDelay

Bundled DPF/VST3 delay for Rocksmith `Pedal_AnalogueDelay`.

Reference: local `pedals/analog delay.pdf`, Moog MF-104/MF-104Z schematics
with SA572 companding, chained MN3008 BBD delay chips, dark loop filtering,
drive, feedback, and VCA mixing. Rocksmith exposes only:

- `Time`: delay time in milliseconds, stored as `ms / 2000` in the VST state.
- `Feedback`: regeneration amount.
- `Mix`: wet level.

The model keeps the delay mono like the original pedal while preserving the dry
stereo input path.

Build:

```sh
make DPF_PATH=/private/tmp/DPF \
  DPF_BUILD_DIR=/private/tmp/dpf-build/AnalogDelay \
  DPF_TARGET_DIR=/private/tmp/dpf-bin
codesign --force --sign - /private/tmp/dpf-bin/AnalogDelay.vst3
```
