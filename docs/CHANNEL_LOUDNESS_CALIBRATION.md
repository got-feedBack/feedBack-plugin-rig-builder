# Amp loudness calibration

Rig Builder keeps amp character and perceived output level as separate concerns.
The VST receives the requested gain, channel, mode and master values unchanged.
A clean unit-impulse stage immediately after the amp applies the measured static
trim, targeting `-12 LUFS`. The final leveler handles only the remaining dynamic
crest and program-dependent variation.

## Measurement

`tools/measure_amp_loudness.py` compiles each amp source into an offline probe.
The probe uses a fixed multitone input and records BS.1770-4 integrated loudness
at the plugin defaults and across 11 positions of every relevant gain and volume
control. Results are stored in `data/amp_loudness_model.json`.

Run all amps:

```sh
python3 tools/measure_amp_loudness.py
```

Run a subset:

```sh
python3 tools/measure_amp_loudness.py dsl100 dual_rect tw22
```

## Channel profiles

Multichannel guitar amps are listed explicitly in `CHANNEL_PROFILES`. Each entry
fixes the real channel and mode selectors, sweeps that channel's own gain and
level pots, and stores the plugin's selector defaults. Runtime selection uses an
exact measured profile for switch positions, including states that omit default
selectors. Intermediate selector morphs use the whole-amp curve so trim does not
snap while a song morphs between channels.

The post-amp trim is clamped to `-24..+24 dB`. This prevents near-muted gain-pot
positions from turning residual signal or noise into a large output. It does not
change saturation, bias, tone-stack response or power-amp behavior.

Bass amps are intentionally excluded from the explicit guitar-channel table.
