# DualRect

Bundled RigBuilder amp for Rocksmith's `Amp_CA100`.

Local reference:

- `amps/Dual Rectifier (Cali_100)/boogie_dualrectifier.pdf`

The local schematic shows the Mesa-Boogie Dual Rectifier preamp, Red/Orange
gain and tone-stack switching, 12AX7 phase inverter, 6L6/EL34 bias selection,
rectifier switching, and feedback/presence network. Rocksmith only stores
`Gain`, `Bass`, `Mid`, `Treble`, and `Pres`, and the local curation maps
`Amp_CA100` to Mesa Dual Rectifier Red G2/G5/G8 captures. This plugin therefore
models the Red channel as one continuous amp: lower `Gain` is Red G2, mid
`Gain` moves toward G5, and high `Gain` moves toward G8/modern saturation.
