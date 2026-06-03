# LoFiFilter

Bundled VST3 for Rocksmith `Pedal_LoFiFilter`.

Reference: local `pedals/lofi filter.pdf`, PedalPCB Lofinator. The schematic
uses op-amp drive, diode clipping, two OTA filter stages, and `Lo`/`Hi`
cutoff controls.

Implemented controls match Rocksmith:

- `FilterType`: moves the Lo/Hi filter window from dark low-pass to brighter
  narrow-band lo-fi filtering.
- `Mix`: controls effect intensity: drive, resonance, lo-fi texture, and wet
  balance.

Version 1.0.1 trims the internal drive and wet/output gain so high Rocksmith
`Mix` values do not behave like an output boost in clean chains.
