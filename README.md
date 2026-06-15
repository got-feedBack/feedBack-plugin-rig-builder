# Rig Builder

A [Slopsmith](https://slopsmith.app) plugin that turns each song's tone into a
**real guitar/bass rig**. Every amp, cab, pedal, and rack in a tone is rendered
by either a **bundled VST3/AU effect** or a **NAM capture / IR** from
[tone3000.com](https://www.tone3000.com), chained into a realistic
**pedal → amp → cab** signal path instead of a generic synth.

Out of the box, pedals and racks default to the copyright-free VST3/AU effects
that ship with the plugin, and amps to NAM captures — but you can assign either
a VST or a NAM to any piece.

> Companion to the built-in `nam_tone` plugin (the audio engine). Rig Builder
> finds/assigns the captures and persists the full chain; `nam_tone` plays it
> back.

---

## Features

- **Auto-map a whole library** — parse every `.sloppak` song and record its
  gear, resolving each unique amp/pedal/cab/rack to a bundled VST or a tone3000
  NAM capture.
- **VST3/AU or NAM, per piece** — assign each gear either a copyright-free
  **bundled VST3/AU effect** (in-app editors, no download) or a **NAM capture /
  IR** from tone3000. Pedals and racks default to bundled VSTs; amps to NAM —
  and you can swap any piece to the other at will.
- **Full chain in real playback** — every gear in a tone becomes its own
  NAM/IR/VST stage (`pedal → amp → … → cab`), so playing the song uses the
  complete chain, not just one amp + cab.
- **Per-tone live preview (▶ Listen)** — monitor your guitar through a tone's
  full chain before committing.
- **Per-stage Bypass** — A/B any amp/pedal/cab in or out (pass-through, not
  mute); the choice is saved per song.
- **Gear catalog** — every gear used by your library, grouped by type, with
  what it's parented to, a photo of the capture, and ▶ to audition it alone.
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

Optional: open **Setup** and connect a tone3000 account to unlock in-app
candidate listing and auto-download. Without it, deep-link mode works fully.

> No hot reload — restart Slopsmith after updating the plugin. Database
> migrations run automatically on launch.

---

## Usage

1. **Setup → Scan library** to map your whole library (or open one song in
   the **Songs** tab).
2. For each tone, assign a capture per gear — upload a `.nam`/`.wav`, pick a
   bundled VST, or **Suggest → Download and assign** (with a tone3000 account).
3. **▶ Listen** to audition the full chain live; toggle **Bypass** on any
   stage to hear what it contributes.
4. **Save preset.** Play the song in Slopsmith — it now runs through the full
   neural chain.
5. Browse everything you've mapped in the **Gear** tab (photos + isolated ▶).

---

## Requirements

- Slopsmith desktop (macOS, Windows, or Linux) with the bundled `nam_tone`
  plugin.
- A tone3000 account is optional (enables in-app candidate listing and
  auto-download).

Rig Builder reads songs in Slopsmith's own `.sloppak` format only.

---

## How it works (short version)

- The gear → real make/model map (`rs_to_real.json`) ships pre-generated with
  the plugin.
- Assignments persist into `nam_tone`'s database as a `preset_pieces` chain
  plus a primary amp+cab (what the stock engine reads).
- Real playback is upgraded to the full chain by transparently serving every
  stage to the audio engine — no changes to the Slopsmith app bundle, so it
  survives app updates. (Kill-switch: `window.__rbChainPlayback = false`.)

For the full design, internals, and contributor notes see **`docs/HANDOFF.md`**
and **`docs/CLAUDE.md`** in this repo.

---

## License

See repository license.
