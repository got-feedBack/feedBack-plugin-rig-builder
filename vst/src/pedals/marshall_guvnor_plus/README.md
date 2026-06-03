# MarshallGuvnorPlus - GV-2 style drive for Rocksmith

Bundled VST3 for Rocksmith's `Pedal_MarshallGuvnorPlus`.

Expected Rocksmith controls:

- `Gain`
- `Bass`
- `Mid` / `Middle`
- `Treble`
- `Deep`

The local references are `/Users/nacho/Files/slopsmith/pedals/Marshall GV2_1.png`
and `/Users/nacho/Files/slopsmith/pedals/marshall gv2_2.gif`. They show the
Marshall GV-2/Guv'nor Plus topology: TL072 gain stages, LED/diode clipping,
passive Marshall-style tone stack, and a Deep low-end control. The real Volume
control is not modeled as a Rocksmith knob; output is compensated internally.

## Build

```sh
make DPF_PATH=/private/tmp/DPF \
  DPF_BUILD_DIR=/private/tmp/dpf-build/MarshallGuvnorPlus \
  DPF_TARGET_DIR=/private/tmp/dpf-bin
codesign --force --sign - /private/tmp/dpf-bin/MarshallGuvnorPlus.vst3
```
