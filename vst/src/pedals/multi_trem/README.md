# MultiTrem

Bundled DPF/VST3 tremolo for Rocksmith `Pedal_MultiTrem`.

Reference: local `pedals/multi-trem.pdf`, a Boss TR-2 Tremolo schematic with
linear VCA control, Rate, Wave, and Depth. Rocksmith exposes:

- `Speed`: LFO rate.
- `Mix`: tremolo depth.
- `Waveform`: triangle/rounded/square family selector.

Build:

```sh
make DPF_PATH=/private/tmp/DPF \
  DPF_BUILD_DIR=/private/tmp/dpf-build/MultiTrem \
  DPF_TARGET_DIR=/private/tmp/dpf-bin
codesign --force --sign - /private/tmp/dpf-bin/MultiTrem.vst3
```
