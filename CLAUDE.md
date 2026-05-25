# CLAUDE.md — NAM Rig Builder plugin (agent primer)

Read this first, then read **`HANDOFF.md`** in this same folder for the full
design (DB schema, tone3000 API quirks, cab-IR binary format, per-feature
history). This file is the quick, self-contained orientation so a Claude
session on a fresh machine — with none of the prior conversation or memory —
can pick up safely.

> The user communicates in **Spanish** — respond in Spanish. They like
> concise technical trade-off tables before deciding.

---

## What this is

`nam_rig_builder` is a **Slopsmith** plugin. Slopsmith is an Electron
music/guitar app. The plugin maps **Rocksmith 2014 tones** (amp + cab +
pedals + racks) to **NAM captures + IRs from tone3000.com**, and persists
per-song presets into the sister `nam_tone` plugin's `nam_tone.db` so the
audio engine plays realistic neural-amp sims instead of generic synth.

Stack: Python (FastAPI) backend `routes.py` + browser JS UI `screen.js` /
`screen.html`. ~80 KB code + ~360 KB generated JSON.

---

## Golden rules (environment gotchas)

1. **This folder lives inside the Electron app-data dir, NOT a git repo.**
   On macOS:
   `~/Library/Application Support/slopsmith-desktop/plugins/nam_rig_builder/`.
   On Windows it's the equivalent under `%APPDATA%\slopsmith-desktop\plugins\`.
   `git` does not work here. Session-to-session state lives in `HANDOFF.md`.

2. **No hot reload.** After editing any plugin file the user must **quit and
   reopen Slopsmith** for changes to take effect. DB migrations in
   `_get_conn()` run automatically on first launch.

3. **Use Slopsmith's bundled Python** for any verification — it's the only
   interpreter with `pycryptodome` (needed to read PSARC files). On macOS:
   ```bash
   PY=/Applications/Slopsmith.app/Contents/Resources/python/runtime/bin/python3.12
   cd <this folder>
   PYTHONPATH=/Applications/Slopsmith.app/Contents/Resources/slopsmith/lib:. \
     $PY -c "import routes; print('routes OK')"
   ```
   (On Windows the bundled interpreter is under the Slopsmith install dir;
   locate `python*/runtime` and `Resources/slopsmith/lib`.)

4. **Validate JS without a browser** using deno (compile-only, no execute):
   ```bash
   deno run --allow-read - <<'EOF'  # or a tiny check script
   const c = await Deno.readTextFile("screen.js"); new Function(c);
   console.log("screen.js: syntax OK");
   EOF
   ```
   The UI builds HTML via template strings inside `.map()` — a single
   `ReferenceError` there silently kills the whole tone list and the panel
   hangs on "Loading…". `tbLoadSongTones` now try/catches the render.

5. **Test DB changes against a COPY**, never the live DB:
   `cp slopsmith-config/nam_tone.db /tmp/test.db`, point `routes._db_path`
   at the copy, then call module-level functions like
   `routes._persist_preset_chain(...)`.

Key paths:
| Thing | macOS path |
|---|---|
| App bundle | `/Applications/Slopsmith.app/` |
| Bundled Python | `…/Slopsmith.app/Contents/Resources/python/runtime/bin/python3.12` |
| Host code | `…/Slopsmith.app/Contents/Resources/slopsmith/` |
| Sister plugin (in bundle) | `…/Resources/slopsmith/plugins/nam_tone/` |
| Config dir | `~/Library/Application Support/slopsmith-desktop/slopsmith-config/` |
| DB / models / IRs | `slopsmith-config/nam_tone.db`, `nam_models/*.nam`, `nam_irs/*.wav` |

---

## How playback works (and the engine limit)

- Sister plugin **`nam_tone`** (ships inside the signed app bundle) owns the
  audio engine and `nam_tone.db` (`presets`, `tone_mappings`). nam_rig_builder
  writes into those tables + an added `preset_pieces` table (full chain).
- The DSP: native module `slopsmith_audio.node` (C++) via
  `window.slopsmithDesktop.audio.loadPreset(JSON)`, with a WASM worklet
  fallback (`nam-core.wasm` + `nam-processor.js`) that is **single-NAM**.
- The bundle's `get_native_preset` builds a 2-stage chain (one amp NAM + one
  cab IR) from `presets.model_file`/`ir_file`.
- **The native engine DOES accept multiple type-1 (NAM) stages in series**
  (verified on an M1 via the preview `slotsLoaded` test). This is what makes
  the full chain (pedal → amp → … → cab) possible.

---

## Current feature state (as of 2026-05-25)

All of the below is **plugin-only — no bundle edits**, so install is just
"drop the folder + restart". See `HANDOFF.md` sections v3.5–v3.8 for detail.

1. **Query-tier gear mapping (v3.5).** `extract_gear_map.py` separates
   display `model` from the `tone3000_query` and records `query_source`.
   Series codename families resolve to a **brand-only** query (the codename
   number is fake and tanks search). Series table is keyed by
   `(instrument, prefix)` — guitar vs bass differ. Unknown amp families fall
   to a generic `"guitar amp"/"bass amp"` query.

2. **Primary-NAM picker = amp/rack only (v3.6).** `_MODEL_SLOT_PRIORITY =
   ("amp","rack")` — a pedal NAM is never promoted to the bundle's single
   `model_file` (avoids "pedal plays as the amp, sounds wrong").

3. **Live preview "▶ Listen" per tone (v3.6).** `tbListenTone` persists
   the tone then loads the **full chain** straight into the native engine
   (monitors live guitar input). Logs `slotsLoaded` to the console.

4. **Per-stage Bypass + persistence (v3.7).** Each piece has a **Bypass**
   button → sets the stage's `bypassed=true` = engine **passes signal
   through** (NOT mute, doesn't break the chain). Persisted in
   `preset_pieces.bypassed` (guarded `ALTER TABLE` migration), scoped per
   tone's preset on read so it doesn't bleed across songs. A bypassed
   amp/cab is also excluded from the bundle's single `model_file`/`ir_file`.

5. **Immediate gear refresh (v3.7).** Upload / assign RS-IR /
   download-and-assign re-render the song from in-memory state and reload
   the live preview — no re-selecting the song. `tbRenderPiece` reads
   `_uploaded_file` before `assigned.file`.

7. **Gear catalog "Gear" tab (v3.9).** `/gear_catalog` groups mapped
   gears by type with what they're parented to + a **photo** (tone3000
   capture image via `_tone_image_index`, read from the local cache —
   Rocksmith gear art is not available). ▶ auditions a gear in isolation
   (`/native_preset_one` + `tbAuditionFile`). The Suggest modal shows each
   candidate's photo + ▶ that downloads (no assign, `/audition_candidate`)
   and auditions. tone3000 has no audio-clip API, so "listen" =
   download-then-audition.

6. **Full-chain REAL playback (v3.8) — the important one.** nam_rig_builder's
   `screen.js` **monkey-patches `window.fetch`** to redirect just
   `GET /api/plugins/nam_tone/native-preset/{id}` →
   `GET /api/plugins/nam_rig_builder/native_preset_full/{id}` (identical shape,
   every NAM stage). So actually playing a song uses the chain, not amp+cab.
   - Strictly scoped by regex; all other fetches pass through.
   - Falls back to the original 2-stage endpoint if the full build is
     not-ok / empty / unparseable / throws.
   - **Kill-switch:** `window.__tbChainPlayback = false` in the console.

`native_preset_full` (in `routes.py`) builds the chain: every `nam`
`preset_piece` as a type-1 stage in signal-flow order
(`_CHAIN_NAM_ORDER = pre_pedal → amp → post_pedal → rack`, stable sort keeps
multiple same-slot pedals in order) + the cab IR as type-2, with each stage
carrying `slot`/`rs_gear` (for the UI bypass map; the engine ignores them)
and persisted `bypassed`. `_state_b64` / `_safe_child` are byte-identical
copies of nam_tone's.

8. **Per-song persistence + cab + batch (2026-05-24/25).**
   - Re-assigning an already-assigned gear now **replaces** the file
     (`_assign_file_to_gear` updates all rows, not just pending).
   - Bypass + gear changes **auto-save** (`tbToggleBypass` /
     `tbAfterGearChange` call `tbPersistTone`); `tbSeedBypass` restores the
     bypass UI after every `/song` fetch (incl. the auto-download re-fetch).
   - Catch-all gears (`Cabinets`, `Pedals`) are categorized via
     `_gear_category` so cabs search/download as IRs; IR download falls back
     to a raw copy if ffmpeg fails.
   - **`default_captures.json`** ships curated `gear → tone3000_id`; batch /
     auto-download prefer it. Regenerate via Settings → "Export defaults".
   - **Two batch modes** (`POST /batch_all {mode}`): `new` = only unmapped
     tones; `all` = remap all but preserve per-tone bypass + cab-IR variant.

See `HANDOFF.md` for the full detail and the route table.

---

## Install + verify on this machine

1. Install Slopsmith, run once (creates app-data + ships `nam_tone`), quit.
2. Put this folder at `…/slopsmith-desktop/plugins/nam_rig_builder/`.
3. Restart Slopsmith → "NAM Rig Builder" appears in the nav. The `bypassed`
   column migration runs automatically.
4. Optional but needed for downloads: Settings → paste a tone3000 API key
   (`t3k_…`). Without it, deep-link mode works (manual `.nam` download).
5. `rs_to_real.json` ships pre-generated; if this machine's Rocksmith DLC
   differs, regenerate via Settings → "Regenerate gear map" with its
   `gears.psarc`. `rs_cab_to_ir.json` references extracted RS cab IRs that
   are NOT shipped — absent ones degrade gracefully to tone3000 deep-links;
   run `extract_irs.py` with `gears.psarc` to populate them.

**Verify the chain plays:** open a multi-NAM song (e.g. Reptilia's dist
tone has 2 pre-pedals + amp + rack + cab = 4 NAM + 1 IR), press ▶ Listen
with DevTools open, confirm the console shows
`slotsLoaded == chain length`. Then play the song for real — it should
sound like the full chain.

---

## Performance note

NAM cost is additive and dominated by model **size**
(`standard ≫ lite ≫ feather ≫ nano`, settable via `preferred_size`).
On an M1 a 4-NAM chain ≈ ~10–20% of one core (comfortable). Weak x86
laptops may crackle past 2–3 `standard` NAMs at small buffers → raise the
audio buffer, use lighter captures for pedals/racks, or flip the kill-switch.

---

## Open / not done

- A **Settings toggle** for full-chain playback (instead of the console
  kill-switch) was offered but not yet built.
- Unknown amp pseudonym families on the "generic" floor (guitar CS, GB;
  bass BT, CH, CS, HT, LT) await the user's brand knowledge — fill in
  `_SERIES_PREFIX_OVERRIDES` (keyed `(instrument, prefix)`) and regenerate.
- WASM-only installs stay single-NAM (worklet limit); the fetch redirect is
  harmless there but won't chain.
- "Aggressive" batch second-pass for gear left pending under the
  conservative license/downloads policy.

Keep `HANDOFF.md` updated when you change behavior — it is the source of
truth across sessions.
