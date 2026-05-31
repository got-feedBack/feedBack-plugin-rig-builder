# BuzzOne

Bundled DPF/VST3 fuzz for Rocksmith `Pedal_BuzzOne`.

Reference: local `pedals/buzz 1.gif`, a Univox Super-Fuzz schematic with
2SC828 transistor stages, phase-split full-wave/octave fuzz, OA90 diode
clipping, and the passive tone/balance network. Rocksmith exposes only:

- `Gain`: drive into the octave fuzz core.
- `Tone`: continuous version of the Super-Fuzz tone switch / balance voicing.

Build:

```sh
make DPF_PATH=/private/tmp/DPF \
  DPF_BUILD_DIR=/private/tmp/dpf-build/BuzzOne \
  DPF_TARGET_DIR=/private/tmp/dpf-bin
codesign --force --sign - /private/tmp/dpf-bin/BuzzOne.vst3
```
