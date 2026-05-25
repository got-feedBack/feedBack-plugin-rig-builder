# Rig Builder — handoff doc

A Slopsmith plugin that maps **Rocksmith 2014 tones** (amp + cab + pedals + racks)
to **NAM captures and IRs from tone3000.com**, persisting per-song mappings in
`nam_tone.db` so the existing NAM runtime plays them back automatically.

This document is for the next person/agent to pick up: it explains the
context that isn't obvious from the code alone — host extension model,
database schema, API quirks, what's done, what isn't, and why.

---

## What the user wanted

Take Rocksmith tones (amp, cab, pedals, racks already exposed by the
existing `tones` plugin) and **map them to real NAM `.nam` captures and
`.wav` IRs** so that playing a CDLC song in Slopsmith uses
realistic neural amp simulations instead of generic synth.

Source for captures: [tone3000.com](https://www.tone3000.com), which
hosts user-contributed NAM models and IRs filterable by gear type and
brand.

Confirmed design decisions (in conversation, before code):

- **Chain piece-by-piece**: amp NAM + cab IR + pedals/racks as NAM/IR/DSP
  when available (not one full-rig capture).
- **Manual deep-link as default**, **automatic batch as opt-in** once an
  API key is configured.
- **Extract Rocksmith's own IRs from the game** for cabs/pedals/racks
  where possible (deferred — see "v2 work" below).
- **Conservative auto policy**: CC0/CC-BY licenses only, ≥50 downloads,
  prefer standard-size captures.
- **Batch over the whole library** (no setlist selector — just one
  button).

---

## Plugin layout

```
plugins/rig_builder/
├── plugin.json              # nav: "Rig Builder", screen, script, routes
├── extract_gear_map.py      # standalone script: gears.psarc → rs_to_real.json
├── extract_irs.py           # standalone script: gears.psarc → 444 cab IRs as .wav
├── rs_to_real.json          # 613 entries: pseudonym/real RS gear → make/model + tone3000 search hints
├── rs_cab_to_ir.json        # 444 entries: RS cab entity → list of extracted IR file paths
├── tone3000_client.py       # REST client + deep-link builder, sqlite cache
├── routes.py                # FastAPI endpoints + DB migration + batch worker
├── screen.html              # 4-tab UI shell
├── screen.js                # UI logic
└── HANDOFF.md               # this file
```

Total ~50 KB of code + ~360 KB of generated JSON.

The extracted IRs themselves live in
`<config_dir>/nam_irs/rocksmith/*.wav` (~16 MB, 444 files, 48 kHz mono
float32) — outside the plugin dir on purpose, so the nam_tone runtime
finds them through its normal IR resolution path.

---

## How Slopsmith loads plugins

Slopsmith is an Electron app whose Python backend lives in
`/Applications/Slopsmith.app/Contents/Resources/slopsmith/`. The host
scans `~/Library/Application Support/slopsmith-desktop/plugins/*/plugin.json`
at startup (search `server.py` for `load_plugins`). For each plugin it
imports the file pointed to by `routes` and calls:

```python
plugin_module.setup(app, context)
```

The **`context` dict** we rely on:

| Key | Type | Notes |
|---|---|---|
| `config_dir` | `Path` | The slopsmith-config dir; `nam_tone.db`, `nam_models/`, `nam_irs/` live here. |
| `get_dlc_dir` | `() -> Path` | Returns the user's DLC dir (CDLC library). |
| `get_sloppak_cache_dir` | `() -> Path \| None` | Optional — where to unpack sloppaks. |

There is **no hot reload**. The user must close + reopen Slopsmith for
new/edited plugin code to take effect.

---

## Critical: sister plugin `nam_tone`

A plugin called `nam_tone` already ships **inside the app bundle** at
`/Applications/Slopsmith.app/Contents/Resources/slopsmith/plugins/nam_tone/`.
It owns:

- `nam_tone.db` — tables `presets` (one `model_file` + one `ir_file` per
  preset) and `tone_mappings` (filename + tone_key → preset_id).
- `/api/plugins/nam_tone/models` — upload/list/delete `.nam` files in
  `slopsmith-config/nam_models/`.
- `/api/plugins/nam_tone/irs` — same for `.wav` IRs in
  `slopsmith-config/nam_irs/`. **Important:** the IR upload runs the
  file through `ffmpeg` to normalize to **48 kHz mono float32 WAV** —
  the browser's `decodeAudioData` is picky. Raw IRs that aren't this
  format will fail to load at playback.
- `/api/plugins/nam_tone/native-preset/{id}` — builds a chain array
  `[{type:1, ...amp...}, {type:2, ...cab...}]` and passes it to the
  native audio engine via `api.loadPreset()`. **The engine accepts a
  chain but the current loader only emits 2 stages** (amp NAM + cab
  IR). Multi-stage pedal/rack support would need changes inside the app
  bundle, not in this plugin.

`rig_builder` **does not duplicate** any of nam_tone's tables or
endpoints — it writes into the same `presets` / `tone_mappings` rows
and uploads files through nam_tone's upload endpoints from the UI.

What we **add** to `nam_tone.db`:

```sql
CREATE TABLE preset_pieces (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  preset_id INTEGER NOT NULL,
  slot_order INTEGER NOT NULL,
  slot TEXT NOT NULL,                 -- pre_pedal|amp|post_pedal|rack|cabinet
  rs_gear_type TEXT NOT NULL,         -- e.g. "Amp_AT120", "Cab_4x12_AT_CL"
  kind TEXT NOT NULL,                 -- nam | ir | rs_ir | dsp | none
  file TEXT,                          -- relative to nam_models/ or nam_irs/
  params_json TEXT NOT NULL DEFAULT '{}',
  tone3000_id INTEGER,
  assigned_mode TEXT,                 -- auto | manual
  bypassed INTEGER NOT NULL DEFAULT 0,-- v3.7: per-piece bypass (pass-through)
  FOREIGN KEY (preset_id) REFERENCES presets(id) ON DELETE CASCADE
);
```

Migration is **idempotent** (`CREATE TABLE IF NOT EXISTS` in
`_get_conn()` in `routes.py`). Safe to run repeatedly.

`preset_pieces` is **informational today** — the nam_tone runtime only
reads `presets.model_file` and `presets.ir_file`. Persisting the full
chain prepares for the day the audio engine accepts pedal/rack stages.
When we save a preset, we pick the **amp slot's NAM** as `model_file`
(by priority `amp > rack > post_pedal > pre_pedal`) and the
**cabinet slot's IR** as `ir_file` — see `_persist_preset_chain` in
`routes.py`.

---

## Rocksmith gear naming

Rocksmith ships `gears.psarc` containing 613 gear entities. Each has:

- An entity name in one of two formats:
  - **Real-brand** — e.g. `Amp_Marshall1962Bluesbreaker`,
    `Amp_MarshallJVM410H`. ~32 of 60 amps are like this.
  - **Pseudonym** — e.g. `Amp_AT120`, `Amp_BT45`, `Amp_CA100` —
    licensed-but-anonymized; the in-game art is modeled on a real amp
    the community has documented.
- A `ThreeDArtAsset` URN whose slug often carries the real make/model
  (`marshall-bluesbreaker`, `epiphone-zephyr-amp20`) — even when the
  entity name is a pseudonym.
- A `SoundBank` URN pointing at a Wwise `.bnk` file inside `gears.psarc`
  (relevant for the v2 IR-extraction work below).

`extract_gear_map.py` parses every `gamexblocks/ngears/*.xblock` file
in `gears.psarc` and produces `rs_to_real.json` keyed by entity name.
It uses three signal sources, in priority order:

1. A known brand prefix in the entity name (`Marshall`, `Fender`,
   `Vox`, `Mesa`, etc.).
2. A hardcoded `_PSEUDONYM_OVERRIDES` dict for the AT/BT/CA/EN/HG/TW
   amp series (community-documented best-guesses).
3. The 3D art asset slug as a fallback.

The output is **plain JSON** — editable by hand to fix any specific
mapping. To regenerate after editing the script's overrides, use the
"Regenerate gear map" button in the Settings tab (calls
`/api/plugins/rig_builder/extract_gear_map`).

**Query-tier strategy (v3.5).** `build_mapping` now separates `model`
(display only) from `tone3000_query` (what we search), and records which
tier resolved each gear in a new `query_source` field. Tiers, in order:

1. `override` — exact `_PSEUDONYM_OVERRIDES` hit → specific query
   ("Marshall JCM 800"). The popular amps with many tone3000 captures.
2. `brand_name` — a real brand prefix is in the entity name.
3. `series` — codename family in `_SERIES_PREFIX_OVERRIDES`. **Brand-only
   query** ("Vox", "Peavey"); the codename number is kept in `model`/
   display but deliberately **excluded** from the query. Reason: the
   number (Amp_HG500 → "500") is a Rocksmith-internal id, not a real
   product number — `"Vox 600B"`/`"Marshall 38"` returned ~zero tone3000
   hits and the gear stayed pending forever. Bass series queries are
   anchored with "bass".
4. `art_asset` — kebab/underscore art slug names a real brand. **Gated on
   the entity NOT being a codename** (`sp is None`): for a codename family
   the slug is just the codename again ("bt-600b"), which previously
   leaked back as "Bt 600B". Catches real brands missing from
   `_BRAND_PREFIXES` (GibsonGA8, EpiphoneZephyr).
5. `generic` — unknown amp codename family. Falls to "guitar amp" /
   "bass amp" so the user still gets pickable candidates instead of a
   dead-end codename query. Flagged so the UI can mark it approximate.
6. `fallback` — non-amp (cab/pedal/rack) with nothing resolved; keeps the
   camel-split codename (cabs use RS IRs so their query rarely matters).

**Series table is keyed by `(instrument, prefix)`** — instrument is
"guitar"/"bass" — because Rocksmith reuses prefix letters across both
(BT is a Vox AC on guitar but a different head on bass; CS appears in
both). A single `(category)` key mis-mapped the bass variants.

Seeded: (guitar) AT→Marshall, BT→Vox, CA→Mesa Boogie, EN→ENGL,
HG→Peavey, TW→Fender Tweed. **Unconfirmed / on the generic floor**
(commented out, awaiting the user's domain knowledge): CS, GB (guitar);
BT, CH, CS, HT, LT (bass). Filling one in + regenerating upgrades the
whole family from `generic` to `series`. GT doesn't exist in the base
game. Editing either dict requires regenerating the map (Settings →
"Regenerate gear map").

**Known imperfect mappings** (extend `_PSEUDONYM_OVERRIDES` or
`_SERIES_PREFIX_OVERRIDES` to fix):
- `Amp_EN30`, `Amp_EN15`, `Amp_CA75`, `Amp_HG100` — not in the override
  dict, fall back to camel-split pseudonym ("En30", "Ca75").
- Several `Bass_Amp_*` and `Bass_Cab_*` keep the "Bass" prefix in
  `tone3000_query`, which makes the search noisier.
- `Cabinets` (literal) — appears in some legacy CDLC tones as a
  catch-all, no specific cab to map.

None of these break anything — the deep-link search still opens, the
user can edit the JSON, the override dict can be extended.

---

## tone3000 API state (read this twice)

The tone3000 REST API at `https://www.tone3000.com/api/v1/*`
**requires Bearer-token auth on every endpoint**, including search and
the public-looking `/tones/{id}`. Verified by direct HTTP probes — all
return `401 Unauthorized` without a key.

**Self-service key signup (current as of 2026-05):** the user creates
a publishable key via tone3000.com → Settings → API Keys → Create API
Key. The key has prefix `t3k_pub_…` and is a long-lived Bearer token
that goes straight into `Authorization: Bearer <key>`. Their older
docs still mention "contact support@tone3000.com" but the official
TS client at https://github.com/tone-3000/api confirms self-service
is now available. **Do not draft a support email for the user — they
just create it on the site.**

So `rig_builder` operates in **two modes**:

### Deep-link mode (no key, default)

`tone3000_client.Tone3000Client.build_search_url(query, gears, platform)`
produces URLs like:

```
https://www.tone3000.com/search?query=Marshall+JCM+800&platform=nam&gears=amp
```

These return HTML pages (200 OK) that the user opens in their browser,
picks a capture, downloads the `.nam`, then drops it into the upload
input in the "Por canción" tab. The plugin proxies that upload to
`POST /api/plugins/nam_tone/models` (or `/irs` for cabs), then
`POST /api/plugins/rig_builder/save_preset` writes the chain.

### API mode (with key)

User pastes a key in Settings → stored in
`slopsmith-config/rig_builder_settings.json` → `Tone3000Client.has_api_access`
flips to `True` → the "Sugerir" modal lists actual candidates inline,
and the batch worker can score top candidates per gear (still doesn't
auto-download in v0 — the batch only registers entries, the user
provides files via the UI).

The client caches responses in `slopsmith-config/rig_builder_cache.db`
with a 7-day TTL so the 100 req/min rate limit isn't a concern when
the user later runs library-wide batches.

**Broken `url` field gotcha (v3.4).** Each tone in the search payload
carries a `url` field, but it's malformed: production returns a slug
path with a stray double slash, e.g.
`https://www.tone3000.com//badcat-lynx-50w-el34-30122`. Both that and
the de-duplicated single-slash slug **404**. The canonical public page
is `/tones/{id}` (verified 200 against production). So the `/search`
endpoint builds candidate links via `Tone3000Client.tone_page_url(id)`
and **ignores the API's `url` field**. If a future API revision starts
returning a working `url`, this is safe to revisit — but don't trust
that field blindly; it shipped broken in May 2026.

---

## Bundled Python (gotcha for any subprocess)

Slopsmith bundles its own Python at:

```
/Applications/Slopsmith.app/Contents/Resources/python/runtime/bin/python3.12
```

This is the **only** interpreter on the system with `pycryptodome`,
which is required to decrypt PSARC files (see
`/Applications/Slopsmith.app/Contents/Resources/slopsmith/lib/psarc.py`).
The user's system `python3` (especially conda envs) may fail with
`_cffi_backend` version mismatches if it tries to import `Crypto`.

When `extract_gear_map.py` is invoked from `routes.py` via
`/api/plugins/rig_builder/extract_gear_map`, it uses `sys.executable`
to inherit the host's interpreter — that's the bundled one when
running inside Slopsmith, so it always works.

When running `extract_gear_map.py` from a shell:

```bash
/Applications/Slopsmith.app/Contents/Resources/python/runtime/bin/python3.12 \
    extract_gear_map.py /path/to/gears.psarc
```

The script's top of file adds `slopsmith/lib` to `sys.path` so the
`psarc` import resolves.

---

## API surface (routes.py)

| Method | Path | Notes |
|---|---|---|
| GET | `/api/plugins/rig_builder/status` | Setup status + coverage stats + API-access state + rs_cab_to_ir state |
| POST | `/api/plugins/rig_builder/extract_gear_map` | Runs `extract_gear_map.py` against a user-supplied `gears.psarc` |
| POST | `/api/plugins/rig_builder/extract_irs` | Runs `extract_irs.py` against a user-supplied `gears.psarc` |
| GET / POST | `/api/plugins/rig_builder/settings` | Get / update plugin settings (API key, policy) |
| GET | `/api/plugins/rig_builder/song/{filename:path}` | Parse + enrich a PSARC/sloppak. Returns each tone with its chain plus per-piece deep-links and existing assignments. |
| GET | `/api/plugins/rig_builder/search?rs_gear=...` | Per-gear candidates (when API key) + deep-link |
| POST | `/api/plugins/rig_builder/save_preset` | Persists preset + preset_pieces + tone_mapping for a single tone |
| POST | `/api/plugins/rig_builder/download_for_gear` | (v3.3) Pull a specific tone3000 capture for one rs_gear, save into nam_models/ or normalize into nam_irs/. Body `{rs_gear, tone3000_id}`. After downloading it calls `_assign_file_to_gear` to stamp the file onto every `preset_pieces` row for that gear and recompute primaries. Returns `{kind, file, pieces_updated, presets_updated}`. |
| POST | `/api/plugins/rig_builder/auto_download_song` | (v3.1) Same as the batch worker but scoped to one filename. Triggered by `screen.js:rbAutoDownloadSong` when the user opens a song with an API key configured **and** by the background materialization watcher (v3.2). Returns `{processed, downloaded, rs_ir_used, skipped_assigned, skipped_no_candidate, failed}`. |
| POST | `/api/plugins/rig_builder/batch_all` | Kicks off the library-wide worker. Body `{mode}`: `new` = map only tones without a preset; `all` = remap every tone (re-resolves captures, preserves per-tone bypass + cab-IR variant). |
| GET | `/api/plugins/rig_builder/batch_status` | Progress + log (polled by UI every 1s while running) |
| GET | `/api/plugins/rig_builder/coverage` | Aggregates preset_pieces — pending vs assigned per rs_gear |
| GET | `/api/plugins/rig_builder/list_songs?q=...` | DLC dir listing (recursive) — joins with web_library.db so titles/artists/years from the host's library cache surface to the UI. Search matches title/artist/album/filename. |
| GET | `/api/plugins/rig_builder/native_preset_full/{preset_id}` | Full multi-stage chain (every NAM + VST + cab IR, honouring bypass) in nam_tone's native_preset shape. The fetch redirect points nam_tone's `native-preset/{id}` here for full-chain playback. |
| GET | `/api/plugins/rig_builder/native_preset_one?file=&kind=` | One-stage native_preset from a single file — Gear-tab / candidate ▶ audition. Now also supports `kind=vst&vst_path=...` for VST audition. |
| GET | `/api/plugins/rig_builder/gear_catalog` | Gears grouped by category with parenting (real make/model + assigned capture/VST) + a tone3000 photo. Powers the Gear tab. |
| POST | `/api/plugins/rig_builder/audition_candidate` | Body `{rs_gear, tone3000_id}`. Downloads a candidate (no assign) so the Suggest modal's ▶ can audition it. Returns `{kind, file}`. |
| POST | `/api/plugins/rig_builder/export_default_captures` | Snapshots the current DB's gear→capture choices into `default_captures.json`. Returns `{count}`. (Settings → "Export defaults".) |
| GET | `/api/plugins/rig_builder/local_files?kind=nam\|ir` | Lists locally-downloaded NAMs (`nam_models/*.nam`) or IRs (`nam_irs/**/*.wav` recursive, so the 888 Rocksmith-extracted cab IRs are included). Each entry has `use_count` + `used_for_gears` so the UI can sort by most-used. Powers the Library picker in both Songs and Gear tabs. |
| POST | `/api/plugins/rig_builder/use_local_for_gear` | Bulk-assign an already-local file to every `preset_pieces` row for an rs_gear_type. Skips the tone3000 round-trip. Body `{rs_gear, local_file, local_kind}`. |
| GET | `/api/plugins/rig_builder/vst/known` | Returns the cached list of installed VST3/AU plugins (populated by the frontend after a successful `scanPlugins()`). |
| POST | `/api/plugins/rig_builder/vst/sync_known` | Frontend pushes the result of `getKnownPlugins()` so the dropdown survives a server restart. Body `{plugins: [...]}`. |
| POST | `/api/plugins/rig_builder/vst/scan` | Triggers the native engine's `scanPlugins()` via JS bridge (proxied — can crash if user has a malformed VST, the UI prefers the file-picker / paste-path flow). |
| POST | `/api/plugins/rig_builder/vst/assign` | Bulk-assign a VST3/AU plugin to every `preset_pieces` row for a given rs_gear_type. Body `{rs_gear_type, vst_path, vst_format, vst_state?}`. |
| POST | `/api/plugins/rig_builder/vst/capture_state` | Persist a captured plugin state blob for an existing VST piece. Body `{rs_gear_type, vst_state, preset_id?}`. |
| GET | `/api/plugins/rig_builder/vst/knob_mapping?rs_gear_type=&vst_name=` | Looks up `rs_knob_to_vst_param.json` for the curated translation table between a Rocksmith gear's knobs and a specific VST's params. Used by the "⇶ Apply RS settings" button. |
| GET | `/api/plugins/rig_builder/vst/suggest/{rs_gear_type}` | Returns suggested VSTs (from `rs_gear_to_vst.json`) cross-referenced with the user's installed list so the UI can mark `✓ installed` vs `↓ download`. |

UI files (`screen.html` + `screen.js`) use the standard slopsmith
patterns: tailwind-like dark theme classes, `window.showScreen` hook
guarded by `__slopsmithNamRigBuilderInstalled`, fetch-based async.

---

## Cloud-on-click materialization (DONE)

Users running `cloud_loader` keep most of their DLC dir as 0-byte
placeholders — actual PSARC content lives in Google Drive and only
gets pulled when needed. rig_builder handles this:

- `list_songs` now returns `[{name, size, materialized}, …]` so the
  UI can render a `☁ cloud` chip next to unmaterialized songs.
- `GET /song/{filename}` returns HTTP 409 + `{error: "cloud_only",
  filename, hint}` when the file is 0 bytes, instead of trying to
  parse and failing with a noisy ValueError.
- `screen.js`'s `rbLoadSongTones` reacts to the 409 by calling
  `POST /api/cloud_loader/materialize?filename=…`, showing a
  "Descargando desde Google Drive…" status, then retrying `/song`
  once the Drive download finishes. The user clicks the song, the
  download is automatic, and the chain shows up when ready.
- The batch worker's `_list_library_songs` already filters 0-byte
  stubs (they show as "X cloud-only placeholders" in the log) so a
  cloud-heavy library doesn't drown the log in parser errors.

Trade-off documented for the user: large libraries can't be batch-
materialized from rig_builder (would download GB from Drive without
explicit consent). The recommended flow is materialize-on-demand:
click songs you actually want to map, and the chain unlocks one at
a time as Drive downloads complete.

**Persistence across cache evictions.** `nam_tone.db`, `nam_models/`,
and `nam_irs/` all live under `slopsmith-config/` — completely
independent from the DLC dir and `sloppak_cache/` that the cloud
layer manages. When a song gets evicted from the local cache (back
to a 0-byte stub) and later re-materialized:

- `nam_tone.db` still has its `presets` + `preset_pieces` +
  `tone_mappings` rows for that filename. The lookup hits as soon
  as the file is back on disk.
- The downloaded `.nam` / `.wav` files in `nam_models/` and
  `nam_irs/` are named by tone3000 tone+model id, not by song. So
  they're shared across every song that uses the same gear and
  survive any cache cycling.
- Net effect: cache → cloud → cache cycle produces zero extra
  tone3000 calls, zero re-downloads, and the song plays through
  the same NAM the moment it's back.

---

## Background materialization watcher (v3.2 — DONE)

Closes the last gap in the "download and just play" flow. Before this,
auto-download only fired when the user opened a song **from the Tone
Bridge tab**. Playing a song from Slopsmith's main view only triggers
`cloud_loader` to drop the real PSARC on disk — `rig_builder` never
heard about it, so the song played with the generic synth until the
user later visited Rig Builder.

The watcher is a daemon thread started in `setup()` (`_start_watcher`)
that polls the DLC dir every `_WATCH_INTERVAL_SEC` (5s):

- `_watch_scan_dlc()` stats every `.psarc`/`.sloppak` → `{name: size}`.
- `_watch_tick()` diffs against the previous tick:
  - **First tick primes a baseline and fires nothing.** We only react
    to transitions *during the session* — firing for every already-
    materialized song at startup would replicate the library-wide
    batch and could blow the disk budget.
  - A file going **0-byte → non-zero**, or a **brand-new** non-zero
    file appearing (manual copy, another plugin's extractor), is the
    trigger. This is intentionally decoupled from `cloud_loader` — it
    reacts to the file existing on disk, not to any specific code path.
  - **Two-phase stability confirmation**: a transition is parked in
    `_watcher_pending` and only fired on the *next* tick if the size is
    unchanged. `cloud_loader.download_to` already writes atomically
    (`.part` → `replace`), so its files are stable on the first
    confirming tick; the check exists to defend against non-atomic
    writers that stream into the destination. Worst-case latency
    "materialized → download starts" is ~2 ticks (~10s).
- Confirmed songs go to `_watch_fire()` → `_auto_download_for_song()`,
  the same helper the HTTP endpoint uses (serialized via `_auto_lock`
  so the watcher and a manual open don't race on `_batch_disk_bytes`).

**Gating.** Firing is skipped (but the baseline still advances) when
`auto_watch` is False in settings, or when no tone3000 key is
configured. Because the baseline keeps advancing, adding a key later
does **not** retro-fire the whole library — only songs materialized
after the key is added. `auto_watch` defaults to True in
`_DEFAULT_SETTINGS`.

**Visibility.** `/status` now returns a `watcher` block
(`running`, `primed`, `fired_count`, `last_fired`, `last_error`,
`interval_sec`) so the UI/operator can confirm it's alive.

Net flow now: play a song in Slopsmith → `cloud_loader` materializes
the PSARC → within ~5-10s the watcher detects it and downloads the NAM
chain → ~10-30s later the chain is in `nam_tone.db`. First play may
still be generic synth (download in flight); by the time the file is
ready the NAM engine picks up the preset. For "instant on first play"
the alternative would be hooking `cloud_loader/materialize` directly —
not done (more invasive, couples the two plugins). See the trade-off
discussion in the conversation that produced this.

---

## Cab IR extraction (v2 — DONE)

`extract_irs.py` pulls all 444 cab IRs out of Rocksmith's
`gears.psarc` and writes them as 48 kHz mono float32 WAVs into
`<config_dir>/nam_irs/rocksmith/`. The mapping from RS entity name to
extracted IRs lives in `rs_cab_to_ir.json`. The batch worker prefers
these over tone3000 for any cab piece, and the per-song UI shows a
"Rocksmith IR" green strip on each cab piece with a dropdown for the
mic-position variants (9 per cab typically).

### Rocksmith cab IR binary format

Discovered by reverse-engineering the cab `.bnk` files:

- Container: standard Wwise SoundBank (`BKHD` / `DIDX` / `DATA` /
  `HIRC` / `STID` chunks). `_parse_bnk()` in `extract_irs.py` walks
  these.
- Each `DIDX` entry's blob inside `DATA` is **NOT** a RIFF .wem.
  Instead it's a custom 16-byte header followed by raw PCM:

    bytes 0-3   : u32 LE  = 128         (constant; meaning unknown — maybe header_size or codec_id)
    bytes 4-7   : u32 LE  = 256         (constant; meaning unknown — maybe flag set)
    bytes 8-11  : u32 LE  = sample_rate (always 48000 in base game cabs)
    bytes 12-15 : u32 LE  = channels    (always 1 in base game cabs)
    bytes 16+   : float32 LE PCM samples

- Empirically every cab IR in the base game's gears.psarc is mono
  48 kHz float32, 55-280 ms long (mean 187 ms), with peak energy in
  the first millisecond — textbook cab IR.
- Peak amplitudes can exceed 1.0 — that's expected for IRs (the
  impulse response is a scale factor, not normalized PCM).

If a future Rocksmith version adds DLC cabs at a different sample
rate or channel count, the extractor checks the header and skips
unfamiliar configurations rather than producing garbage.

### What other PSARCs don't add

Verified by scanning the user's full set of base-game PSARCs:

- `session.psarc` (359 MB, Session Mode): 476 `.bnk` of backing
  tracks / band-mate audio loops. All standard Wwise RIFF .wem.
- `etudes.psarc` (2.3 GB, lesson audio): 187 `.bnk` of guitar
  exercises. All standard RIFF .wem.
- `audio.psarc` (1.3 GB): 21 `.bnk` of UI sounds (boot, crowd noise).
  All standard RIFF .wem.
- `cache.psarc`, `static.psarc`, `guitars.psarc`: no `.bnk` files.

None contain cab IRs in the custom format. The 444 cab IRs from
`gears.psarc` are the complete set.

---

## What was verified (live against the real tone3000 API)

A real `t3k_cs_…` secret key was used to drive the batch worker
against the user's actual library (May 2026). Results:

- `search_tones` succeeded for every queried gear (e.g. "Marshall
  JCM 800" returned 140 captures, top one with 38k downloads).
- `list_models` resolved a `standard`-size NAM for 14/17 unique
  gears parsed across 3 materialized songs.
- `download_model_file` initially failed with 401 — fixed by
  adding the Bearer header (see "Auth gotcha" above).
- Each `.nam` file written is a valid WaveNet model JSON
  (`version: 0.5.0`, `architecture: WaveNet`, plus
  `config` and `weights`). Sizes range 276 KB – 408 KB for
  `standard` captures.
- The cab IR downloaded from tone3000 is a clean 48 kHz mono
  float32 RIFF/WAVE — drops straight into nam_tone without
  needing ffmpeg normalization (the format happened to already
  match what nam_tone wants; the ffmpeg step in
  `_ffmpeg_normalize_ir` remains as defense against other tones
  that may use different formats).
- 12 presets persisted to `nam_tone.db` with the full chain in
  `preset_pieces`. `model_file` and `ir_file` correctly populated
  via the slot-priority picker (amp > rack > post_pedal >
  pre_pedal).

## What was verified (in-process smoke tests)

Six smoke tests, all green:

1. `_persist_preset_chain` picks the amp's NAM as the primary
   `model_file` even when a pre-pedal NAM appears first in the chain.
2. `/status` returns the right gear count (613) and recognizes no API
   key is configured.
3. `/list_songs?q=…` finds songs in the user's real DLC dir.
4. `/search?rs_gear=Amp_AT120` returns query="Marshall JCM 800" and a
   well-formed deep-link URL (no API key needed).
5. `/coverage` correctly aggregates `preset_pieces` by `rs_gear_type`
   with pending/total counts.
6. `/song/<real CDLC>` parses tones and returns enriched chains; a
   corrupt PSARC returns 400, not 500.

Manual checks against the live tone3000 server:
- All `/api/v1/*` endpoints → 401 without auth.
- `/search?query=…` (public web page) → 200 OK (HTML).
- Deep-link URLs verified to open correct prefiltered pages.

---

## Primary-NAM picker: amp/rack only (v3.6 — DONE)

`_MODEL_SLOT_PRIORITY` (shared by `_persist_preset_chain` and
`_recompute_preset_primaries`) is now `("amp", "rack")` — pedals are
**not** eligible to become a preset's primary `model_file`. Before, the
priority included `post_pedal`/`pre_pedal`, so a song whose amp was still
unmapped but whose pedal had a NAM would play the **pedal as if it were
the amp** (wrong/weak tone — the user's "solo toma el .nam del pedal y no
suena"). Now, with no amp/rack NAM the primary stays empty and the amp
surfaces as pending instead. The engine plays one NAM anyway (see
not-done #1), so this only changes *which* NAM, never the count.

## Per-tone live preview "Listen" + full-chain test (v3.6 — DONE)

The per-song tone cards have a `▶ Listen` button left of `Save preset`
(`rbRenderTone` → `rbListenTone` in `screen.js`). It persists the tone's
current selection (shared `rbPersistTone`, returns the `preset_id` from
`save_preset`) then previews it as a **live input monitor** (play your
guitar, hear it through the chain).

**It sends the WHOLE chain, on purpose** — this is the direct multi-NAM
experiment. `GET /api/plugins/rig_builder/native_preset_full/{preset_id}`
builds a native_preset with **every** NAM piece as its own type-1 stage
(ordered by `_CHAIN_NAM_ORDER` = pre_pedal → amp → post_pedal → rack; a
*stable* sort preserves the original `slot_order` among multiple pedals in
the same slot, e.g. Reptilia_dist's 2 pre-pedals) plus the cab IR as
type-2. `rbListenTone` loads it straight into the native engine
(`window.slopsmithDesktop.audio`: clearChain → loadPreset → setGain →
setMonitorMute(false) → startAudio) and **logs `slotsLoaded`** to the
console. That number vs the chain length is the verdict on whether the
engine chains multiple NAMs:

- `slotsLoaded == chain.length` → the engine accepts all NAM stages →
  multi-NAM (pedal→amp→cab, incl. 2+ pre-pedals) works in preview today,
  no bundle changes. Real song *playback* still goes through nam_tone's
  2-stage `get_native_preset`, so that part would need the same full-chain
  builder upstream (or a bundle patch — discouraged, see not-done #1).
- `slotsLoaded < chain.length` → engine caps stages (a `console.warn`
  fires). Fall back to full-rig captures (one NAM = pedal+amp).

Notes / gotchas:
- This bypasses nam_tone's preview, driving the native engine directly, so
  it owns audio while active. `rbStopPreview()` (called on toggle-off, tone
  re-render, and leaving the screen) mutes + clears + stops audio it
  started. If there's **no** native engine (browser/WASM-only), it falls
  back to `window.namStartPresetTest` (single NAM).
- `_state_b64` / `_safe_child` in `routes.py` are byte-for-byte copies of
  nam_tone's so the stage `state` blobs + absolute paths match exactly.
- Previewing **persists** the preset (the engine only loads a saved id).
- No hot reload: the user must restart Slopsmith for this to take effect.

## Per-stage bypass + immediate gear refresh (v3.7 — DONE)

- **Per-piece Bypass button** on each tone card (`rbToggleBypass`). Sets
  the chain stage's `bypassed=true`, which makes the engine **pass the
  signal through** that stage (NOT silence it) — so you can audition each
  amp/pedal in or out without breaking the chain. While previewing, a
  toggle reloads the chain live (`rbReloadPreview`, no audio restart).
- **Bypass is persisted** (`preset_pieces.bypassed`, migrated via guarded
  `ALTER TABLE`). `save_preset`/`_persist_preset_chain` store it,
  `native_preset_full` and `/song` read it back (the `/song` read is
  **scoped to the tone's preset_id** so a bypass in another song doesn't
  bleed across — the gear-global `assigned` lookup can't distinguish
  songs). A bypassed amp/rack/cab is also excluded from the bundle's
  single-NAM `model_file`/`ir_file` (`_persist_preset_chain` +
  `_recompute_preset_primaries`), keeping real-playback consistent.
- **Immediate gear refresh** (`rbAfterGearChange`): uploading a file,
  assigning a Rocksmith IR, or download-and-assign now re-render the open
  song from in-memory state and (if that tone is previewing) re-save +
  reload the chain — no more re-selecting the song. `rbRenderPiece` reads
  `_uploaded_file` (pending change) before `assigned.file`.

**Regression watch (fixed 2026-05-23):** `rbRenderPiece` once referenced a
removed `assigned` local in the file-label `title=` after the `_uploaded_file`
refactor → it threw inside `tones.map()` → `el.innerHTML` never set → the
song panel hung on "Loading…" forever. `rbLoadSongTones` now wraps the
render in try/catch so a render throw shows an error instead of hanging.
Lesson: this UI builds HTML via template strings in `.map()`; one
`ReferenceError` there silently kills the whole list.

## Full-chain REAL playback via fetch redirect (v3.8 — DONE)

The remaining gap (chain only in preview, amp+cab in real play) is closed
**without editing the bundle**. Real playback resolves tone → preset_id →
`_namApplyNativePreset` (nam_tone/screen.js, module-scoped) which does
`fetch('/api/plugins/nam_tone/native-preset/{id}')` — the bundle's 2-stage
builder. rig_builder's screen.js (loads globally) **monkey-patches
`window.fetch`** to redirect *only* that exact URL to
`/api/plugins/rig_builder/native_preset_full/{id}` (identical response
shape), so the engine receives every NAM stage.

Why this way: the bundle is code-signed and wiped on update, and
`_namApplyNativePreset` isn't on `window` to wrap. Patching the one fetch is
plugin-only, update-proof, and the single seam both preview and playback
share. Safety: the patch is scoped to a strict regex, passes every other
request straight through, and falls back to the original 2-stage endpoint if
the full-chain build is not-ok / empty / unparseable / throws. Kill-switch:
`window.__rbChainPlayback = false` (console) disables the redirect.

Caveat: depends on the native engine accepting multiple type-1 stages
(verified via the preview `slotsLoaded` test). WASM-only installs stay
single-NAM (the worklet holds one model); the redirect is harmless there.
The full-chain payload carries extra per-stage `slot`/`rs_gear` keys (for
the UI's bypass mapping) which the engine ignores — confirmed by the
working preview.

## Gear catalog "Gear" + single-stage audition + photos (v3.9 — DONE)

New tab **Gear** (`rbLoadCatalog` / `rbRenderCatalogCard`, nav + panel
in `screen.html`, case in `rbShowTab`). Backend `GET /gear_catalog`
aggregates `preset_pieces` per gear (best row: file-bearing > latest),
enriches via `rs_to_real.json` (real make/model + category), groups by
category (amp / pedal / cab / rack / other), and resolves a **photo** from
the tone3000 capture (`_tone_image_index` reads the local
`rig_builder_cache.db` → each Tone's `images[0]`). Each card shows what the
gear is parented to (real name + assigned capture/file), the photo, a
tone3000 link, and ▶ to audition.

- **Photos** are tone3000 **capture** images (the real modeled gear),
  available only for captures whose uploader added an image; otherwise a
  "sin foto" placeholder. Rocksmith's own gear art is NOT used (3D assets,
  not locally extractable; `art_cache/` holds only song cover art).
- **Single-stage audition:** `GET /native_preset_one?file=&kind=` builds a
  one-stage native_preset; `rbAuditionFile` loads it into the engine to
  hear that gear **in isolation**. The catalog ▶ uses it directly.
- **Search-candidate audition:** the Suggest modal now shows each
  candidate's photo (from `/search`'s `images`) + a ▶ that calls
  `POST /audition_candidate` → `_download_candidate` (download to
  nam_models/nam_irs, **no assign**) → `rbAuditionFile`. tone3000's API has
  no audio clip field, so "listen" = download-then-audition (user's choice).
- Audition shares the native engine with the tone preview via
  `rbStopPreview` (now resets both the per-tone listen button and the
  audition button; `rbState._auditionId` tracks the active ▶).
- Catch-all RS entities not in `rs_to_real` (`Cabinets`, `Pedals`,
  `DI_Amp_TubePre`) fall under "Otros" — expected; many CDLC use the
  generic `Cabinets` entity rather than a specific cab.

## Fix: re-assigning a gear now replaces the old NAM (2026-05-24)

`_assign_file_to_gear` (the manual "Download and assign" path, sole caller)
previously updated only *pending* preset_pieces rows (`kind='none'`/empty
file). So picking a new capture for a gear that was **already assigned**
changed nothing — it kept the previous NAM (visible in the Gear tab). It now
updates **every** row for that gear (replacing the file across all presets
that use it) and recomputes their primaries. Auto/batch flows are unaffected
(they use `_persist_preset_chain` / `_download_candidate`, not this). The Gear
tab also reloads after a download (`rbDownloadForGear` → `rbLoadCatalog` when
`currentTab === 'gear'`).

## Fix: per-song bypass + gear changes auto-save (2026-05-24)

Per-song **Bypass** toggles and gear swaps (upload / RS-IR assign) used to
live only in memory (`_bypassed` / `_uploaded_file`) and persist **only** on
the explicit "Save preset" (or ▶ Listen) — so toggling Bypass or swapping a
file then navigating away/restarting lost the change. Now `rbToggleBypass`
and `rbAfterGearChange` call `rbPersistTone` immediately, so per-song changes
auto-save. (Round-trip verified: persisted `bypassed` reads back via
`/song`'s preset-scoped `bypass_map`.) The "Save preset" button still exists
(explicit save + confirmation).

## Default capture map (ship curated gear→tone choices) (2026-05-25)

`default_captures.json` (shipped in the plugin) maps
`rs_gear → {tone3000_id, kind, model_id}` — a curated default of which
tone3000 capture to use per gear. The **batch** and **per-song
auto-download** flows now prefer this map over a fresh tone3000 search
(`_load_default_captures()`), so a new install reproduces the maintainer's
exact tone choices (with an API key to download the files; the files
themselves are NOT shipped — only the ids). Regenerate from the current DB
via Settings → "Export defaults" (`POST /export_default_captures` →
`_build_default_captures()`), or it's a one-off snapshot. Gears not in the
map fall back to search/pick as before.

## Two batch modes + cab fixes (2026-05-25)

**Batch modes.** The Dashboard has two buttons; `POST /batch_all` takes
`{mode}` and `_batch_worker(mode)` honours it:
- `new` — **Map new songs only.** Skips any tone that already has a preset
  (`tone_mappings` row) → never touches existing config. Use it to fill in
  newly-added songs.
- `all` — **Remap all.** (Re)maps every tone, re-resolving captures
  (preferring `default_captures.json`), but **preserves each tone's saved
  bypass and chosen cab-IR variant** (it loads the existing pieces into
  `existing_by_gear` first and carries `bypassed` + the rs_ir variant into
  the rebuilt chain). Caveat: a manually-chosen capture for a gear NOT in
  `default_captures.json` IS re-resolved by search.

**Cab categorization (`_gear_category`).** Catch-all entities (`Cabinets`,
`Pedals`, `DI_Amp_*`) aren't in `rs_to_real.json`, so `/search` and
`download_for_gear` used to default them to category `amp` — the cab's
Suggest searched amp NAMs and downloaded cabs as NAMs. `_gear_category(rs_gear)`
now guesses cab/pedal/rack from the entity name, so cabs search the IR
platform and download as IRs.

**Raw-IR fallback.** `_download_candidate`'s IR path now falls back to a raw
file copy when ffmpeg normalization fails (matches nam_tone's IR upload),
instead of dropping the assignment — so a cab still gets assigned even
without a working ffmpeg.

**Bypass display on reload (`rbSeedBypass`).** `/song` returns the persisted
`bypassed` correctly, but the auto-download re-fetch used to re-render
without re-seeding the UI flag, so bypass looked off after reload on songs
with unmapped pieces. `rbSeedBypass(data)` is now called after every `/song`
fetch (initial load + the auto-download re-fetch).

## Fix: VST editor crash + master/chain VST state in real playback (2026-05-25)

Two Discord/tester bugs in the chain editor's VST flow:

**1. Crash after editing a VST then navigating / loading a song.** Opening the
inline VST editor calls `api.openPluginEditor(slotId)`, which spawns a NATIVE
plugin window pointing at an engine slot. Nothing closed that window when the
user left the Master tab (or loaded a song), so the next `clearChain` /
`loadPreset` (preview, audition, real playback) cleared the slot out from under
the still-open window → host crash. Fix: `rbCloseActiveVstEditor()` (screen.js)
closes the native window + drops the tracked slot, and is called at the top of
`rbShowTab`, `rbLoadSongTones`, `rbStopPreview`, and `rbListenTone` (before any
`clearChain`). `rbTeardownVstEditor` already closed-then-cleared on re-edit;
this extends that discipline to *navigation*.

> **Follow-up (same day):** the crash still fired intermittently when editing a
> master VST and then leaving the plugin to PLAY a song. The in-plugin
> teardowns don't cover leaving the rig_builder screen entirely — and the
> existing `showScreen` wrapper only stopped a running *preview* on leave, not
> an open VST editor with no preview. Fix: `rbOnLeaveRigBuilder()` closes the
> editor's native window + clears its slot whenever we leave `plugin-rig_builder`,
> wired into BOTH the `showScreen` wrapper's leave branch AND the host's
> authoritative `window.slopsmith.on('screen:changed', …)` event (the latter
> fires even when the host calls its own lexical `showScreen`, bypassing the
> wrapper). The song player's `loadPreset` then runs after the editor window is
> already gone.

**2. Master/chain VST played at plugin defaults in a real song.** The engine's
`loadPreset` restores a VST's settings ONLY from its **opaque state blob** —
the exact value `savePreset()` produces and `loadPreset()` round-trips (also
what `setSlotState` consumes; verified in the bundle's `audio_engine` save/load
path + the `slopsmith_audio.node` strings: `setSlotState`, `VST3PluginState`,
Steinberg `PresetFile`, NO `pluginState` key). rig_builder instead stored
captured params as `{"params":{…}}` JSON and emitted them as the stage `state`
via `_state_b64({pluginPath,…,pluginState})` — which the VST processor can't
parse as a state chunk, so it came up at defaults (e.g. a master_post comp with
no makeup gain → song "too quiet / comp not applied"). The preview "▶ Listen"
hid this because it re-applies params via `setParameter` after `loadPreset`
(`rbReapplyVstParamsToChain`) — but there is NO such hook after nam_tone's
`loadPreset` for an actual song.

Fix: capture the engine's opaque blob (`api.savePreset()` → the lone type-0
stage's `.state`, since the inline editor loads just that one VST) and store it
in the vst_state envelope `{"params":{…},"opaque":"<b64>"}` (`rbStampVstState`
+ `rbCaptureVstOpaqueState` in screen.js; wired into both Capture-state buttons,
both debounced auto-saves, and the picker capture). Backend `_vst_stage_state()`
(routes.py) emits `opaque` **verbatim** as the stage `state` in both
`native_preset_full` and `_build_master_stages`; legacy params-only pieces fall
back to the old wrapper (they still only apply in preview).

> **Migration:** pieces saved before this fix have no `opaque` blob, so they
> still play at defaults in real songs. The user must re-open each VST editor
> once and re-save (Capture state or just drag → auto-save) to grab the opaque
> blob. Verified `_vst_stage_state` (verbatim opaque vs legacy fallback) and
> `_build_master_stages` against a copy of the live DB.
>
> Note: the cab IR plays at `output_gain` (default **0.5 = −6 dB**) on every
> preset — a separate, deliberate global attenuation. Left as-is; if songs are
> still quiet after the comp's makeup gain applies, raise it in the IR stage.

## What is **not** done (v3+)

1. **Multi-stage chain playback — RESOLVED in v3.8** (full-chain fetch
   redirect; see that section). Below is the original investigation that
   led there, kept for context:
   - The 2-stage limit we *emit* lives in editable Python:
     `nam_tone/routes.py:get_native_preset` builds the chain from the
     2-column `presets` table (model_file + ir_file). That file is
     user-writable.
   - The **real DSP is compiled**, so a plugin can't change the actual
     capability: `nam_tone/wasm/nam-core.wasm` (WebAssembly) and
     `app.asar.unpacked/build/Release/slopsmith_audio.node` (native C++,
     reached via `window.slopsmithDesktop.audio.loadPreset`). The WASM
     worklet (`nam-processor.js`) holds a **single** NAM context
     (`this._ctx`, one `_nam_load_model`) → the browser fallback is
     definitively single-NAM.
   - **Open question:** the native desktop module takes a generic
     `chain` array and `loadPreset` returns `result.slotsLoaded`, which
     *hints* it may load multiple slots — but whether it processes
     several **type-1 (NAM)** stages in series (amp + pedal-as-NAM) is
     **unverified**. Settle this with a runtime probe (load a 2-NAM
     chain, inspect `slotsLoaded`) before investing.
   - **Auto-patching the bundle is a trap** for "easy install": the app
     is code-signed (`com.byron.slopsmith-desktop`, Team 573MF8LBVN), so
     editing bundle files breaks the signature, the change is wiped on
     every app update, and the compiled engine still can't be touched.
   - Preview (Listen) drives `loadPreset` directly from rig_builder, so
     *if* the native engine chains NAMs, multi-NAM **preview** is
     possible with zero bundle edits; real-play multi-NAM still needs
     `get_native_preset` (bundle) or upstream engine support.

2. **Auto-download of `.nam` files — DONE (v3, verified live).**
   When the user configures a tone3000 API key in Settings, the
   batch worker calls `client.list_models(tone_id)` for each top
   candidate, picks a model by user size preference (`pick_best_model`
   in `tone3000_client.py`, falling back through standard → lite →
   feather → nano), and streams the binary to `nam_models/` (for
   .nam) or through ffmpeg into `nam_irs/` (for cab IRs that lacked
   a Rocksmith IR). Filenames embed the tone3000 tone+model id
   (`tone3000_{tone_id}_m{model_id}_{rs_gear}.nam`) so subsequent
   batches across the library skip the network entirely. A
   `disk_budget_mb` setting (default 2000) caps total download size
   per batch. The "Descargar y asignar" button in the per-gear
   Sugerir modal runs the same helper for manual one-off downloads.
   See `_download_candidate` in `routes.py`.

   **Auth gotcha — DO NOT REMOVE the Bearer header from
   `download_model_file`.** Empirically tone3000's model_url
   endpoint returns 401 without the same Bearer token used for
   the JSON API; the docs imply signed CDN URLs but production
   doesn't behave that way. We verified this against a real
   account in May 2026 (search worked, download 401'd until we
   added the header). If a future client change tries to "clean
   up" by stripping the auth header on the CDN call, downloads
   silently fail end-to-end with no preset files written.

3. **Amp/pedal/rack IR extraction.** Investigated and **abandoned**.
   The `.bnk` files for amps (avg 2.5 KB), pedals (1 KB), and racks
   (1.2 KB) contain only `BKHD` + `HIRC` chunks — DSP graph metadata,
   not embedded audio. Rocksmith's amp DSP is implemented as Wwise
   DSP plugins, not convolution, so there's no IR to extract. Cabs
   are the only category where this works.

4. **Other Rocksmith PSARCs (session.psarc, etudes.psarc,
   audio.psarc).** Checked — they hold `.bnk` files but they're
   standard Wwise RIFF/.wem audio (Session Mode backing tracks,
   lesson etudes, UI sounds), not gear IRs. The custom
   16-byte-header float32 PCM format used by cab IRs only appears
   in `gears.psarc`.

5. **Expanded pseudonym overrides.** ~6 amp pseudonyms are still
   unresolved (see "Known imperfect mappings"). Trivially extendable.

6. **The "agresiva" toggle UI exists in Settings** but the batch flow
   currently only picks one top candidate; "aggressive" only relaxes
   the license/downloads filter inside `pick_top_candidate`. No
   second-pass to re-try gear left pending under conservative policy.

---

## File paths cheat sheet

| Thing | Path |
|---|---|
| App bundle | `/Applications/Slopsmith.app/` |
| Bundled Python | `/Applications/Slopsmith.app/Contents/Resources/python/runtime/bin/python3.12` |
| Host server code | `/Applications/Slopsmith.app/Contents/Resources/slopsmith/server.py` |
| Host PSARC reader | `/Applications/Slopsmith.app/Contents/Resources/slopsmith/lib/psarc.py` |
| Sister plugin | `/Applications/Slopsmith.app/Contents/Resources/slopsmith/plugins/nam_tone/` |
| User config dir | `~/Library/Application Support/slopsmith-desktop/slopsmith-config/` |
| User plugins dir | `~/Library/Application Support/slopsmith-desktop/plugins/` |
| This plugin | `~/Library/Application Support/slopsmith-desktop/plugins/rig_builder/` |
| NAM models on disk | `slopsmith-config/nam_models/*.nam` |
| NAM IRs on disk | `slopsmith-config/nam_irs/*.wav` |
| NAM database | `slopsmith-config/nam_tone.db` |
| Plugin settings | `slopsmith-config/rig_builder_settings.json` |
| tone3000 cache | `slopsmith-config/rig_builder_cache.db` |

---

## Install on a fresh Mac

1. Install Slopsmith (the .app) and run it at least once so it creates
   `~/Library/Application Support/slopsmith-desktop/`.
2. Quit Slopsmith.
3. Unzip this plugin into
   `~/Library/Application Support/slopsmith-desktop/plugins/rig_builder/`.
4. The included `rs_to_real.json` was generated from one specific
   Rocksmith install; if the new Mac has DLC the first one didn't (or
   vice-versa), regenerate via Settings → "Regenerate gear map" with
   that machine's `gears.psarc`.
5. Open Slopsmith. "Rig Builder" appears in the nav.

Optional: tone3000 API key → Settings → paste → "Guardar". Without it,
deep-link mode works fully.

---

## How to verify nothing's broken (next agent)

Run from inside the plugin dir with the bundled Python:

```bash
PY=/Applications/Slopsmith.app/Contents/Resources/python/runtime/bin/python3.12
PYTHONPATH=/Applications/Slopsmith.app/Contents/Resources/slopsmith/lib:. \
  $PY -c "import routes; print('OK')"
```

Should print `OK` with no traceback. If it doesn't, the most likely
causes are: (a) Slopsmith was updated and the bundled Python path
changed; (b) a sibling module was edited in a way that breaks import
ordering.

For a deeper integration test, mock the FastAPI app and call
`routes.setup(MockApp(), context)` — see the conversation transcript
or the smoke-test pattern in the prior session for a recipe.
