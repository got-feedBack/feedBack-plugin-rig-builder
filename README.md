# Rig Builder

A [Slopsmith](https://slopsmith.app) plugin that turns **Rocksmith 2014
tones** into **real neural-amp rigs**. It maps each song's amp + cab +
pedals + racks to [tone3000.com](https://www.tone3000.com) NAM captures and
IRs, then chains them so the song plays through a realistic
**pedal → amp → cab** signal path instead of a generic synth.

> Companion to the built-in `nam_tone` plugin (the audio engine). NAM Rig
> Builder finds/assigns the captures and persists the full chain;
> `nam_tone` plays it back.

---

## Features

- **Auto-map a whole library** — parse every PSARC/sloppak and record the
  Rocksmith gear, suggesting a tone3000 capture per unique amp/pedal/cab/rack.
- **Full neural chain in real playback** — every gear in a tone becomes its
  own NAM/IR stage (`pedal → amp → … → cab`), so playing the song uses the
  complete chain, not just one amp + cab.
- **Per-tone live preview (▶ Listen)** — monitor your guitar through a tone's
  full chain before committing.
- **Per-stage Bypass** — A/B any amp/pedal/cab in or out (pass-through, not
  mute); the choice is saved per song.
- **Gear catalog** — every gear used by your library, grouped by type, with
  what it's parented to, a photo of the capture, and ▶ to audition it alone.
- **Rocksmith cab IRs** — extracts the game's own 444 cab impulse responses
  and prefers them for cabinets.
- **Deep-link or API mode** — works without a tone3000 key (opens prefiltered
  searches in your browser); with a key, it lists candidates in-app and can
  auto-download captures.

---

## Install

1. Install Slopsmith and run it once (it ships the `nam_tone` engine and
   creates the app-data folder). Quit Slopsmith.
2. Drop this folder into the plugins directory so it lives at:
   - **macOS:** `~/Library/Application Support/slopsmith-desktop/plugins/rig_builder/`
   - **Windows:** `%APPDATA%\slopsmith-desktop\plugins\rig_builder\`
   - **Linux:** `~/.config/slopsmith-desktop/plugins/rig_builder/`
3. Restart Slopsmith. **Rig Builder** appears in the nav.

Optional: open **Settings** and paste a tone3000 API key (`t3k_…`) to unlock
in-app candidate listing and auto-download. Without it, deep-link mode works
fully.

> No hot reload — restart Slopsmith after updating the plugin. Database
> migrations run automatically on launch.

---

## Usage

1. **Dashboard → Start batch** to map your whole library (or open one song in
   **By song**).
2. For each tone, assign a capture per gear — upload a `.nam`/`.wav`, pick a
   Rocksmith cab IR, or **Suggest** → **Download and assign** (with a key).
3. **▶ Listen** to audition the full chain live; toggle **Bypass** on any
   stage to hear what it contributes.
4. **Save preset.** Play the song in Slopsmith — it now runs through the full
   neural chain.
5. Browse everything you've mapped in the **Gear** tab (photos + isolated ▶).

---

## Requirements

- Slopsmith desktop (macOS or Windows) with the bundled `nam_tone` plugin.
- A copy of Rocksmith 2014's `gears.psarc` is only needed to (re)generate the
  gear map or extract cab IRs on a new machine — the repo ships a
  pre-generated `rs_to_real.json`.
- A tone3000 account + API key is optional (enables auto-download).

---

## How it works (short version)

- The gear → real make/model map (`rs_to_real.json`) is generated from the
  game's own `gears.psarc` by `extract_gear_map.py`.
- Assignments persist into `nam_tone`'s database as a `preset_pieces` chain
  plus a primary amp+cab (what the stock engine reads).
- Real playback is upgraded to the full chain by transparently serving every
  stage to the audio engine — no changes to the Slopsmith app bundle, so it
  survives app updates. (Kill-switch: `window.__rbChainPlayback = false`.)

For the full design, internals, and contributor notes see **`HANDOFF.md`**
and **`CLAUDE.md`** in this repo.

---

## License

See repository license.
