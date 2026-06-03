# LineDrive - OS-2 style drive for Rocksmith

Small bundled VST3 for Rocksmith's `Pedal_LineDrive`.

Rocksmith exposes:

- `Gain`
- `Tone`

The local reference is `/Users/nacho/Files/slopsmith/pedals/line drive.png`,
which is a Boss OS-2 schematic: buffered input/output stages, two clipping
paths, a Color blend, Tone, and Level. Rocksmith does not expose Color or
Level, so this plugin fixes Color internally toward a modern drive/distortion
blend and normalizes output internally.

## Build

```sh
make DPF_PATH=/private/tmp/DPF \
  DPF_BUILD_DIR=/private/tmp/dpf-build/LineDrive \
  DPF_TARGET_DIR=/private/tmp/dpf-bin
codesign --force --sign - /private/tmp/dpf-bin/LineDrive.vst3
```
