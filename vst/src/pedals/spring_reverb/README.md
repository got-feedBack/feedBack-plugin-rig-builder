# SpringReverb

Bundled VST3 for Rocksmith `Pedal_SpringReverb`.

Reference: local `pedals/Spring Reverb.png`, an EHX Holy Grail-style circuit
with op-amp input/output stages, dry/effect blend, and a digital reverb block.

Implemented controls match Rocksmith:

- `Time`: spring tank decay.
- `Mix`: dry/wet blend.
- `Depth`: dwell, drip, and resonant spring intensity.

The dry path is not clipped or saturated. Spring drive happens only inside the
virtual tank, with output compensated so clean Rocksmith rigs stay clean when
the reverb is engaged. Large Rocksmith `Mix` values keep most of the dry guitar
present and add the tank instead of turning the pedal into a quiet wet-only
crossfade.
