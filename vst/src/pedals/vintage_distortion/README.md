# VintageDistortion

Bundled DPF/VST3 distortion for Rocksmith `Pedal_VintageDistortion`.

Reference: local `pedals/vintage distortion.png`, a DOD 250 style circuit:
LM741 op-amp gain stage, feedback cap, passive output filtering and asymmetric
1N4148 clipping. The original has gain and output level; Rocksmith exposes:

- `Gain`: op-amp push into the diode clipper.
- `Tone`: fixed Rocksmith tone control layered onto the DOD 250 voice.

Output level is internally normalized because Rocksmith does not expose a
volume knob for this pedal.

Build:

```sh
make DPF_PATH=/private/tmp/DPF \
  DPF_BUILD_DIR=/private/tmp/dpf-build/VintageDistortion \
  DPF_TARGET_DIR=/private/tmp/dpf-bin
codesign --force --sign - /private/tmp/dpf-bin/VintageDistortion.vst3
```
