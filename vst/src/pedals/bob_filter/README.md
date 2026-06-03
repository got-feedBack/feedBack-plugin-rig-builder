# BobFilter

Bundled VST3 for Rocksmith's `Pedal_BobFilter`.

The reference schematic is the local `bob filer (analog filter).pdf`, a Moog
MF-105/MuRF-style analog filter architecture with an input stage, direct VCA,
and eight filter/VCA cells controlled from an envelope/control section.

Rocksmith exposes:

- `Sens`
- `Attack`
- `Release`
- `Mix`
- `Filter`

This implementation keeps that surface: a dynamic envelope drives an 8-band
resonant filter bank plus a sweep voice. `Filter` selects the sweep direction /
voicing.
