# How to build a pedal UI (in-app canvas)

This guide explains how to add a faithful in-app UI for one of the bundled
pedals/racks. All of these UIs live in a single file — **`pedal_canvas.js`** —
and are drawn on an HTML `<canvas>`. There is **no build step**: it's plain
JavaScript served to the app, so you just edit the file and restart Slopsmith.

> **Golden rule (copyright):** recreate the *look* of the real pedal only.
> **No real brand or model names, no logos.** Use neutral names in code too
> (we call Boss-style pedals "chief"). Match the layout, knob count, lettering
> style and footswitch — but make it legally yours. Use subtle, realistic
> colours (never pure white `#fff` / pure black `#000`).

---

## 1. How the system works (1 minute)

- `pedal_canvas.js` exposes a global `window.RBPedalCanvas` and a registry
  object **`P`** that maps a **stem** → a **spec** describing how to draw that
  pedal.
- The **stem** is the VST's filename, lowercased, with `.vst3` removed and all
  non-alphanumerics stripped:
  - `BassFuzz.vst3` → `bassfuzz`
  - `StudioChorus.vst3` → `studiochorus`
  - `EQ8.vst3` → `eq8`
- When the user presses **Edit** on a pedal, the app looks up `P[stem]`. If it
  exists, it draws your faithful UI inline (draggable knobs drive the plugin
  live). If it doesn't, it falls back to a plain auto-generated knob panel.
- Your job: add a `P.<stem> = { … }` entry.

---

## 2. Before you draw — find two things

### a) The VST + its parameter order
Open `vst/src/<pedal_dir>/<Name>Params.h`. You'll see an enum like:

```c
enum BassDistortionParamId { kGain = 0, kTone, kFilter, kParamCount };
static const char* const kBassDistortionNames[] = { "Gain", "Tone", "Filter" };
```

That order is what you need. **Each knob's `id` = its 0-based position in this
enum** (Gain=0, Tone=1, Filter=2). Do **not** add any offset — the framework
handles the engine's hidden "Buffer Size"/"Sample Rate" params for you.

> Use **exactly the number of knobs Rocksmith exposes** (= the param count).
> Don't invent extra knobs.

### b) A reference photo of the real pedal
The pedal each VST models is usually noted in the `*_ui.cpp` file's header
comment or in `data/rs_gear_to_vst.json`. Get a clear photo and copy its
layout, knob style, colours and lettering.

---

## 3. The spec shape

```js
P.mypedal = {
  w: 300, h: 480,                 // design size in px (portrait stomp ≈ 300×480)
  knobs: [
    // cx, cy, r are NORMALIZED (0..1 of w/h). id = param enum index.
    { id: 0, cx: 0.30, cy: 0.27, r: 0.10, style: 'pointer', cap: [40, 42, 48] },
    { id: 1, cx: 0.70, cy: 0.27, r: 0.10, style: 'pointer', cap: [40, 42, 48] },
  ],
  tick: rgb(150, 150, 150),       // optional: knob tick colour
  ptr:  rgb(235, 236, 240),       // optional: knob pointer colour
  draw(d) {
    // d = { ctx, W, H, s }  where W = spec.w, H = spec.h, s = 1
    box(d, 40, 42, 48);                                  // body
    textC(d, 0.30 * d.W, 0.41 * d.H, F.barlow, 11, rgb(235,236,240), 'DRIVE');
    textC(d, 0.70 * d.W, 0.41 * d.H, F.barlow, 11, rgb(235,236,240), 'TONE');
    textC(d, 0.5 * d.W,  0.60 * d.H, F.bebas, 44, rgb(235,236,240), 'MY PEDAL');
    ledDot(d, d.W * 0.5, d.H * 0.77, true, 210, 70, 58); // on LED
    footRound(d, d.W * 0.5, d.H * 0.88, 23 * d.s);       // footswitch
  },
};
```

You do **not** draw the knobs yourself — the renderer reads `spec.knobs` and
draws them on top of your `draw()` at the right value. You just draw the body,
labels, LED and footswitch.

### ⚠️ Coordinate rule (important)
- **`spec.knobs` `cx/cy/r`** → **normalized** 0..1.
- **Helpers inside `draw()`** (`ledDot`, `footRound`, `textC`, `textSpaced`,
  `outlineText`, `screw`) → **absolute** pixels, so write `d.W * 0.5`,
  `d.H * 0.7`, etc.
- **`boxedLabel`** is the exception → its `cx/cy/hw/hh` are **normalized**.

---

## 4. Helpers you can use in `draw(d)`

| Helper | What it draws |
|---|---|
| `box(d, r,g,b)` | Full box-pedal body (dark frame + coloured panel + 4 screws) |
| `chiefBody(d, r,g,b)` | Boss-compact body (knob plate + treadle + step pad + LED) |
| `chiefName(d, n1, n2)` | Engraved treadle name; two words diagonal, or one centred |
| `ledDot(d, cx,cy, on, r,g,b)` | The status LED |
| `footRound(d, cx,cy, R)` | Round metal footswitch |
| `screw(d, cx,cy)` | A single screw |
| `textC(d, cx,cy, font, px, col, str, align?)` | Centred text |
| `textSpaced(d, cx,cy, font, px, col, str, sp)` | Letter-spaced text (`sp` = spacing) |
| `outlineText(d, cx,cy, font, px, fill, outline, str, sp?)` | Text with an outline (big wordmarks) |
| `boxedLabel(d, cx,cy, hw,hh, font, px, line, txt, str)` | Rounded-rect outline + centred label (NORMALIZED) |
| `rgb(r,g,b,a?)` | Colour string |
| `rr(c, x,y,w,h,radius)` | Rounded rect path (call `c.fill()`/`c.stroke()` after) |

### Fonts (embedded, copyright-free)
Use `F.bebas`, `F.barlow`, `F.anton`, `F.crete`. Pick the one closest to the
real pedal's lettering:
- **Bebas** — tall condensed caps (wordmarks).
- **Anton** — heavy bold caps (big bold wordmarks like "DISTORTION").
- **Barlow** — clean sans (knob labels, small text).
- **Crete** — serif italic (engraved Boss-style names).

### Knob styles (the `style` field)
- `'pointer'` — light cap + tick fan + pointer line (default).
- `'boss'` — knurled black knob with a pointer (chief/Boss pedals).
- `'davies'` — skirted black knob (fuzz/vintage).
- `'knurled'` — aluminium knurled knob.

Set per-knob colour with `cap: [r,g,b]` (the cap base colour). Override the
tick/pointer colours per-spec with `tick:` / `ptr:`.

---

## 5. Shortcuts (use these when they fit)

Most pedals fit one of three families — use the builder instead of writing
`draw()` by hand:

### Boss-compact pedal → `chiefSpec`
```js
P.basschorus = chiefSpec(300, 480, [40, 158, 150],
  [{ id: 0, cx: 0.205, lbl: 'RATE' },
   { id: 1, cx: 0.40,  lbl: 'DEPTH' },
   { id: 2, cx: 0.595, lbl: 'LO FILTER', lblPx: 8 },   // lblPx shrinks long labels
   { id: 3, cx: 0.79,  lbl: 'MIX' }],
  'Bass', 'Chorus');                                    // two-word treadle name
```
`chiefSpec(w, h, [r,g,b], knobIds, name1, name2)` — body colour, up to 4 knobs
across the top, engraved two-word name on the treadle.

### Generic box pedal with a wordmark → `boxSpec`
```js
P.bassphase = boxSpec(320, 470, [124, 92, 68],
  [{ id: 0, cx: 0.20, lbl: 'RATE' }, { id: 1, cx: 0.40, lbl: 'DEPTH' },
   { id: 2, cx: 0.60, lbl: 'MIX' },  { id: 3, cx: 0.80, lbl: 'FILTER' }],
  'PHASE', 'BASS  PHASER', [244, 236, 220]);            // wordmark, subtitle, accent colour
```
`boxSpec(w, h, [r,g,b], knobs, wordmark, subtitle, accentColour, wordmarkFont?)`.

### Graphic EQ → `eqSpec`
```js
P.eq8 = eqSpec({ w: 320, h: 500, style: 0, db: 15, col: [188, 190, 186],
  label: 'Equalizer',
  bands: ['50','100','200','400','800','1600','3200','6400'] });   // param id = band index
```
`style: 0` = Boss portrait (treadle), `style: 1` = Mesa landscape (white
nameplate). `db` = the ±dB range. Faders are added automatically; no `knobs`.

---

## 6. Step-by-step

1. Find the VST file in `vst/` → compute the **stem**.
2. Open `vst/src/<dir>/<Name>Params.h` → note the **param enum order**
   (knob `id`s) and the **knob count**.
3. Get a **reference photo** of the real pedal it models.
4. Add `P.<stem> = …` to `pedal_canvas.js` (use a shortcut if it fits; copy a
   similar existing pedal as a starting point). The existing pedals near the
   top of the `P` section are good templates.
5. Save. **Restart Slopsmith** (no hot reload).
6. Open a song (or the Gear tab), press **Edit** on that pedal:
   - The face should look like the real pedal.
   - Each knob should sit at the song's value and, when dragged, change the
     **correct** parameter.

---

## 7. Checklist before you commit

- [ ] Knob `id`s match the param enum order in `<Name>Params.h`.
- [ ] Knob **count** matches what Rocksmith exposes (no extra/missing knobs).
- [ ] No real brand/model names or logos anywhere (UI **and** code).
- [ ] Colours are subtle/realistic (no pure white/black).
- [ ] Font choice resembles the real pedal.
- [ ] Tested in-app: looks right + knobs map to the right parameters.

---

## 8. What still needs a UI

Anything **not** in the `P` registry currently uses the plain auto-generated
fallback. Already done (faithful): the 11 bass pedals + `eq8` / `basseq8` /
`eq5`. Still to do: the Studio **rack** units and the rest of the pedal
library. Pick one, follow the steps above, open a PR.

Questions or a tricky layout? Look at how the closest existing pedal is built
in `pedal_canvas.js` and at the matching C++ reference in
`vst/src/<dir>/<Name>_ui.cpp` (same look, different language).
