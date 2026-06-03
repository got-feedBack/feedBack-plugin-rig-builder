# ShredZone - MT-2 style high-gain distortion for Rocksmith

Bundled VST3 for Rocksmith's `Pedal_ShredZone`.

Rocksmith exposes:

- `Gain`
- `Bass`
- `Mid`
- `Treble`

The local reference is `/Users/nacho/Files/slopsmith/pedals/shred zone.pdf`,
which documents the Boss MT-2 Metal Zone: dual-gain high-saturation clipping,
large low/mid voice, and an active EQ section. The real pedal also has Level
and semi-parametric mid frequency; Rocksmith does not expose those here, so this
plugin fixes the mid-frequency behavior internally and normalizes output.

`Bass`, `Mid`, and `Treble` are mapped from Rocksmith's `-7..+7` style values to
normalized plugin controls where `0.5` is neutral.

## Build

```sh
make DPF_PATH=/private/tmp/DPF \
  DPF_BUILD_DIR=/private/tmp/dpf-build/ShredZone \
  DPF_TARGET_DIR=/private/tmp/dpf-bin
codesign --force --sign - /private/tmp/dpf-bin/ShredZone.vst3
```
