# ValveEcho

Bundled DPF/VST3 delay for Rocksmith `Pedal_ValveEcho`.

Reference: local `pedals/valveecho_1.png` and `pedals/valveecho_2.png`,
Binson Echorec PE603T valve/magnetic drum schematics. The original circuit has
ECC83/ECC82 tube stages, multiple playback heads, feedback/repeat switching and
dark magnetic media loss. Rocksmith exposes only:

- `Time`: delay time in milliseconds, stored as `ms / 2000` in the VST state.
- `Feedback`: repeat regeneration.
- `Mix`: wet echo level.

Hidden Echorec controls are fixed internally to a usable multi-head voice with
tube saturation, head spread, wow/flutter and dark regenerating repeats.

Build:

```sh
make DPF_PATH=/private/tmp/DPF \
  DPF_BUILD_DIR=/private/tmp/dpf-build/ValveEcho \
  DPF_TARGET_DIR=/private/tmp/dpf-bin
codesign --force --sign - /private/tmp/dpf-bin/ValveEcho.vst3
```
