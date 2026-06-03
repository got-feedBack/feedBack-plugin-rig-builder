# NoiseGate

Bundled DPF/VST3 noise gate for Rocksmith `Pedal_NoiseGate`.

Reference: local ET-45C / default gate schematic with Sens and Decay controls.
Rocksmith exposes only `Thresh` and `Rate`, so the DSP keeps the circuit-level
idea of a sidechain detector driving a gain cell while mapping those two knobs:

- `Thresh`: normalized from Rocksmith dB values, about -100 dB to -50 dB.
- `Rate`: gate aggressiveness; higher values close faster and with more range.

Build:

```sh
make DPF_PATH=/private/tmp/DPF \
  DPF_BUILD_DIR=/private/tmp/dpf-build/NoiseGate \
  DPF_TARGET_DIR=/private/tmp/dpf-bin
codesign --force --sign - /private/tmp/dpf-bin/NoiseGate.vst3
```
