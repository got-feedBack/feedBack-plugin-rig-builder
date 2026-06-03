# Octavius

Bundled DPF VST3 for Rocksmith `Pedal_Octavius`.

The local reference is `pedals/octavius.pdf`, a Boss OC-2 style octave-down
pedal with direct, octave 1, and octave 2 level controls. Rocksmith exposes
only `Tone` and `Mix`, so the plugin keeps the tracking and sub-octave balance
inside the effect: `Mix` blends the generated octave-down voice and `Tone`
brightens the octave voice while shifting weight from octave 2 toward octave 1.
