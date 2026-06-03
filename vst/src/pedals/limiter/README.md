# Limiter

Bundled DPF/VST3 limiter for Rocksmith `Pedal_Limiter`.

Rocksmith exposes only:

- `Limit`: limiter amount / threshold.
- `Rate`: recovery speed.

The local `pedals/limiter.png` reference appears to be a BBD delay schematic,
so this plugin models the Rocksmith-facing limiter behavior instead of copying
that circuit. The DSP uses a stereo-linked peak detector, fast gain reduction,
soft knee, modest makeup, and a soft ceiling.

Build:

```sh
make DPF_PATH=/private/tmp/DPF \
  DPF_BUILD_DIR=/private/tmp/dpf-build/Limiter \
  DPF_TARGET_DIR=/private/tmp/dpf-bin
codesign --force --sign - /private/tmp/dpf-bin/Limiter.vst3
```
