# Buzz-Tone - three-transistor germanium fuzz (bundled VST3)

Small two-knob VST for Rocksmith's `Pedal_CaptFuzzle`.

It models only the controls Rocksmith exposes:

- `Gain`: input push into the starved germanium fuzz core.
- `Tone`: post-fuzz brightness.

The local schematic in `pedals/captain fuzzle.gif` is used as the character
reference: three 2N1305 germanium stages, 1.5 V supply, coupling caps, and a
fixed output level. This is not a SPICE clone; it keeps the audible cues that
matter in a Rocksmith rig slot: thin input/output coupling, asymmetric
germanium clipping, low-headroom compression, and a bright/splatty fuzz voice.

## Build (macOS arm64)

```sh
make DPF_PATH=/private/tmp/DPF \
  DPF_BUILD_DIR=/private/tmp/dpf-build/BuzzTone \
  DPF_TARGET_DIR=/private/tmp/dpf-bin \
  PKG_CONFIG=false vst3
codesign --force --sign - /private/tmp/dpf-bin/BuzzTone.vst3
```

Copy `BuzzTone.vst3` to `rig_builder/vst/Buzz-Tone.vst3`.
