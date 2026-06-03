# TW40

Bundled VST3 amp for Rocksmith `Amp_TW40`, based on the local Fender
Bassman 5F6-A schematic.

Rocksmith exposes `Gain`, `Bass`, `Mid`, `Treble`, and `Pres`. The plugin
keeps that contract:

- `Gain` drives the 12AY7/12AX7 preamp and 5881-style power section.
- `Bass`, `Mid`, and `Treble` feed a 5F6-A FMV tone stack derived from the
  same Yeh and Smith Bassman model used by the bundled AmpEQ.
- `Pres` follows the Bassman negative-feedback presence circuit and upper
  speaker bite.

Reference:

- `/Users/nacho/Files/slopsmith/amps/Fender Bassman Tweed (TW40)/Fender_bassman_5f6a.pdf`
