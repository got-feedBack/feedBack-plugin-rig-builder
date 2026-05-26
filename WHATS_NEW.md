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
