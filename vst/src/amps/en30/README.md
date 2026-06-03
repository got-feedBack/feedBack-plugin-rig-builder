# EN30

Bundled DPF/VST3 AC30 Top Boost-style amp for Rocksmith `Amp_EN30`.

Reference schematics:

- `/Users/nacho/Files/slopsmith/amps/vox ac30 (en30)/ac30-60-02-iss5.pdf`
- `/Users/nacho/Files/slopsmith/amps/vox ac30 (en30)/Vox_ac30cc2_ac30cc2x_2005_sm.pdf`
- `/Users/nacho/Files/slopsmith/amps/vox ac30 (en30)/Vox_ac30c2.pdf`

Rocksmith exposes `Gain`, `Bass`, `Mid`, `Treble`, `Pres`, and `Bright`. The
AC30 reference has no Mid pot, so `Mid` is implemented as a post-tone-stack
body/cut control while preserving the Rocksmith knob contract. `Pres` behaves
like the inverse of the AC30 cut/presence area, and `Bright` switches in the
brilliant/top-boost voicing.

Build:

```sh
make DPF_PATH=/private/tmp/DPF \
  DPF_BUILD_DIR=/private/tmp/dpf-build/EN30 \
  DPF_TARGET_DIR=/private/tmp/dpf-bin
codesign --force --sign - /private/tmp/dpf-bin/EN30.vst3
```
