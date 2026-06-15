# Rig Builder — Install & try

A Slopsmith plugin that maps each song's tones (amp + cab + pedals + racks)
to NAM captures from [tone3000.com](https://www.tone3000.com) and bundled
VST3/AU effects, so playing a song in Slopsmith uses realistic neural amp
simulations instead of the generic engine sounds.

## Install (macOS)

1. Quit Slopsmith if it's running.
2. Unzip into the user plugins directory:

   ```bash
   cd ~/Library/Application\ Support/slopsmith-desktop/plugins/
   unzip ~/Downloads/rig_builder.zip
   ```
3. Open Slopsmith. **"Rig Builder"** appears in the side nav.

That's it — no Python, no extra dependencies. The plugin runs inside the
slopsmith backend.

## How it works out of the box

The shipped `rs_to_real.json` already maps every gear entry to real-world
make/model strings, so search/suggest works immediately. Many pedals and
racks resolve to copyright-free VST3/AU effects bundled with the plugin, so
most tones play without any download at all.

Two modes:

- **Deep-link mode (no setup):** click "Suggest" on any gear piece. A
  modal opens with a button to `tone3000.com` prefiltered by the right
  query. You download the `.nam` manually and drag it into the upload
  zone in the **Songs** tab.

- **Auto-download mode (connect tone3000):** Setup → **Connect with
  tone3000** (a browser sign-in, no key to paste). After that:
  - **Just open a song** in the **Songs** tab and it auto-downloads
    every missing piece in the background. A banner shows progress;
    when it's done the chain updates with the new files inline.
  - The "Suggest" modal also lists candidates inline with a one-click
    "Download and assign" button for manual choice.
  - **Setup → Scan library** processes the whole library at once.

  Every download is idempotent — opening the same song twice, or two
  songs that share an amp, never re-downloads. Files live in
  `slopsmith-config/nam_models/` keyed by the tone3000 ID, so the
  next song that uses the same amp finds it instantly.

## Songs

Rig Builder reads songs in Slopsmith's own `.sloppak` format. Point it at a
raw archive and it will tell you to convert it to `.sloppak` first.

## Quick verification

After install, open Slopsmith → Rig Builder → **Songs** tab → search any
song you have → click it. You should see the tone chain with gear names,
images, and a "Suggest" button per piece. If you see that, the plugin is
healthy.

## Troubleshooting

| Symptom | Fix |
|---|---|
| "Rig Builder" not in nav | Restart Slopsmith (the plugin loader runs at startup only) |
| All gear pieces show with empty "Suggest" results | Connect a tone3000 account in Setup, or use deep-link mode |
| Song click does nothing / spinner stuck | The song file may be a `cloud_loader` stub (0 bytes). The plugin tries to materialize from Drive automatically — make sure `cloud_loader` is authenticated to your Google Drive |
| A gear returns 0 candidates | The make/model query may be too generic. Edit the query in the Suggest modal (e.g. "Ampeg SVT") and click "Save override to rs_to_real.json" — the corrected mapping persists for future searches and batches |

## Deeper docs

`docs/HANDOFF.md` next to this file has full technical context for
developers/AI agents who want to extend the plugin: API surface, schema
changes to `nam_tone.db`, the tone3000 auth gotchas, and the full-chain
playback implementation.
