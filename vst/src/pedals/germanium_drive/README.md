# GermaniumDrive - smooth germanium overdrive (bundled VST3)

Small two-knob VST for Rocksmith's `Pedal_GermaniumDrive`.

It intentionally models only the controls Rocksmith exposes:

- `Gain`: silicon preamp push + asymmetric germanium-style saturation.
- `Tone`: post-drive brightness control.

The Skywave/Hudson Broadcast schematic in `pedals/germanium drive.pdf` is used
as a character reference, but the implementation is not a full circuit clone.
Rocksmith describes the pedal as "a classic smooth overdrive", so this plugin
keeps the fixed input low cut, germanium asymmetry, and subtle transformer-like
rounding without exposing Voltage, Low Cut, Level, or Gain Mode.

## Build (macOS arm64)

```sh
git clone https://github.com/DISTRHO/DPF.git ../DPF
(cd ../DPF && git submodule update --init --recursive)
make vst3
codesign --force --sign - bin/GermaniumDrive.vst3
```

Copy `bin/GermaniumDrive.vst3` to this plugin's `vst/` directory.
