# Chorus

Bundled DPF/VST3 chorus for Rocksmith `Pedal_Chorus`.

Reference: local `pedals/chorus.pdf`, a Boss CE-2 schematic around the MN3007
BBD, MN3101 clock driver, TL022 LFO, and fixed dry/wet output blend.
Rocksmith exposes:

- `Rate`: LFO speed.
- `Depth`: BBD delay modulation width.
- `Mix`: extra dry/wet balance that Rocksmith adds on top of the CE-2 idea.

Build:

```sh
make DPF_PATH=/private/tmp/DPF \
  DPF_BUILD_DIR=/private/tmp/dpf-build/Chorus \
  DPF_TARGET_DIR=/private/tmp/dpf-bin
codesign --force --sign - /private/tmp/dpf-bin/Chorus.vst3
```
