# NpnDelay

Bundled DPF/VST3 delay for Rocksmith `Pedal_NPNDelay`.

Reference: local `pedals/classic npn delay.pdf`, a Boss DM-2 style analog
delay schematic with NE570 companding, MN3005/MN3205 BBD delay, dark repeat
filtering, NPN switching/buffer stages, and a short 20-300 ms reference range.
Some Rocksmith presets use longer Time values, so the model accepts up to
about 420 ms while keeping the repeats dark and compressed. Rocksmith exposes
only:

- `Time`: delay time in milliseconds, stored as `ms / 2000` in the VST state.
- `Feedback`: repeat regeneration / intensity.
- `Mix`: echo level.

The model keeps the wet path mono like the original DM-2 while preserving the
dry stereo input path.

Build:

```sh
make DPF_PATH=/private/tmp/DPF \
  DPF_BUILD_DIR=/private/tmp/dpf-build/NpnDelay \
  DPF_TARGET_DIR=/private/tmp/dpf-bin
codesign --force --sign - /private/tmp/dpf-bin/NpnDelay.vst3
```
