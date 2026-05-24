# NAM Rig Builder — Install & try

A Slopsmith plugin that maps Rocksmith tones (amp + cab + pedals + racks)
to NAM captures from [tone3000.com](https://www.tone3000.com), so playing
a CDLC song in Slopsmith uses realistic neural amp simulations instead of
the generic engine sounds.

## Install (macOS)

1. Quit Slopsmith if it's running.
2. Unzip into the user plugins directory:

   ```bash
   cd ~/Library/Application\ Support/slopsmith-desktop/plugins/
   unzip ~/Downloads/nam_rig_builder.zip
   ```
3. Open Slopsmith. **"NAM Rig Builder"** appears in the side nav.

That's it — no Python, no extra dependencies. The plugin runs inside the
slopsmith backend.

## How it works out of the box

The shipped `rs_to_real.json` already contains 613 Rocksmith gear entries
mapped to real-world make/model strings (auto-extracted from a base game
`gears.psarc`). So search/suggest works immediately for any CDLC.

Two modes:

- **Deep-link mode (no setup):** click "Suggest" on any gear piece. A
  modal opens with a button to `tone3000.com` prefiltered by the right
  query. You download the `.nam` manually and drag it into the upload
  zone in the "By song" tab.

- **Auto-download mode (requires API key):** Settings → tone3000 API key
  → paste a `t3k_cs_…` key. After that:
  - **Just open a song** in the "By song" tab and it auto-downloads
    every missing piece in the background. A blue banner shows progress;
    when it's done the chain updates with the new files inline.
  - The "Suggest" modal also lists candidates inline with a one-click
    "Download and assign" button for manual choice.
  - Dashboard → "Start batch" processes the whole library at once.

  Every download is idempotent — opening the same song twice, or two
  songs that share an amp, never re-downloads. Files live in
  `slopsmith-config/nam_models/` keyed by the tone3000 ID, so the
  next song that uses the same amp finds it instantly.

## Get a tone3000 API key (optional)

1. Sign up free at [tone3000.com](https://www.tone3000.com).
2. Settings → API Keys → Create API Key.
3. Pick "Secret Key" — starts with `t3k_cs_…`.
4. Paste into the plugin's Settings tab → Save.

**Note:** Keys are personal. Don't share yours — each tester should get
their own. The plugin stores the key only in
`~/Library/Application Support/slopsmith-desktop/slopsmith-config/nam_rig_builder_settings.json`
on the local machine.

## Optional: extract Rocksmith's own cab IRs

If you own Rocksmith 2014, you can extract the game's own cabinet IRs
(444 of them) for free, no tone3000 round-trip:

1. Settings → "Extract Rocksmith IRs".
2. Point at your `gears.psarc` (typically inside the Steam install).
3. Click "Extract". Takes ~30 seconds.

After extraction, the auto-mode batch prefers these for cab pieces over
tone3000 IR searches.

## Quick verification

After install, open Slopsmith → NAM Rig Builder → "By song" tab → search any
song you have → click it. You should see the tone chain with gear names,
images, and a "Suggest" button per piece. If you see that, the plugin is
healthy.

## Troubleshooting

| Symptom | Fix |
|---|---|
| "NAM Rig Builder" not in nav | Restart Slopsmith (the plugin loader runs at startup only) |
| All gear pieces show with empty "Suggest" results | Make sure your tone3000 API key is saved in Settings, or use deep-link mode |
| Song click does nothing / spinner stuck | The PSARC may be a `cloud_loader` stub (0 bytes). The plugin tries to materialize from Drive automatically — make sure `cloud_loader` is authenticated to your Google Drive |
| `Bass_Amp_CS75B` returns 0 candidates | This is a Rocksmith pseudonym that lacks a real-brand override. Edit the query in the Suggest modal (e.g. "Ampeg SVT") and click "Save override to rs_to_real.json" — the corrected mapping persists for future searches and batches |

## Deeper docs

`HANDOFF.md` next to this file has full technical context for
developers/AI agents who want to extend the plugin: API surface, schema
changes to `nam_tone.db`, the Wwise `.bnk` reverse-engineering for cab
IR extraction, the tone3000 auth gotchas, and the v3 auto-download
implementation.
