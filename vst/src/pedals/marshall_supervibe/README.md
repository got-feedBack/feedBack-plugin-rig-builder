# MarshallSupervibe

Bundled DPF/VST3 for Rocksmith `Pedal_MarshallSupervibe`.

Reference: local `pedals/marshall super vibe.pdf`, a Marshall SV-1/Supervibe
schematic with TL072 stages, MN3007 BBD delay, MN3101 clock, and direct/delay
mixing. Rocksmith exposes:

- `Rate`: modulation speed.
- `Depth`: BBD delay sweep width.
- `Mix`: direct/delay blend.
- `Wave`: LFO shape and sharper vibe/rotary character.

The plugin keeps both channels phase-linked so it behaves like a mono pedal in
the Rocksmith chain instead of an auto-pan.

Build:

```sh
make DPF_PATH=/private/tmp/DPF \
  DPF_BUILD_DIR=/private/tmp/dpf-build/MarshallSupervibe \
  DPF_TARGET_DIR=/private/tmp/dpf-bin
codesign --force --sign - /private/tmp/dpf-bin/MarshallSupervibe.vst3
```
