# AcousticSimulator

Bundled VST3 for Rocksmith's `Pedal_AcousticEmulator`.

The reference schematic is the local `acoustic simulator.png`: a blue-button
acoustic simulator with input conditioning, a mild FET/diode voice, and several
active EQ stages for body, mid, top, and output shaping.

Rocksmith exposes four knobs:

- `Tone`
- `MidShift`
- `Body`
- `Mid`

This implementation keeps the pedal clean and focuses on acoustic-style EQ:
low body resonance, electric-pickup mid shaping, adjustable mid center, and
top-end air.
