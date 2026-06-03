# Big Buzz

Bundled DPF/VST3 fuzz for Rocksmith `Pedal_BuzzToo`.

Reference: local `pedals/buzz 2.jpg`, a triangle-era four-transistor fuzz
schematic with two silicon diode clipping stages, passive two-branch tone
stack, and output volume. Rocksmith exposes only:

- `Gain`: sustain/drive into the two clipping stages.
- `Tone`: continuous tone stack balance from thick/dark to bright.

The output volume is internally normalized because Rocksmith does not expose a
Big Buzz output knob.

Build:

```sh
make -C vst/src/big_buzz DPF_PATH=/private/tmp/DPF \
  DPF_BUILD_DIR=/private/tmp/dpf-build/BigBuzz \
  DPF_TARGET_DIR=/private/tmp/dpf-bin
codesign --force --sign - /private/tmp/dpf-bin/BigBuzz.vst3
```

Copy `Big Buzz.vst3` to `rig_builder/vst/`.
