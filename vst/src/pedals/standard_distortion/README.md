# StandardDistortion - DS-1 style distortion for Rocksmith

Bundled VST3 for Rocksmith's `Pedal_Distortion`.

Rocksmith exposes:

- `Gain`
- `Tone`

The local reference is `/Users/nacho/Files/slopsmith/pedals/standard distortion.pdf`,
which documents the Boss DS-1 Distortion: transistor input/output buffers,
TA7136-style gain stage, diode hard clipping, and the familiar post-clipping
Tone/Level network. The real pedal has a Level knob; Rocksmith does not expose
it here, so the plugin compensates output internally.

## Build

```sh
make DPF_PATH=/private/tmp/DPF \
  DPF_BUILD_DIR=/private/tmp/dpf-build/StandardDistortion \
  DPF_TARGET_DIR=/private/tmp/dpf-bin
codesign --force --sign - /private/tmp/dpf-bin/StandardDistortion.vst3
```
