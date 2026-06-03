# SendInTheClones

Bundled DPF/VST3 chorus/doubler for Rocksmith `Pedal_SendInTheClones`.

Reference: local `pedals/send in the clones.png`, an Electro-Harmonix Clone
Theory layout with MN3007 BBD modulation. Rocksmith exposes only:

- `Clones`: number/spread of the doubled voices.
- `Depth`: modulation depth.
- `Mix`: dry/wet blend.

Rate and chorus/vibrato mode are fixed internally because Rocksmith does not
send those controls for this pedal.

Build:

```sh
make DPF_PATH=/private/tmp/DPF \
  DPF_BUILD_DIR=/private/tmp/dpf-build/SendInTheClones \
  DPF_TARGET_DIR=/private/tmp/dpf-bin
codesign --force --sign - /private/tmp/dpf-bin/SendInTheClones.vst3
```
