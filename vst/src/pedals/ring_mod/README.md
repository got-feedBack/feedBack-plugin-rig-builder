# RingMod

Bundled DPF/VST3 ring modulator for Rocksmith `Pedal_RingMod`.

References:

- `pedals/ring mod.gif`: Oberheim/Maestro RM-1A schematic with MC1495
  balanced modulator, oscillator, squelch, and null trims.
- `pedals/ring mod_2.png`: RM-1A stripboard layout.

Rocksmith exposes:

- `Depth`: ring-mod wet amount and carrier injection.
- `Waveform`: oscillator shape, from rounded sine to sharper square.
- `Sensitivity`: squelch/envelope sensitivity and carrier range.
- `Attack`: envelope and carrier slew time.

The hardware volume, null trims, offset, pitch and range trims are folded into
fixed internal calibration so the Rocksmith control surface stays unchanged.

Build:

```sh
make -C vst/src/ring_mod DPF_PATH=/private/tmp/DPF \
  DPF_BUILD_DIR=/private/tmp/dpf-build/RingMod \
  DPF_TARGET_DIR=/private/tmp/dpf-bin
codesign --force --sign - /private/tmp/dpf-bin/RingMod.vst3
```

Copy `RingMod.vst3` to `rig_builder/vst/`.
