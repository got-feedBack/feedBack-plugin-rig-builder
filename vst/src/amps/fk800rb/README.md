# Freddy Krueger 800BR — bundled VST3 (bass head)

A parody-named clone of the **Gallien-Krueger 800RB** bass head, bundled at
`../FreddyKrueger800BR.vst3`. Built with [DPF](https://github.com/DISTRHO/DPF).
DSP and panel are modeled from the **GK 800RB Service Manual** (preamp sheet
`406-0045` "Bob Gallien 800 RB Preamp", power amp `406-0044`, plus the Operators
Manual block diagram & specs) — *not* from any other plugin; the file layout
just follows the sibling `eden_wtdi` amp as a DPF scaffold.

## Signal chain (faithful to the manual)
`Input(+ −10dB pad) → Volume (preamp drive / GK growl) → Voicing Filters
→ 4-band Active EQ → Boost → Electronic Crossover → Master Volumes (300W/100W)`

| Block | Manual reference | Model |
|---|---|---|
| Input pad | −10 dB, sens 2mV→6mV, headroom 1V→3V rms | scales preamp drive ×0.316 |
| Volume / growl | LF353 input stage + D1/D2 clip | level-preserving `tanh` soft-clip |
| Lo Cut | "bass roll-off … stage rumble" | high-pass 110 Hz |
| Mid Contour | "notch at ~500 Hz, mellow round" | peak −11 dB @ 500 Hz |
| Hi Boost | "adds edge and definition" | high shelf +6.5 dB @ 2.2 kHz |
| Bass | boost/cut @ 60 Hz | low shelf ±15 dB |
| Lo-Mid | boost/cut @ 250 Hz | peak ±15 dB |
| Hi-Mid | boost/cut @ 1 kHz | peak ±15 dB |
| Treble | boost/cut @ 4 kHz | high shelf ±15 dB |
| Boost | footswitch preset, +15 dB max | gain + soft-clip when pushed |
| Crossover | SVF 100 Hz–1.04 kHz, Full/Bi-Amp | 4th-order LR-ish LP/HP split |
| Masters | 300W (low) + 100W (high) | per-band gain (bi-amp) / combined (full) |

15 params: 9 knobs (Volume, Treble, Hi-Mid, Lo-Mid, Bass, Boost level,
Crossover, 100W Amp, 300W Amp) + 6 switches (−10 dB, Lo Cut, Mid Contour,
Hi Boost, Boost On, Bi-Amp). Tone knobs flat at 0.5; crossover 0.5 ≈ 500 Hz.

## Build (macOS, arm64)
The build workspace must be a **space-free path** (DPF's make can't handle the
space in "Application Support"). The originals are built under
`~/Documents/Arduino/Envelope/vst-build/`:
```
cd ~/Documents/Arduino/Envelope/vst-build/pedals/fk800rb
WB=~/Documents/Arduino/Envelope/vst-build
make vst3 DPF_PATH=../../DPF DPF_BUILD_DIR="$WB/build/FreddyKrueger800BR" DPF_TARGET_DIR="$WB/bin"
codesign --force --sign - "$WB/bin/FreddyKrueger800BR.vst3"   # ad-hoc
cp -R "$WB/bin/FreddyKrueger800BR.vst3" <plugin>/vst/         # drop into bundle
```
The Slopsmith engine loads it by absolute path from `vst/` (no system install).
A standalone DSP stability sweep (2880 param combos) reports 0 non-finite
samples; flat-setting gain ≈ 1.26.
