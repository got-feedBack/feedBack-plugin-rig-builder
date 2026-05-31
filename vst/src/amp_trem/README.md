# AmpTrem

Bundled DPF/VST3 optical amp tremolo for Rocksmith `Pedal_AmpTrem`.

Reference: local `pedals/amp trem.gif`, a Demeter Tremulator-style circuit:
op-amp LFO, LED/LDR optocoupler, TL061 audio stage, and output `Depth` control.
Rocksmith exposes only:

- `Speed`: LFO rate.
- `Depth`: opto gain-reduction depth.

Build:

```sh
make DPF_PATH=/private/tmp/DPF \
  DPF_BUILD_DIR=/private/tmp/dpf-build/AmpTrem \
  DPF_TARGET_DIR=/private/tmp/dpf-bin
codesign --force --sign - /private/tmp/dpf-bin/AmpTrem.vst3
```
