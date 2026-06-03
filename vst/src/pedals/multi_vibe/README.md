# MultiVibe

Bundled Rocksmith `Pedal_MultiVibe` implementation.

- Reference: `/Users/nacho/Files/slopsmith/pedals/multi vibe.jpg`
- Circuit target: Boss VB-2 style MN3207/MN3102 BBD vibrato
- Rocksmith knobs: `Speed`, `Mix`, `Waveform`

`Mix` is treated as the Rocksmith intensity control: it raises the wet BBD
vibrato and the delay modulation depth together. The output is phase-linked
stereo so centered guitar stays centered instead of turning into auto-pan.
