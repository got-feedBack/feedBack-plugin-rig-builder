# Rig Builder — handoff doc

> **Edit: non-recreated VSTs open their NATIVE window (2026-06-12, branch
> `feat/amp-loudness-normalize`).** User: VSTs without an in-app recreation should
> open the plugin's own window on Edit instead of generic synthesized sliders —
> "solo si no se puede integrar el ui del vst al juego". New editor priority in
> `screen.js`: (1) **faithful in-app canvas** (`rbHasCanvasUI(piece)` = there's an
> `RBPedalCanvas` spec for the stem) → in-app face; (2) **native window** for any
> VST we HAVEN'T recreated → `await rbTryOpenNativeEditor(api, slotId)` (opens
> `openPluginEditor`, returns false if the plugin is UI-less / rejects) + a slim
> `rbNativeEditorPanelHtml` info row with 📸 Capture + ✕; (3) **generic sliders**
> only as the UI-less fallback (native window failed). Two new helpers
> (`rbTryOpenNativeEditor`, `rbNativeEditorPanelHtml`) wired into `rbToneEditVst`
> (~3643), `rbMasterEditVst` (~4491), and the gear catalog `rbCatalogEditInline`
> (~8835, now routes ANY non-faithful VST to `rbCatalogEditVst`'s native window,
> not just param-less ones). The native window is tracked via `_vstEditorSlot` so
> `rbCloseActiveVstEditor` closes it on navigation. Edit params in the native
> window → 📸 Capture snapshots them into the tone/master saved state. Pure JS —
> restart Slopsmith to load.

> **Leveler start-up blast fix — detector warm-up (2026-06-12, branch
> `feat/amp-loudness-normalize`).** After the load-mute fixes the user STILL heard
> "fuerte el bajo y luego se baja" on song start (confirmed with a full
> Cmd+Q + `pkill` restart). Root cause was the LEVELER, not the mute: its 30 ms
> mean-square detector (`msEnv`) starts at 0, so the first ~90 ms read
> artificially quiet → the AGC snapped to a big BOOST → the tone blasted, then
> dropped as the detector caught up. Fix in `PluginProcessor.cpp`: (1) **seed** —
> on the first signal block, jump `msEnv` straight to that block's mean square
> instead of ramping from 0; (2) **warm-up** — for the first ~45 ms of signal HOLD
> the gain and MUTE the output (`confidence` ramp), so the detector fully settles
> before the AGC's first decision, then fade in over ~20 ms at the
> already-correct level. Sim (real DSP): start-up overshoot 10.6 dB (none) → 6.2
> (seed) → **0.4 dB (warm-up)**. Net: a brief soft attack on the very first note
> per song load instead of a blast. State reset in prepareToPlay
> (`warmupSamples`/`detectorSeeded`). Rebuild+install+sign the racks bundle as
> usual; restart Slopsmith.

> **Load-mute: full silence + un-mute on the FIRST REAL TONE (2026-06-12, branch
> `feat/amp-loudness-normalize`).** User still heard "el instrumento fuerte y
> después baja" on song load. Two causes, both in `screen.js`:
> 1. **Dry-during-load played the RAW DI at full volume.** `rbPreLoadMute` left the
>    input monitor UN-muted during load ("clean guitar while it loads"), so the
>    un-leveled instrument blasted, then dropped to the processed −14 LUFS chain.
>    Flipped the default to **full silence** (monitor muted + chain 0); opt back in
>    with `window.__rbDryDuringLoad = true`.
> 2. **The un-mute fired while the active tone was NONE.** `buildForSong` resolved
>    the tone from the highway, which often isn't ready yet (`initial tone → NONE`),
>    un-muted on the bypassed/silent state, and the REAL tone (`stpinte_bass`…) was
>    applied LATER by the recheck / default-tone path UN-MUTED → the amp came in
>    loud before the leveler caught up. Fix: (a) for a **single-tone song** apply
>    that one tone immediately (no highway wait); (b) only `rbSignalChainLoaded()`
>    (un-mute) once a REAL tone is active — if NONE, stay muted and let the recheck
>    (`~screen.js:1592`), early no-schedule detector (t+1500, `~1652`), late
>    base/first-change and 10 s fallback paths each call `rbSignalChainLoaded()`
>    when they apply the first real tone. The 15 s coalesce safety net still
>    backstops a stuck load. Net: silence during load → processed chain fades in
>    with the right tone already active, leveler converged. (Cross-song level
>    differences the user also noted: the leveler only levels the GUITAR chain to
>    −14 LUFS — the PSARC BACKING-TRACK volume is the bundle's, not ours.)

> **Leveler limiter ORDER + load-mute event fix (2026-06-12, branch
> `feat/amp-loudness-normalize`).** Two follow-ups:
> 1. **Limiter must sit BEFORE the makeup, not after.** In the leveler the
>    brickwall limiter was operating on the gain that already included the
>    Output-Trim makeup (Chain volume ×5 = +14 dB), so it clawed the makeup back
>    down to the ceiling → "se escucha todo muy despacio". Fixed in
>    `PluginProcessor.cpp`: `currentGain`/`nextAgcGain` now track the AGC gain
>    WITHOUT the makeup; the limiter caps the NORMALIZED peaks; `makeupGain` is
>    applied last as a clean output gain. Sim: two distorted tones 17 dB apart →
>    both land ~0 LUFS (loud, makeup preserved) and within 0.1 dB (even); the old
>    'after' order landed ~−2..−3 LUFS (quiet). TRADE-OFF (by user request): the
>    makeup is now unconstrained, so a high Chain volume CAN clip — tell the user
>    to keep Chain volume ~×2–3 (the leveler already evens loudness; Chain volume
>    is just the final clean level).
> 2. **Song-load mute stayed timer-based.** The fetch-interceptor mutes first at
>    `song:loaded` with a SHORT fallback timer (no `deferUnmute`); when the
>    mega-chain preload then calls `rbPreLoadMute(...,{deferUnmute:true})` it hit
>    `if (_rbMuteInFlight) return` and LOST the defer → the short timer un-muted
>    mid-load and the VST/NAM load peaks leaked ("se escucha cómo cargan los
>    VST"). Fixed in `screen.js`: when a `deferUnmute` call coalesces onto an
>    in-flight mute, UPGRADE the pending unmute timer to the long (15 s) safety
>    net so the real un-mute comes from `rbSignalChainLoaded()` (event-driven, the
>    moment the load truly finishes), never the short timer.

> **RB Final Leveler → un-starve the boost + brickwall limiter (2026-06-12,
> branch `feat/amp-loudness-normalize`).** Even with the LUFS detector a song's
> tones still sounded uneven ("muy distintos"), specifically: loud tones clean
> but loud, quiet tones never brought up. ROOT CAUSE: the AGC clamped its boost
> by the instantaneous peak (`maxAllowedByPeak = ceiling − peakDb`) — so a tone
> couldn't be boosted toward the loudness target without its peaks crossing the
> ceiling. That **starved the boost on quieter tones** (they stayed quiet) while
> compressed loud tones got cut fine. The user's global "Chain volume" makeup
> (×5 = +14 dB, baked into the leveler Output Trim) then amplified the gap. FIX
> in `PluginProcessor.cpp`: (1) drive the AGC PURELY by loudness (LUFS) to the
> target — no peak clamp on the boost; (2) catch transients with a **brickwall
> peak limiter** on the output (instant attack so it never overshoots the
> ceiling, ~80 ms release). Net: quiet tones get boosted to the target loudness
> (the comp finally "raises the low volumes") and peaks stay safe. Verified
> offline (sim of the real DSP): two distorted tones 17 dB apart in input level
> both land within ~1 dB at the output with peaks ≤ ceiling — the OLD path only
> matched them by CLIPPING (peaks at 1.00). CAVEAT (real DSP limit, not a bug):
> a VERY high-crest CLEAN/dynamic tone still can't be made as LOUD as a
> compressed one without compressing it (peak ≈ loudness + crest must fit under
> the ceiling); the user's tones are all distorted (low, similar crest) so they
> level evenly. With the leveler now working, the +14 dB makeup pushes ~6 dB of
> limiting — the user may want to lower "Chain volume" for a cleaner, less
> squashed result. Build/install/sign + restart steps same as the LUFS note
> below. Diagnostic tools added: `tools/measure_tone_loudness.py` +
> `tools/dpf_pipe_harness.cpp.in` + `tools/cab_lufs.cpp` measure a tone's actual
> full-chain (pedal→amp→rack→cab) LUFS offline (each VST piece is a separate
> stdin→stdout process; skips reverb/chorus tones that allocate in activate()).
> This is how the bass tones were found at +3..+7 LUFS pre-leveler (clipping-hot).

> **RB Final Leveler → perceptual LUFS detector (2026-06-12, branch
> `feat/amp-loudness-normalize`).** Symptom: a song's tones still sounded at
> different volumes even with per-amp leveling — because tone loudness is
> dominated by the WHOLE chain (fuzz/distortion/octave pedals, bass vs guitar
> cab, racks), not the amp, and the end-of-chain RB Final Leveler equalized
> **RMS**, which doesn't match perceived loudness across spectrally/dynamically
> different tones (a bass at −14 RMS sounds ~5 dB quieter than a guitar at −14
> RMS). Fix: `vst/src/RBFinalLeveler/Source/PluginProcessor.cpp` now runs the
> detector signal through an **ITU-R BS.1770 K-weighting** filter (2 biquads/ch,
> coefficients computed per sample-rate via the libebur128 derivation — verified
> byte-exact vs the standard at 48 k) before the 30 ms running mean-square, and
> reads loudness as `−0.691 + 10·log10(meanSquare)` LUFS. The AGC (target/boost/
> cut/ceiling/adaptive convergence) is unchanged; the peak/ceiling anti-clip
> still uses the RAW peak. Param NAMES unchanged ("Target RMS dB" is now a target
> LUFS — kept for the name-keyed state restore) and the −14 default still
> applies. Now the leveler AND the per-amp trim use the SAME metric (−14 LUFS).
> Diagnosis verified on `musetime_p.psarc` (Time is Running Out): all 6 tones use
> in-model VST amps but their amp trims were tiny (−0.4..+4.2 dB) — the spread is
> from pedals/cabs. **Build:** `cmake --build vst/src/RBFinalLeveler/build --config
> Release`, then copy the artefact binary into
> `vst/racks/RB Final Leveler.vst3/Contents/MacOS/RB Final Leveler` (macOS slot
> only — keep linux/win) + `codesign --force --sign - "vst/racks/RB Final Leveler.vst3"`.
> Restart Slopsmith (Cmd+Q + `pkill -f 'uvicorn server:app'` + reopen — orphan
> backend serves stale code). DON'T commit the `build/` artifacts (persistent
> noise); commit only the Source .cpp + the shipped bundle binary + CodeResources.

> **Per-amp loudness normalization — in rig builder, NOT in the VSTs (2026-06-11,
> branch `feat/amp-loudness-normalize`).** Every bundled amp VST3 bakes its own
> output coeff (`kLvl`), so amps drifted across a ~23 dB loudness range (measured
> `jc90` ≈ −1.8 LUFS … `ua610` ≈ −25.3 LUFS) and cranking Gain raised level. Fixed
> at the chain-build layer so no VST is recompiled and saved `vst_state` stays
> valid. Two pieces:
> 1. **Measurement** — `tools/measure_amp_loudness.py` + `tools/amp_lufs_harness.cpp.in`
>    compile a per-amp DPF offline probe (includes the amp's `*Plugin.cpp`, a
>    `Probe` subclass reaches `run()/setParameterValue()/sampleRateChanged()`),
>    feed a 110–1760 Hz multitone @ RMS 0.10 and measure **BS.1770-4 integrated
>    LUFS** (K-weighting + gating, mono/left) while sweeping each loudness param
>    (the param RS "Gain" maps to — from `rs_knob_to_vst_param.json` — plus any
>    Volume/Master pot). Output: `data/amp_loudness_model.json` (60 amps). Re-run
>    after adding/retuning an amp. Needs `/usr/bin/clang++`; ~9 min for all 60.
> 2. **Compensation** — `routes.py` (`_amp_loudness_trim_db` / `_amp_trim_mult_for_state`
>    / `_amp_trim_stage`). From the amp's resolved params it interpolates the
>    measured LUFS and computes a CLEAN trim: target **−14 LUFS**, with the Gain
>    contribution soft-capped (tanh) at **+2 dB (→ −12)** and Volume/Master at
>    **+3 dB (→ −11)**; clamp [−24, +9] dB. The trim rides a **1-sample
>    (identity) unit-impulse IR stage** inserted right after the amp (a clean gain
>    the engine already applies to IR stages) — **the amp's Gain param is never
>    touched, so it still saturates exactly as before**. Wired into all 3 chain
>    builders (`native_preset_full`, mega_chain's `_build_tone_stages`,
>    `native_preset_one` audition). The trim stage carries `rs_gear="__rb_amp_trim"`
>    + a top-level `amp_trim` marker so mega_chain's IR dedupe keeps per-amp trims
>    distinct (and `screen.js` `rbChainGainTargetFor` skips it so it isn't counted
>    as a cab). The **RB Final Leveler stays** as a safety net — the per-amp trim
>    just hands it an already-even level. Constants `_AMP_TARGET_LUFS / _AMP_CAP_*`
>    in routes.py are tunable by ear without re-measuring. Limitation: LUFS is
>    measured at the multitone point (consistent with `kLvl`); absolute playback
>    LUFS with a real guitar differs, but the relative leveling + caps hold, and a
>    couple of very quiet DI preamps (ua610/meve1073) only reach ~−16 LUFS (the +9
>    dB boost clamp, to limit hiss). See AMP_LOUDNESS.md for the loudness standard.

> **Loudness fixes (2026-06-09).** (1) Bundled COMPRESSORS had open-loop makeup
> that ballooned with the comp knob (BassMultiComp +16 dB, DynamicsCompression
> +21 dB, StudioComp ~+18 dB at extremes) → volume jumped when you turned up
> Compress. All three now use **closed-loop makeup**: match the post-compression
> smoothed RMS (~250 ms) to the input's so output loudness stays constant
> (range-limited −12..+18 dB; StudioComp keeps the Output knob as a trim).
> Verified flat offline (DPF harness, steady tone). (2) **RBFinalLeveler** (the
> end-of-chain limit/comp, JUCE) reworked for song/tone consistency: detector is
> now a fixed ~30 ms running mean-square (block-size independent, was averaging
> one ~3-10 ms block), and gain uses **adaptive convergence** — snaps fast
> (~20 ms) on a big sustained level gap (a drive→clean tone switch, which used to
> fade in over ~600 ms) but stays slow on small drift so dynamics aren't
> flattened (no pumping). Offline algo check: all levels converge to −14 dBFS
> RMS; drive→clean re-levels in ~400 ms. Build the leveler with `cmake --build
> vst/src/RBFinalLeveler/build --config Release`, copy the artefact binary into
> `vst/racks/RB Final Leveler.vst3` + ad-hoc re-sign. **GIT GOTCHA:** the working
> tree carries persistent pre-existing noise (deleted pedal .vst3 bundles, RBFinalLeveler/build
> artifacts, an app-regenerated data/rs_to_real.json). `git add` ONLY the exact
> files you changed and check `git diff --cached --stat` before committing — never
> `git add -A`/`commit -a`, and watch `pull --rebase --autostash` (it can stage
> that noise into your commit, as happened in c93b7d8 → fixed in a0b828a).

> **RS-knob → VST param mapping fixed for pedals/racks (2026-06-09).** Symptom:
> only AMPs responded to a song's knobs; pedals/racks played at plugin defaults.
> Root cause: the bundled pedal/rack VSTs were **renamed** for the no-copyright
> UI (`EQ8`→`GE-8`, `StudioComp`→`HZX`, `StudioEQ`→`LNG`, `MultiPitch`→`Multi
> Pitch`, etc.) but `data/rs_knob_to_vst_param.json` still keyed each block under
> the OLD design stem, so `_build_params_for_piece` (keyed on the assigned file
> stem) returned `None` → empty params. Amps weren't renamed, hence they alone
> worked. Fix = **added lowercased real-filename-stem aliases** for all 76
> renamed bundled VSTs (each duplicates its own design block — additive, nothing
> removed) + **authored real blocks** for the 2 gears that only had a kHs
> fallback (`Bass_Pedal_BassWah` = Auto/Pedal/Sens/Speed; `Bass_Pedal_BassSubOctave`
> = Mix/Tone). Audit: every gear with a `bundled` VST now resolves its stem (0
> unresolved). Also fixed `_effective_vst_state_for_piece` (`routes.py`) so a
> **stale `{"params": {}}`** stored on existing presets no longer short-circuits
> — empty params now RE-COMPUTE from the RS knobs at playback, so updaters self-
> heal without a re-batch (new installs were already correct once the table was
> fixed). To re-key in future, regenerate aliases by deriving the stem from each
> gear's `rs_gear_to_vst.json` `bundled` path.
>
> **Second table — `_VST_PARAM_RANGES` (same root cause, 2026-06-09).** Symptom:
> Studio EQ (file `LNG.vst3`) had ALL freqs/Q pinned at max. `apply_vst_state.py`
> has a SECOND stem-keyed table, `_VST_PARAM_RANGES`, that normalizes display-
> domain params (Hz/Q/dB) → [0,1]. It's keyed by the design stem (`studioeq`,
> `studiocomp`, `studiographiceq`), so the renamed file stems (`lng`, `hzx`,
> `g-550`) missed it → a Hz value (1600) skipped normalization and clamped to
> 1.0. Fix = `_STEM_RANGE_ALIASES` ({lng→studioeq, hzx→studiocomp,
> g-550→studiographiceq}) resolved via `_range_stem()` at both lookup sites
> (`_translate_one_knob` + the `_graphic_eq` fold). Only bundled stems with a
> range entry need listing. NOTE the bundled Studio EQ Bass band is only 30–300
> Hz (`seqFBass` in `StudioEqParams.h`); RS BassFreq up to ~455 legitimately
> clamps — that's a VST-range fidelity gap, not a mapping bug (widen both the DSP
> helper and the range to fix).

> **In-chain VST editing (2026-06-02, `feat/pedals-vst`).** Pressing **Edit** on
> a pedal in a song tone's chain now edits it **inside the live full-chain
> preview** instead of loading an isolated single copy. `rbToneEditVst`
> (`screen.js`) starts (or reuses) the tone's `rbListenTone` preview, maps the
> piece → its engine slot in the loaded chain via `rbChainSlotIdForPiece`
> (matches `native_preset.chain` stage `path`/`rs_gear`, then `getChainState()`
> index → slot id), and routes the canvas/sliders' `setParameter` at that slot —
> so you hear the WHOLE chain (amp+cab+pedals) and the knob adjusts the chain's
> sound, not a louder solo pedal. No 2nd copy is stacked (the old "Edit doubles
> the sound" guard was an unconditional `clearChain` in `rbTeardownVstEditor`;
> it's now gated by `rbState._vstEditorInChain` so closing the pedal face leaves
> the preview playing). Falls back to the legacy isolated `loadVST` editor when
> no live chain is available / the piece isn't found. Master-chain editing
> (`rbMasterEditVst`) still uses the isolated path — separate follow-up.

> **Released: v2.0.0 (2026-06-01)** — 100 copyright-free bundled VST3 effects
> (pedals + racks) under `vst/`, plus in-app HTML-canvas pedal UIs
> (`pedal_canvas.js` / `window.RBPedalCanvas`). Pressing **Edit** renders the
> pedal face inline (draggable controls → `setParameter`) instead of opening a
> native plugin window; gear photos show the rendered face for bundled-VST
> gears. Fonts served at `…/asset/font/{name}`, the JS at `…/asset/pedal_canvas.js`.
> Engine prepends "Buffer Size"/"Sample Rate" to every param list →
> `rbFilterVstParams` drops them and `rbBuildCanvasModel` maps LOGICAL→REAL
> param ids (fixes off-by-two controls). Edit auto-applies the RS knob mapping
> when a tone has no captured state. Amps stay on their chosen NAM (not in
> `rs_gear_to_vst.json`); amp VSTs are on the separate `feat/amps-vst` branch.

> **Released: v1.2.0 (2026-05-27)** — amp gain variants, library Manage tab,
> chain preloader (instant tone switching, now default), and loudness/audio
> fixes. Closed issues #12 (tone-change spike), #13 (master chain not applying),
> #14 (mac IR extraction), #15 (quiet NAMs). See `WHATS_NEW.md` for the
> user-facing summary.

A Slopsmith plugin that maps **Rocksmith 2014 tones** (amp + cab + pedals + racks)
to **NAM captures and IRs from tone3000.com**, persisting per-song mappings in
`nam_tone.db` so the existing NAM runtime plays them back automatically.

This document is for the next person/agent to pick up: it explains the
context that isn't obvious from the code alone — host extension model,
database schema, API quirks, what's done, what isn't, and why.

---

## Amp gain variants (`feat/amp-gain-variants` — experimental)

Rocksmith amps respond to their Gain knob (a Twin at gain=10 is sparkling
clean; at gain=80 it's snarling). Tone3000 captures are **fixed-setting
snapshots** — one NAM is one (amp + knob position). So if we ship a single
"Twin clean" capture for `Amp_Twin`, every Twin tone in every song uses that
clean character even when the song asks for gain=80.

The new `gain_variants` field in `rs_to_real.json` lets a curator ship up to
N captures per amp, tagged by which RS-gain range each one models:

```json
"Amp_Twin": {
  "name": "Fender Twin Reverb",
  "category": "amp",
  "tone3000_query": "fender twin reverb",
  "gain_variants": {
    "clean":  { "tone3000_id": 12345, "rs_gain_range": [0, 35] },
    "crunch": { "tone3000_id": 12346, "rs_gain_range": [35, 70] },
    "dist":   { "tone3000_id": 12347, "rs_gain_range": [70, 100] }
  }
}
```

`default_captures.json` accepts the same shape, so a curator can ship the
variants without curating `rs_to_real.json` if they prefer.

### Picking the variant

`_pick_amp_gain_variant(gear_def, rs_gain)` (in `routes.py`):

- **Pass 1**: returns the variant whose `rs_gain_range` covers the song's
  Gain knob value.
- **Pass 2 (fallback)**: closest range centre by distance to `rs_gain` —
  lets a curator ship just 2 variants with a gap between them and still
  get sensible picks for in-between gain values.
- Returns `None` if `gain_variants` isn't defined → legacy single-NAM
  behaviour. The feature is fully additive.

The Gain knob is read via `_gear_rs_gain(piece)` from the parsed
`piece["knobs"]["Gain"]` value. Defaults to 50.0 (centre) when absent.

### Where it's wired in

- `_auto_download_for_song` (per-song flow): cache key is
  `(rs_type, variant_id)` so the same amp at two different gain settings
  triggers two downloads. The `existing` DB lookup is `tone3000_id`-aware
  so tone B's "dist" doesn't accidentally reuse tone A's "clean" file.
- `_batch_worker` (library-wide flow): same cache key change.
- `_existing_assignment_for_gear(rs_type, tone3000_id=...)`: the
  library-wide reuse path also filters by `tone3000_id` when a variant
  is in play.
- `_enrich_chain_piece`: returns an `amp_variant` block when the gear has
  variants, listing the picked level + the available levels. The per-song
  UI renders that as an emerald badge under the amp piece.

### Override

There is no dedicated "switch variant" UI yet — the existing flow already
handles it: the user picks a different NAM via **📚 Library** or the file
input, the new file gets `assigned_mode='manual'`, and the batch leaves
the manual choice alone forever after.

### Curating new variants

To add variants for an amp:

1. Find 2-3 captures on tone3000 covering different gain settings of the
   target amp. Note their `tone3000_id`.
2. Edit `rs_to_real.json` (or ship via `default_captures.json` instead —
   either works): add a `gain_variants` block as shown above.
3. Pick `rs_gain_range` values. Common 3-tier split:
   - `clean`: `[0, 35]`
   - `crunch`: `[35, 70]`
   - `dist`: `[70, 100]`
4. Run Dashboard → Batch all → mode "all" so the existing songs pick the
   right variant (legacy single-NAM downloads stay until they're
   overwritten by the variant ones).

Amps that benefit most (high non-linearity / wide gain range): Marshall
JCM800, Mesa Boogie Mark series, Engl Powerball, Soldano SLO, Bogner
Ecstasy, Friedman BE100. Cleaner amps (Twin, Princeton, Vox AC15) can
get by with just `clean` + `crunch`.

---

## v1.1.0 (2026-05-26) — OAuth login + smart batch + file picker

Three changes since the 1.0.0 release. All plugin-only.

### tone3000 auth is now OAuth (no pasted secret)

The user/testers were (rightly) wary of pasting a tone3000 **secret key**
(`t3k_cs_…`) — tone3000's own docs say to treat it like a password and never
put it on a user's device. tone3000 supports OAuth 2.0 + PKCE (the
**publishable key** `t3k_pub_…` is safe to embed; it only identifies the app,
the per-user access token does the work). So Settings now has **Connect with
tone3000** instead of a key box.

Flow (RFC 8252 loopback, fits this Electron app perfectly):

1. `GET /oauth/start?origin=<window.location.origin>` → backend generates PKCE
   `code_verifier` + `state` (kept in-memory in `_oauth_pending`, 10-min TTL),
   builds the authorize URL, returns it.
2. UI opens it with `window.open` → the host's nav-guard re-routes external
   URLs to the **system browser** (same mechanism the deep-links already use).
3. User signs in on tone3000 → redirected to
   `http://127.0.0.1:<port>/api/plugins/rig_builder/oauth/callback` (the local
   uvicorn server — the system browser can reach loopback).
4. `/oauth/callback` validates `state`, exchanges `code`+verifier for tokens,
   persists them, fetches the username, shows a "close this tab" page.
5. UI polls `/oauth/status` until connected.

**Security specifics (this was made mandatory — plugin ships to many users):**
- `redirect_uri` is **hard-restricted to loopback** server-side
  (`_safe_loopback_redirect`): a tampered `origin` can never send the auth code
  off-machine. tone3000 also only honors *registered* redirect URIs.
- **PKCE (S256)** → an intercepted code is useless without the verifier.
- `state` validated → CSRF.
- Settings file written **`0600`** (`_write_settings_file`) so other OS users
  can't read tokens/secret.
- Tokens auto-refresh (`Tone3000Client._refresh_token`); `_persist_tokens`
  saves rotated tokens **without** nulling the live client.

Client (`tone3000_client.py`): `Tone3000Client` now takes `access_token` /
`refresh_token` / `token_expires_at` / `publishable_key` / `on_tokens`. Bearer
= access token if present, else the secret. `generate_pkce`,
`build_authorize_url`, `exchange_code`, `_refresh_token`, `get_user` added.
`DEFAULT_PUBLISHABLE_KEY` is a placeholder = the dev's personal publishable
key — **register a dedicated "Rig Builder" OAuth app + its loopback
redirect_uri with tone3000 before wide release** (see "Open" below).

The **secret-key UI was removed** from Settings (the backend still accepts
`tone3000_api_key` as a fallback, just not shown). New settings keys:
`tone3000_access_token`, `tone3000_refresh_token`, `tone3000_token_expires_at`,
`tone3000_username`.

### Batch: "manual is sacred, auto inherits"

Testers expected "map all songs with the gear I selected" but a manual choice
in one song didn't propagate, and `Remap all` could clobber manual picks. New
per-gear resolution order in `_batch_worker`:

0. **Manual is sacred** — if the *current* tone has a hand-assigned
   (`assigned_mode in manual/manual_vst`) piece for this gear that's still on
   disk, keep it **verbatim** (incl. VST path/format/state). Never overwritten.
1. Rocksmith cab IR on disk (unchanged).
2. **Reuse** a capture already assigned to this gear in **any** song
   (`_existing_assignment_for_gear`, manual preferred) → so a manual pick
   spreads to the auto/untouched songs.
3. `default_captures.json`.
4. tone3000 search.

`_manual_piece_usable` gates step 0; `existing_by_gear` now loads the full
piece (assigned_mode, tone3000_id, params, vst fields). `Map new` still skips
already-mapped tones entirely; `Remap all` refreshes auto pieces but preserves
all manual ones.

### Native file picker

`Browse…` buttons next to the two `gears.psarc` inputs (Regenerate gear map,
Extract Rocksmith IRs) call `window.slopsmithDesktop.pickFile([...])` (the
host's `dialog:pickFile` IPC — same one `audio_engine` uses) →
`rbBrowseForPsarc`. Degrades to manual entry if no desktop bridge.

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
