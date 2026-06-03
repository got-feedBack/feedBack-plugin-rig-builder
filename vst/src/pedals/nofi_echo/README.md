# NoFiEcho

Bundled DPF/VST3 delay for Rocksmith `Pedal_NoFiEcho`.

Reference: local `pedals/nofi echo.png`, an Ibanez DE7-style stereo
delay/echo schematic. The real pedal has mode/range switching around a digital
delay IC and stereo output conditioning; Rocksmith exposes only:

- `Time`: delay time in milliseconds, stored as `ms / 2000` in the VST state.
- `Feedback`: regeneration amount.
- `Mix`: echo level.

The plugin fixes the hidden controls to an "Echo" voice: darker lo-fi repeats,
light clock wobble, modest companding, and a stereo spread/crossfeed path.

Build:

```sh
make DPF_PATH=/private/tmp/DPF \
  DPF_BUILD_DIR=/private/tmp/dpf-build/NoFiEcho \
  DPF_TARGET_DIR=/private/tmp/dpf-bin
codesign --force --sign - /private/tmp/dpf-bin/NoFiEcho.vst3
```
