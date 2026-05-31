# AutoSweep — envelope filter / auto-wah (bundled VST3)

Source for the AutoSweep plugin bundled at `../AutoSweep.vst3`. Built with
[DPF](https://github.com/DISTRHO/DPF). DSP ported from an ESP32 envelope-filter
sketch (class `QTron`). 8 params: Mode (LP/BP/HP), Attack, Release, Range,
Peak, Mix, Gain, Boost. Envelope follower drives a biquad whose cutoff sweeps
with playing dynamics.

## Build (macOS, arm64)
```
git clone https://github.com/DISTRHO/DPF.git
(cd DPF && git submodule update --init --recursive)   # pugl, needed for the UI
# put these sources in a folder beside DPF (Makefile uses ../DPF/...), then:
make vst3
codesign --force --sign - bin/AutoSweep.vst3          # ad-hoc; local builds aren't quarantined
```
Drop `AutoSweep.vst3` into this plugin's `vst/` folder. The engine loads it by
absolute path (no system install needed).
