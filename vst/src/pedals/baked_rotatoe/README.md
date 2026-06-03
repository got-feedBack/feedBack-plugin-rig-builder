# BakedRotatoe

Bundled DPF/VST3 rotary speaker for Rocksmith `Pedal_BakedRotatoe`.

Reference: local `pedals/baked rotatoe.pdf`, an R.G. Keen LERA Leslie effect
rotor adapter. The PDF is not a full audio-path pedal schematic; it models the
mechanical speed ramp of a rotating speaker using a 100uF capacitor, op-amp
buffer and LED/LDR speed control. This plugin uses that idea for the speed
inertia and models the audible Leslie/RT-20 cues in DSP:

- separated low drum and high horn bands,
- independent doppler delay, phase and tremolo,
- slow/fast ramping instead of instant LFO speed jumps,
- stereo horn spread with level compensation.

Rocksmith exposes:

- `Rate`: target rotor speed.
- `Depth`: doppler, tremolo and cabinet motion intensity.
- `Mix`: wet/dry blend.
- `Balance`: drum/horn balance.

Build:

```sh
make -C vst/src/baked_rotatoe DPF_PATH=/private/tmp/DPF \
  DPF_BUILD_DIR=/private/tmp/dpf-build/BakedRotatoe \
  DPF_TARGET_DIR=/private/tmp/dpf-bin
codesign --force --sign - /private/tmp/dpf-bin/BakedRotatoe.vst3
```

Copy `BakedRotatoe.vst3` to `rig_builder/vst/`.
