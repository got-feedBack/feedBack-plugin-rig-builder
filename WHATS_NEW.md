# Rig Builder 1.3.4 — Windows extractor + gear-photo size fixes (2026-05-31)

Two Windows-only fixes on top of 1.3.3.

- **Fixed: gear-photo extraction aborted with "UnicodeEncodeError: 'charmap'".**
  After the 1.3.3 "No module named 'common'" fix, extracting photos still died
  on Windows because the extractor prints box-drawing/em-dash/ellipsis/×
  characters and the Windows console + subprocess pipe default to cp1252.
  The tools now force UTF-8 output (and the backend decodes it as UTF-8), so
  extraction completes.

- **Fixed: gear photos shown huge in the Gear and Songs tabs.** The thumbnails
  size themselves with CSS utility classes; on builds where those classes get
  purged the raw ~512px art rendered full size. Image dimensions are now set
  inline so the thumbnails stay small everywhere.

---

# Rig Builder 1.3.3 — Songs-tab render + gears.psarc extractor fixes (2026-05-31)

A small bug-fix release on top of 1.3.2.

- **Fixed: extracting `gears.psarc` failed with "No module named 'common'".**
  The extractor scripts (`extract_gear_map` / `extract_gear_photos` /
  `extract_irs`) import a shared `common` helper. When the packaged app ran
  them as a subprocess (notably on Windows) the script's own folder wasn't on
  Python's import path, so the import blew up and extraction died with
  *"extractor failed: ModuleNotFoundError: No module named 'common' — is this
  really gears.psarc?"*. Each extractor now puts its own folder on the path
  before importing.

- **Fixed: Songs-tab chain piece showing a blank white box.** A gear whose
  photo failed to load (or had a white background) rendered as a white
  rectangle that could also swallow the card border and the status dot. The
  thumbnail now sits on a dark tile with the gear-category label behind it, so
  a missing/odd photo falls back cleanly.

- **Fixed: missing NAM/VST status dot.** The little corner dot that shows
  whether a piece has a NAM, a VST, or nothing now always renders.

---

# Rig Builder 1.3.2 — Chain volume, bundled AutoSweep wah, and VST-mapping fixes (2026-05-30)

The big one is **loudness**: the guitar was sitting far below the backing
track, and the engine ignored every per-stage gain. This release adds the one
gain the engine *does* respect, plus a pile of VST-mapping corrections and a
new bundled effect.

- **"Chain volume" control (the only gain the engine honors).** The native
  engine ignores per-stage NAM/IR gain — the sole lever is `setGain('chain')`.
  There's now a user **Chain volume (makeup)** slider in Settings → Cabinets
  (default **4×**) that drives it, persisted across restarts and re-applied on
  every song load. Fixes the "I had to drop the song to 25% to hear myself"
  problem. (Also fixed: `get_settings` was dropping the saved value, and the
  pre-load gain clamp capped it at 4× — raised to 32×.)

- **Killed the −12 dB volume drop on song load.** A double-attenuation in the
  chain preloader was quietly knocking 12 dB off every tone the moment a song
  loaded. Gone.

- **Bundled AutoSweep envelope filter — no install needed.** Rocksmith's
  **Auto Tone** (auto-wah) pedal now maps to a bundled `AutoSweep.vst3`
  (a real envelope follower → biquad sweep, ported from the user's hardware
  design). Per-song downloads auto-assign it; the UI shows only the
  RS-driven knobs (FilterType / Resonance / Sensitivity / Attack / Release +
  Mix) with the Rocksmith names, and Attack/Release/Sens scaling is calibrated
  to the RS values.

- **VST-mapping corrections across many gears** (curated against real songs):
  - **Compressor pedals** — RS *Compress* amount now drives a proportional,
    full-range *Threshold* curve (was clamping at the floor).
  - **Graphic EQ pedals** — band centers pinned to the real RS frequencies,
    and the 8 RS bands now fold cleanly into MEqualizer's 6.
  - **Tremolo** — RS *Mix* → *Depth* (not Dry/Wet) + a musical *Rate*.
  - **Spring / kHs reverbs** — real Kilohearts Reverb param names
    (*Time → Decay*, *Depth → Size*) and consistent mapping across all
    reverb gears.
  - **Lo-Fi filter** param fix; **Acoustic Emulator** routed to a NAM.
  - **kHs Compressor** calibrated so RS knobs map 1:1.

- **Per-song / cloud downloads auto-assign the VST primary** *before* falling
  back to NAM-reuse, so a freshly downloaded song comes up with its pedals
  already on the right plugins.

- **Repo restructure** — code reorganized into `rb_core/` + `tools/` +
  `data/` + `assets/` with de-duplication (no behavior change; easier to
  maintain). Rocksmith cab IRs get a +1.5× make-up; tone3000 IRs unchanged.

- **Clearer setup errors** — a failed `gears.psarc` extraction now surfaces
  the real underlying error instead of a generic "extractor failed".

---

# Rig Builder 1.3.1 — VST primary auto-assign + cab IRs wire on extract (2026-05-28)

Two fresh-install fixes flagged by users who ran the documented 1.3.0
workflow (Settings → Scan for plugins → Setup → Rescan all) but ended
up with no VSTs mapped to their pedals and no cabinets assigned after
extracting from gears.psarc:

- **Rescan all now promotes each gear to its primary installed VST.**
  Previously the batch worker only resolved NAM/IR captures from
  tone3000 + curated defaults — it never consulted `rs_gear_to_vst.json`,
  so even with Kilohearts Essentials + Melda MFreeFXBundle installed
  and scanned, every pedal/comp/EQ/mod stayed as a NAM. New step,
  inserted between "manual is sacred" and the cab branch: for each
  non-amp/non-cab gear, walk the gear's primary VST candidates from
  `rs_gear_to_vst.json`, pick the first one that's in the
  `rig_builder_known_vsts.json` cache, and assign it. Also computes
  the matching `vst_state` envelope from the song's RS knobs (using
  the same `apply_vst_state.py` logic as the standalone script), so
  the VST loads with the right params at song-play time instead of at
  plugin defaults.

- **Extract from gears.psarc now wires the IRs into cabs automatically.**
  Extracting wrote `.wav` files under `nam_irs/rocksmith/` but didn't
  update any `preset_pieces.file` column, so the user had to remember
  to Rescan all afterwards before any cab actually used the new IRs.
  The endpoint now calls `_wire_cabs_to_presets(replace_auto=True)`
  as a tail step: every cab row that wasn't manually overridden gets
  re-pointed at the freshly-extracted Rocksmith IR, even if it already
  had a tone3000 IR assigned. Manual overrides stay sacred. Response
  payload reports `cabs_wired` so the UI can show "extracted N IRs,
  wired M cab rows".

If your 1.3.0 workflow looked clean but pedals/cabs weren't actually
using the right plugins/IRs, just upgrade and run **Settings → Scan
for plugins** + **Setup → Rescan all** once. Existing manual picks
(including 📸-captured states) are preserved across the migration.

---

# Rig Builder 1.3.0 — full VST chain for pedals/comp/EQ, bass-aware audio, cab self-heal (2026-05-28)

A **pedals + modulation + EQ + comp overhaul**: every distortion / fuzz /
overdrive / chorus / flanger / EQ / comp / reverb / delay pedal-and-rack now
maps to a real free VST plugin with its RS knobs translated to the plugin's
actual parameter names and ranges. Bass songs no longer over-saturate guitar-
amp captures. Cabs self-heal automatically. Tone switching survives songs
that ship with no schedule.

## Required plugins (free, install once)

The mappings target two free VST bundles. Install both before opening Rig
Builder — once installed, every pedal/rack/comp/EQ in your library will Just
Work without you picking VSTs per gear.

| Bundle | What you get | Download |
|---|---|---|
| **Kilohearts Essentials** | kHs Distortion (Saturate/Overdrive/HardClip), kHs Chorus, kHs Flanger, kHs Phaser, kHs Delay, kHs Reverb, kHs Compressor, kHs Bitcrush, kHs Filter, kHs 3-Band EQ, …35 free FX | <https://kilohearts.com/products/kilohearts_essentials> |
| **Melda MFreeFXBundle** | MEqualizer (16-band parametric), MCompressor (full-featured), MTremolo, MVibrato, MFlanger, MPhaser, MRingModulator | <https://www.meldaproduction.com/MFreeFXBundle> |

Both bundles ship VST3 + AU and run unrestricted on every desktop platform
Slopsmith supports. We picked these specifically for their consistent param-
naming conventions — that's what lets us auto-map every RS knob to its right
VST param across the full library.

After installing them, **open Rig Builder → Settings → Scan installed VSTs**
once so the plugin discovers the new files; then **Setup → Rescan all** to
re-attach every preset to its primary VST.

## Headline changes

### 🎛 Pedals, mod, EQ, comp — full VST primary across the board (NEW)

Until 1.3.0 most pedals/racks ran as NAM captures alongside the amp. 1.3.0
promotes every effect type to its native free VST equivalent, with curator-
verified knob → param mappings:

- **Distortion / fuzz / overdrive (16 gears) → kHs Distortion** with per-
  subtype `Type` pinned (fuzz=Saturate, OD=Overdrive, dist=HardClip),
  `Dynamics`=max, and curator mappings for Tone (→ Bias), Filter (→ Dynamics),
  Blend (→ Dry/Wet), Gain (→ Drive). Verified via in-app sweep diagnostic —
  `window.rbSweepParam` is permanent in screen.js for future curators.
- **EQ (6 gears: EQ5/EQ8/BassEQ8/AmpEQ/StudioEQ/StudioGraphicEQ) →
  MEqualizer** with correct band naming (`Frequency N (EQ N)`,
  `Gain N (EQ N)`, `Q N (EQ N)`) AND auto-enabled bands per gear.
- **Compressor (4 gears: MBComp/Compression/Compressor/StudioCompressor) →
  kHs Compressor** (Threshold/Attack/Release/Ratio + Makeup) — 1-to-1 with
  RS knobs, vs MCompressor's 20-param wall.
- **Chorus (6 gears) → kHs Chorus** with Rate (slider position), Depth,
  Delay, Stereo, Mix.
- **Flanger / phaser / tremolo / vibrato → kHs Flanger / kHs Phaser /
  MTremolo / MVibrato**.
- **Reverb / delay (8 racks: StudioVerb/Plate/Chamber/StereoAnalogVibe/
  StudioDelay/TapeEcho/StudioFlanger/StudioChorus) → kHs Reverb / kHs Delay**.

Total: **51 distinct mappings**, **580+ preset_piece rows** repopulated in
the live DB. Already-📸 Captured states are sacred — those keep their
hand-saved opaque blobs across migrations.

### 🔊 Bass-amp playback no longer over-saturated (FIX)

Guitar amps need a +18 dB chain-input boost to drive the captured model into
saturation (live pickup signal arrives quieter than capture-time DI). Bass
captures — Gallien-Krueger RB800 G1.0/G3.0/G5.0, Bassman, CS75B, etc. — are
authored at clean gain and the same boost over-saturates them. Two-layer fix:

- **Catalog audition** uses `rs_gear.startsWith('Bass_')` to pick unity
  (1.0×) drive for bass amps. Now matches the tone3000 web preview.
- **Song playback** reads `window.highway.getStringCount()` — 4 strings →
  unity drive; 6+ strings → guitar drive. Re-polls at +1.5s/+3.5s because
  the highway publishes string count after the chain loads.

Guitar amps unchanged.

### 🎚 Cab self-heal (NEW — merged from amp-remap-library-overhaul)

Cabs whose IR went missing on disk now auto-reassign the right replacement
on every song open + on the watcher's periodic scan. No more "open the cab
editor and re-pick" workflow. Mic-position variants (Dynamic Cone / Condenser
Edge / etc.) audition inline on each cab card.

### 🎯 Curated-only mode + Quick start workflow (NEW)

A first-run Quick start card walks Extract from gears.psarc → Scan library →
Download all curated variants → Play. The `🎯 Curated only` checkbox is the
new default — only the 1-to-1 curated mappings in `rs_to_real.json` +
`default_captures.json` are used; gears without a curated default land in
Pending for manual assignment instead of triggering a tone3000 fuzzy search.

### ✅ Pending tab excludes VST-assigned pieces (FIX)

The coverage SQL flagged every VST piece as pending because VST rows
legitimately have `file=NULL`. New criterion: pending = no file AND no
vst_path. Effect on a typical library: 660 → 109 pending pieces.

### ⬇ Map all also downloads every curated variant (FIX)

Running Setup → Rescan all now auto-downloads any curated amp variant whose
NAM is missing from disk, regardless of whether a song currently uses it.
Idempotent (skip-if-on-disk + rate-gated). The standalone "⬇ Download all
curated variants" button in Gear still works for explicit one-off runs.

### 🎛 Inline VST editor — junk params hidden, name resolution fixed (FIX)

The HTML slider editor for VSTs now filters out Melda's `Param 1..4` /
`(Preset trigger)` / `Bypass` / `Program` rows and any `MIDI` entries.
MCompressor's ~20-param editor shrinks to ~6 musical sliders; MEqualizer
to its actual band controls. The Library/Plugins panel also got a cleanup.

Additionally: the chain walker (real-song playback) now resolves saved
param keys by NAME — previously it only matched numeric IDs, so the bulk-
populated states from the script silently no-op'd at load time and editors
opened with plugin defaults. The walker also clamps values to [0,1]
defensively. Editor-open paths share the same helper so the inline view
matches what playback uses.

### 🎵 Tone switching survives unmapped songs (FIX)

Songs with no PSARC tone schedule (Message in a Bottle, Livin' on a Prayer,
older CDLC) used to land on a heuristic "first guitar tone" after a 10 s
scary `FALLBACK` warning. Three-step fix:
1. Accept the first scheduled tone-change as the intro when the base is
   empty (covers songs that publish the schedule but no base).
2. Detect "no schedule at all" at t+1.5 s and apply the default tone
   immediately with an INFO log (not a WARN).
3. The default tone now matches the user's instrument — bass arrangement
   → bass-flavored tone; guitar → guitar tone — based on `getStringCount()`.

### 📊 Perceptual A/B level-match for amp variant audition (NEW)

LUFS normalization already matches integrated loudness across captures, but
distortion captures still sound louder than clean ones at the same LUFS
(harmonic density). Variant audition buttons now apply a perceptual trim per
level — clean=0 dB, crunch=-3 dB, dist=-6 dB — so clicking the 3 ▶ buttons
on a curated amp gives a fair A/B at matched loudness.

## Internal / dev

- `window.rbSweepParam(slot, paramName)` — permanent DevTools diagnostic
  that maps a stepped/enum VST param's display ↔ normalized values without
  guessing from docs.
- `[rig_builder restore] verify: want=X got=Y` — every editor-open path
  now logs whether the post-set values match what was requested.
- `apply_vst_state.py` carries a `_VST_PARAM_RANGES` table for dB/Hz-domain
  params, so future curated mappings can output display values and the
  script normalizes correctly per plugin.
- 43-commit merge of `feat/amp-remap-library-overhaul` (the parallel branch:
  cab self-heal, mic positions, curated-only mode, swap-replace).

---

# Rig Builder 1.2.0 — amp variants, instant tone switching, louder & cleaner (2026-05-27)

A big **amp + library overhaul**: per-gain-stage amp captures, a real library
manager, near-instant tone switching, and a stack of loudness/audio fixes. This
release closes the four open issues (#12, #13, #14, #15).

## Headline changes

- **🎚 Amp gain variants (clean / crunch / dist).** Each amp can carry up to
  three captures keyed to its gain range, so a song's clean and high-gain tones
  pull the *right* capture instead of one compromise. New Gear-catalog panel to
  pick per-variant captures (▶ audition per variant), backend CRUD, and a
  curation workflow — pin captures by `model_id`, import a shared CSV, and the
  `inspect_tone3000` helper.

- **📚 Library management overhaul.** New **Manage** tab, on-disk storage
  subdirs, and file classification by *content* (not by which folder a file
  happens to live in) so captures, IRs and VSTs sort correctly.

- **⚡ Chain preloader — instant tone switching (now the default).** The whole
  song's chain is pre-loaded and a tone change flips bypass instead of
  rebuilding the chain. No audible rebuild on every tone change, and dedupes
  shared NAMs/IRs across a song's tones. *(fixes #12)*

- **🔉 Loudness & saturation fixes.** Per-NAM loudness normalization, chain
  makeup gain scaled by NAM count (asymmetric cap), and the engine now drives
  amp NAMs with enough input level to actually saturate. Rocksmith cab IRs are
  L2-normalized to match tone3000 IRs (they were 10–20 dB quieter). NAMs are no
  longer almost-inaudible. *(fixes #15)*

- **🔗 Master chain reliably applies in real songs.** The preloader + AMP-toggle
  auto-apply + persisted master/per-tone bypass mean the master chain engages
  when you actually play a song — not only in Listen, and without the
  "load song → toggle AMP → reload" dance. *(fixes #13)*

- **🔇 No more tone-change spike.** The monitor is muted during `loadPreset`
  with a fade-in on restore, killing the loud feedback/click on tone changes.
  *(fixes #12)*

- **🎛 Bigger free-VST catalog + RS knob mapping.** Pedal VST suggestions grew
  23 → 81 (free plugins only), standardized on the free **Kilohearts Essentials**
  bundle, with 41 seeded Rocksmith-knob → VST-param mappings. RS knob values are
  shown per piece so you can dial them in by hand.

- **🖼 Generic gear-photo extraction** for pedals, racks, cabs and amps
  (`extract_gear_photos.py`).

## Issue fixes

- **#15 — NAMs very quiet.** Per-NAM normalization + input-gain drive into the
  amp NAMs + Rocksmith-IR L2-normalization.
- **#12 — feedback spike on tone change.** Chain preload + monitor mute/fade.
- **#13 — master chain not applying to song.** Chain preloader + AMP-toggle
  auto-apply + persisted bypass.
- **#14 — Extract Rocksmith IRs on mac.** IR extraction pipeline reworked
  (auto-locate the extracted-IR directory + L2 normalization). If a mac
  `gears.psarc` still yields 0 IRs, the Windows `gears.psarc` extracts cleanly.

## Upgrade notes

- After updating, **quit and reopen Slopsmith** (no hot reload). DB migrations
  run automatically on first boot.
- The **Chain preloader** is on by default (was the "Mega-chain" toggle). If a
  weak machine crackles past 2–3 `standard` NAMs, raise the audio buffer or use
  lighter captures for pedals/racks.

---

# Rig Builder 1.1.0 — sign in, don't paste keys (2026-05-26)

This release removes the need to handle a sensitive tone3000 API key and makes
the library-wide mapping behave the way you'd expect.

## Headline changes

- **🔐 Connect with tone3000 (OAuth login).** Instead of pasting an API key,
  Settings now has a **Connect with tone3000** button: it opens tone3000 in
  your browser, you approve once, and the plugin gets a temporary, revocable
  token. **No secret key to copy or store.** This is tone3000's recommended
  flow for plugins.
  - Uses OAuth 2.0 with **PKCE** over a loopback redirect — the authorization
    code can never leave your machine (the `redirect_uri` is hard-restricted
    to `127.0.0.1`/`localhost`), `state` is validated against CSRF, and tokens
    are stored owner-only (`0600`) and refreshed automatically.
  - The old "paste a secret key" box was **removed** from the UI (the backend
    still understands one for advanced/server use, but it's no longer shown).
- **🧠 Smart library mapping — "manual is sacred, auto inherits".** The
  Dashboard batch buttons now do what you'd expect:
  - A piece you assign **by hand in one song is never overwritten** by the
    batch — not even by *Remap all*. Per-song tweaks are safe.
  - Songs you've **never touched inherit** the capture you assigned to that
    same gear elsewhere — so configuring an amp once spreads it across the
    library without redoing it per song or exporting defaults first.
  - *Map new songs only* fills just the unmapped tones, inheriting your
    existing per-gear choices; *Remap all* refreshes auto pieces while keeping
    every manual pick.
- **📂 Browse… buttons.** The *Regenerate gear map* and *Extract Rocksmith IRs*
  settings now have a native **Browse…** file picker — no more typing the
  `gears.psarc` path by hand.

## Upgrade notes

- After updating, **quit and reopen Slopsmith** (no hot reload).
- Settings → **Connect with tone3000** to sign in. If you previously pasted an
  API key it keeps working in the background, but signing in is preferred.

---

# Rig Builder 1.0.0 — first stable release (2026-05-25)

The first stable release of **Rig Builder** (formerly `nam_rig_builder`). It
maps Rocksmith 2014 tones (amp + cab + pedals + racks) to NAM captures + IRs
from tone3000.com — and now also to your own **VST3 / AU plugins** — so playing
a CDLC in Slopsmith uses the full, realistic chain instead of generic sounds.

## Headline features

- **🎛 VST3 / Audio Unit support.** Assign any installed VST3/AU as a chain
  stage on any pedal / amp / rack — alongside NAM captures and IRs. Inline
  parameter editor (crisp HTML sliders driving the plugin in real time), plus
  the plugin's own native editor window. Plugin settings are captured as the
  engine's opaque state blob so they apply in **real song playback**, not just
  the preview (e.g. a compressor's makeup gain). N:1 bulk-assign from the Gear
  tab applies a plugin to every song that uses that gear.
- **🔗 Master Chain.** Global pre/post FX wrapped around *every* song: a
  `master_pre` chain sees the raw DI, a `master_post` chain sees the wet output
  (e.g. a global compressor / EQ on the master bus). Per-stage bypass + its own
  inline VST editor.
- **🖥 New DAW-style UI.** Reworked chain editor with a Library picker
  (Files | Plugins tabs), per-slot library browser, a searchable
  category-grouped plugin picker, a song list with real title/artist metadata,
  and a fully English interface.
- **🔊 Full-chain real playback.** The whole chain (pedal → amp → … → cab,
  multiple NAM stages + VSTs + IR) plays in actual songs via a scoped
  `fetch` redirect — no edits to the signed Slopsmith bundle, survives updates.
- **⬇ Auto-download + batch.** With a tone3000 API key, opening a song
  auto-downloads every missing piece; Dashboard batch processes the whole
  library. Curated `default_captures.json` picks good captures by default.
  Deep-link mode works with no key.

## Stability fixes in this release

- VST editor no longer crashes the app when navigating to another tab,
  loading a song, or leaving the plugin to play a song (the open native editor
  window is now closed before any chain reload).
- Master / chain VST settings now actually apply during real song playback
  (opaque-state capture) — previously the plugin came up at its defaults.
- Per-song bypass + gear edits auto-save and survive re-download; saved gear
  presets are no longer lost on song re-download.

> **Upgrading from a VST preview build:** VST pieces saved before this release
> stored only a `{params}` dict and play at plugin defaults in real songs.
> Re-open each VST editor once and save (Capture state or any slider drag) to
> grab the new opaque state blob. DB migrations run automatically on first boot.

## AMP toggle now auto-applies the chain mid-song

Previously, if you loaded a song with **AMP off** and turned it on later,
the master chain (and per-song chain) wouldn't engage — the bundle gates
its chain push on AMP being on at song-load time. Rig Builder now watches
the AMP button and, on every OFF → ON flip, replicates the chain push
itself ~1 second after the toggle so master is included. No more
"leave the song and come back" workaround.

Kill-switch if it ever misfires: `window.__rbAmpAutoApply = false` in the
DevTools console.

---

# What's new — rig_builder VST preview (2026-05-25)

This is a working preview of the **VST3 / Audio Unit support** branch
of `nam_rig_builder`, renamed to `rig_builder`. Made by Nacho with
help from Claude.

## TL;DR

The plugin can now assign **VST3 or AU plugins** as chain stages
alongside the existing NAM + IR support. The Slopsmith engine already
hosts VSTs (`type: 0` in the chain JSON via `loadVST` / `setParameter`
APIs) — we just never used that surface before. This branch wires it
up end-to-end.

## What works

1. **NAM / IR / VST per gear** — every pedal/amp/rack row in the Songs
   tab and every card in the Gear tab now has a `⚙ VST…` button. Open
   it → pick a `.vst3` or `.component` bundle → assign.

2. **File picker + paste path** — no need for `scanPlugins()`. The
   user's Slopsmith was crashing on scan (one of the user's UAD/NI
   plugins instantiates badly during JUCE validation), so the primary
   path is now `pickFile` + a text input for raw paths. Scan is still
   there as opt-in for users whose plugins don't crash.

3. **In-Slopsmith param editor** — when you `▶ Load & Edit`, we
   render HTML sliders for every param exposed by `getParameters()`.
   Real-time `setParameter` via slider drags. Solves the blurry
   native-editor problem on Retina + lets us capture state as a
   portable `{paramId: value}` JSON dict instead of opaque blobs.

4. **N:1 bulk assign** in the Gear tab — assigning a VST to a gear
   from the catalog applies it to every `preset_pieces` row with that
   `rs_gear_type` (one click, dozens of songs updated).

5. **Rocksmith knob display per piece** — under each gear row in
   Songs, the in-game RS knob settings are now visible (e.g.
   `Rate=50 · Depth=30 · Mix=70`). Always there, no curation needed.

6. **RS → VST knob mapping** (skeleton) — `rs_knob_to_vst_param.json`
   is the curation file; `/vst/knob_mapping` is the endpoint; the
   `⇶ Apply RS settings` button consumes both. Empty by default —
   the curated entry for `Pedal_Chorus20 × uaudio_brigade_chorus` is
   in the `_example_*` section, move it up to enable.

7. **Auto-migration** — `nam_rig_builder_settings.json` and
   `nam_rig_builder_cache.db` get copied to their new names on first
   boot, so the tone3000 API key + search cache survive the rename.

## DB schema additions

`preset_pieces` got three new columns (auto-migrated):
```sql
ALTER TABLE preset_pieces ADD COLUMN vst_path TEXT;
ALTER TABLE preset_pieces ADD COLUMN vst_format TEXT;   -- 'VST3' | 'AudioUnit'
ALTER TABLE preset_pieces ADD COLUMN vst_state TEXT;    -- JSON {"params": {...}}
```

`kind = 'vst'` is the new kind value (was: 'nam', 'ir', 'rs_ir').

## Chain JSON for VST stages

`native_preset_full` emits these alongside the existing type-1 / type-2:
```json
{
  "type": 0,
  "name": "uaudio_brigade_chorus",
  "path": "/Library/Audio/Plug-Ins/VST3/uaudio_brigade_chorus.vst3",
  "format": "VST3",
  "bypassed": false,
  "slot": "pre_pedal",
  "rs_gear": "Pedal_Chorus20",
  "state": "<base64({pluginPath, format, pluginState:'{\"params\":{...}}'})>"
}
```

On `loadPreset`, we then walk the loaded chain (via `getChainState()`),
identify type-0 slots, and call `setParameter` for each saved param.
This is the workaround for the engine's chain JSON not reliably
restoring per-VST state via the `state` field alone.

## Known issues to test with your setup

1. **VST scan crash** — `api.scanPlugins()` crashes Slopsmith hard on
   Nacho's machine (one of his plugins instantiates badly during JUCE
   validation). Same crash happens in the bundle's `audio_engine`
   plugin, so it's not us. We work around by using `pickFile` + raw
   paths. **If your scan doesn't crash, the dropdown auto-populates
   and that flow is also fully wired.**

2. **Blurry native editor** — when you click `▶ Load & Edit`, the
   native plugin window opens but renders at 1x scale on Retina. The
   `JUCE VST3PluginWindow::ScaleNotifierCallback` exists in the engine
   binary but isn't being invoked. **Workaround in the plugin**: the
   inline HTML param editor (sliders below the panel) renders crisp
   and lets you bypass the native window entirely. **Real fix is in
   the bundle** — your call whether to ship a notify-scale fix.

3. **VST param state restoration** — the chain JSON's `state` field
   for type-0 stages doesn't seem to apply automatically when the
   engine builds the chain (params come up at defaults). We work
   around in JS by walking the chain after `loadPreset` and calling
   `setParameter` for each saved value. **If you can confirm whether
   the engine SHOULD honour the b64 state for VST stages, that would
   save us the post-load walk.**

4. **`scanPlugins` API shape uncertainty** — `audio_engine/screen.js`
   does `knownPlugins = await api.scanPlugins()` (return-value as
   list). My code handles both that and the void-return signature.
   Confirm whichever is correct so we can drop the fallback.

## Install (same as the original plugin)

1. Quit Slopsmith
2. Drop this folder at
   `~/Library/Application Support/slopsmith-desktop/plugins/rig_builder/`
3. If you have the old `nam_rig_builder` installed, disable it:
   `mv .../plugin.json .../plugin.json.disabled` in its dir
4. Open Slopsmith → "Rig Builder" appears in the nav

The DB migration runs automatically on first boot.

## Files added/modified vs main

- **NEW**: `rs_gear_to_vst.json` — seed catalog of free VST suggestions per RS gear
- **NEW**: `rs_knob_to_vst_param.json` — curation table for auto-translating RS knobs → VST params
- **RENAMED**: `nam_rig_builder` → `rig_builder` everywhere
  - `plugin.json` id/name
  - Python URL prefix `/api/plugins/rig_builder/*`
  - JS prefix `rb*` (was `tb*`)
  - Settings/cache filenames (with auto-migration from old names)
- **MODIFIED**: `routes.py` — VST endpoints + schema migration + chain emission
- **MODIFIED**: `screen.js` — VST panels in both Songs + Gear tabs, inline param editor

Branch: `feat/rig-builder-vst` off `main`. Not committed yet — drop
the folder in, test, and we can commit once we agree on what to keep.
