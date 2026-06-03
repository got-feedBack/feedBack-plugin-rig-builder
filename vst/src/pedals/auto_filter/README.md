# AutoFilter

Bundled DPF/VST3 envelope filter for Rocksmith `Pedal_AutoFilter`.

Reference: local `pedals/auto filter.gif` and `pedals/auto filter_2.gif`,
both based on the Mu-Tron III / Neutron style circuit: TL07x input and filter
stages, envelope follower, LED/LDR sweep cells, range/mode switches, and peak
control.

Rocksmith exposes:

- `FilterType`: mode selector. The mapping keeps the old 0/1/2 convention:
  low-pass, band-pass, high-pass.
- `Res`: peak/resonance.
- `Sens`: input detector sensitivity and sweep range.
- `Attack`: envelope attack time.
- `Release`: envelope release time.

The hardware gain/range/direction trims are folded into fixed internal
calibration so the Rocksmith control surface stays unchanged.

Build:

```sh
make -C vst/src/auto_filter DPF_PATH=/private/tmp/DPF \
  DPF_BUILD_DIR=/private/tmp/dpf-build/AutoFilter \
  DPF_TARGET_DIR=/private/tmp/dpf-bin
codesign --force --sign - /private/tmp/dpf-bin/AutoFilter.vst3
```

Copy `AutoFilter.vst3` to `rig_builder/vst/`.
