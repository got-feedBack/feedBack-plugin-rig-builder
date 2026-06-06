# MrYMaz38 — "Mr. Y MAZ 38"

White-box model of the **Dr. Z Maz 38 (Senior NR)** for Rocksmith's `Amp_GB38`.
Parody brand **"Mr. Y"**. The face must never read "Dr. Z" or "Maz".

> `Amp_GB38` is the Maz **38**. No Maz 38 schematic was available, but the Maz
> line shares one preamp + tone stack, so the front-end is traced from the Maz 18
> Jr schematic; only the POWER amp differs.

## Panel (Senior NR — no reverb)
`VOLUME · TREBLE · MIDDLE · BASS · CUT · MASTER` + High/Low inputs.

Same Maz preamp/tone stack as the Maz 18, but **4× EL84 (~38W) + solid-state
rectification** → much more headroom, tighter/firmer lows, breaks up later, far
less sag than the 2×EL84/GZ34 Maz 18. (The Mr. Y MAZ 18 build was the smaller sibling.)

## Rocksmith mapping
**RS Gain → VOLUME**, Bass/Mid/Treble → tone stack. `_static` pins Cut + Master.
UNIQUE_ID `Ym38`.
