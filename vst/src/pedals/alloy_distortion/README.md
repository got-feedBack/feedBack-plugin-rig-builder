# AlloyDistortion - HM-2 style metal distortion for Rocksmith

Bundled VST3 for Rocksmith's `Pedal_MetalDistortion`.

Rocksmith exposes:

- `Gain`
- `Tone`

The local reference is `/Users/nacho/Files/slopsmith/pedals/alloy distortion.pdf`,
which documents the Boss HM-2 Heavy Metal: asymmetric soft clipping, hard
clipping, germanium crossover distortion, and two Color Mix tone controls. Since
Rocksmith exposes only `Gain` and `Tone`, this plugin fixes the hidden level and
uses `Tone` as a combined low/high color blend.

## Build

```sh
make DPF_PATH=/private/tmp/DPF \
  DPF_BUILD_DIR=/private/tmp/dpf-build/AlloyDistortion \
  DPF_TARGET_DIR=/private/tmp/dpf-bin
codesign --force --sign - /private/tmp/dpf-bin/AlloyDistortion.vst3
```
