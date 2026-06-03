# MultiPitch

Bundled DPF VST3 for Rocksmith `Pedal_MultiPitch`.

The local reference is `pedals/multipitch.pdf`, an MF-102 style ring modulator
schematic with input drive, carrier oscillator, carrier null trim, LFO and mix.
Rocksmith exposes `Pitch1`, `Tone`, and `Mix`; `Pitch1` is treated as carrier
pitch, `Tone` controls carrier/LFO brightness, and `Mix` blends the balanced
modulator output.
