# CustomDrive

Bundled VST3 for Rocksmith's `Pedal_CustomDrive`.

The reference schematic is the local `custom drive.png`: op-amp pre-gain,
MOSFET/diode clipping, a voice switch, and a passive tone/output network.

Rocksmith exposes:

- `Gain`
- `Tone`
- `Voice`

There is no Rocksmith volume knob for this gear, so the output is internally
trimmed to stay near unity.
