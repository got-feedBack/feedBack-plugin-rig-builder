# TW26

Bundled VST3 amp for Rocksmith `Amp_TW26`, based on the local Fender
57 Deluxe / 5E3 schematic.

Rocksmith exposes `Gain`, `Bass`, `Mid`, `Treble`, and `Pres`. The real
amp has two interactive channel volumes and one Tone control, so the plugin
keeps the Rocksmith contract while translating the extra tone knobs into the
5E3 voicing:

- `Gain` drives the 12AY7 preamp and cathode-biased 6V6 power section.
- `Bass` changes the loose low-end/coupling behavior.
- `Mid` controls the tweed mid body instead of a blackface-style scoop.
- `Treble` follows the single 5E3 Tone control.
- `Pres` adds or removes power-amp/speaker bite, since the schematic has no
  dedicated presence pot.

Reference:

- `/Users/nacho/Files/slopsmith/amps/Fender Deluxe (TW26)/Fender-57-Deluxe-Schematic.pdf`
