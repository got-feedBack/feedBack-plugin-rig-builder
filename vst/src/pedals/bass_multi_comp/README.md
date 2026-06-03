# BassMultiComp

Bundled VST3 for Rocksmith's `Bass_Pedal_MBComp`.

The reference schematic is the local `multi bass comp.webp`, an EBS MultiComp
2-style bass compressor with split/envelope control paths and a band-aware
compression sound.

Rocksmith exposes:

- `Compress`
- `Filter`
- `Rate`

This implementation keeps the Rocksmith surface intact: `Filter` moves the
low/high crossover, `Compress` changes threshold/ratio, and `Rate` controls
envelope recovery.
