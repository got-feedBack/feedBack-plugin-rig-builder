# SuperDrive - SD-1 style overdrive for Rocksmith

Small bundled VST3 for Rocksmith's `Pedal_SuperDrive`.

Rocksmith exposes:

- `Gain`
- `Tone`

The local reference is `/Users/nacho/Files/slopsmith/pedals/super drive.pdf`,
which documents the Boss SD-1 Super OverDrive: input/output buffers, uPC4558
op-amp, asymmetric diode clipping in the feedback path, and a post-clipping tone
network. The real pedal has a Level knob; Rocksmith does not expose it for this
gear, so this implementation normalizes output internally.

## Build

```sh
make DPF_PATH=/private/tmp/DPF \
  DPF_BUILD_DIR=/private/tmp/dpf-build/SuperDrive \
  DPF_TARGET_DIR=/private/tmp/dpf-bin
codesign --force --sign - /private/tmp/dpf-bin/SuperDrive.vst3
```
