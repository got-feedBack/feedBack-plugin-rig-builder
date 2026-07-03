# Robustness & DRY audit — July 2026

Branch: `chore/audit-robustness-dry` (off `feat/amps-rework`).
Method: 5 parallel deep reviews (shared DSP headers, routes.py, screen.js,
pedal_canvas.js, cross-plugin copy-paste scan), findings verified before fixing.
This file is the running log so nobody has to re-read the code to resume.

## Inventory (phase 0) — CLEAN
- pedals: 84 sources → 163 binaries (extras = known stem aliases/renames)
- amps: 60 sources ↔ 60 binaries, 1:1 (the "65" count included .md files + tools/)
- racks: 18 sources → 21 binaries (reverbs share one engine → one source, 3 binaries)
- `vst/src/pedals/_shared` and `racks/_shared` are symlinks to `vst/src/_shared` (untracked, build-only)

## C++ shared-header fixes APPLIED (need VST rebuilds to take effect)

| File | Fix | Why |
|---|---|---|
| `_shared/semiconductors.hpp` | diode exponent clamp ±20 → `kDiodeExpMax` (±60) at all 5 solver sites | With ±20, low-Is specs never conduct: red/green LED (knee needs e≈35/62) and BC547-family junctions (e≈26) degenerated to a digital hard clip at `maxAbsV`. Zeners/silicon/germanium were fine (knee < e=16) and are unaffected. 60 keeps `exp/sinh` finite in float. Affected plugins: `pedals/marshall_guvnor_plus`, `pedals/acoustic_simulator` |
| `_shared/pedalkit.hpp` | `parameterChanged` guard adds `i < kMaxCtl` | latent OOB write into `fValues[16]` if a future pedal declares >16 params (max today is 6) |
| `_shared/oversampler.hpp` | comments only: header/usage said "4x / core set to 4*sr" but `OS = 2` | doc trap — a new plugin following the comment would run core coefficients an octave off. No binary change |
| `_shared/reverb_core.hpp` | `kCombMax` 4000→8500, `kApMax` 1500→3100; denormal flush (`rvDn`) in `RvComb.store` and `RvAllpass` buffer write | 4000 clamps all 8 combs to the same length ≥176.4 kHz (metallic flutter instead of hall); comb/allpass feedback denormals burn CPU after input stops on hosts without FTZ. Affected: `studio_verb`, `studio_chamber`, `studio_plate` |
| `_shared/reverb_plugin.hpp` | added `activate() override { rv.clear(); }` | host deactivate/reactivate replayed up to seconds of stale tail |
| `_shared/sharke_core.h` | `setSampleRate` re-applies remembered `pwrHeadroom/pwrSag` instead of hardcoded (2.0, 0.04); `setPowerHeadroom` stores them | after a sampleRateChanged, HB5000's 1.7/0.05 headroom silently reverted to defaults (EQ/tone recovered via recalc, headroom did not). Affected: `sharke_hb3500` (no audible change — its values ARE the defaults), `sharke_hb5000` |

Build verification (all clean): `studio_verb`, `marshall_guvnor_plus`, `sharke_hb5000` (arm64 mac).

### Build recipe (rediscovered gotchas)
- Path spaces break DPF link step → build through a space-free symlink:
  `ln -s ".../vst/src" $SCRATCH/vstsrc` and `cd $SCRATCH/vstsrc/<cat>/<plugin>`
- Overrides needed: `make DPF_PATH=$SCRATCH/vstsrc/racks/DPF BUILD_DIR=... TARGET_DIR=... DGL_BUILD_DIR=$BUILD_DIR -j4`
  (`DGL_BUILD_DIR` must equal `BUILD_DIR` or the link step looks for `DPF/build-<Name>ns/libdgl-opengl.a`)
- pedals/ and amps/ have no local `DPF` symlink — always pass `DPF_PATH` explicitly.

### C++ findings DEFERRED (real, but need care / user decision)
1. **Amp-core recalc thump** (guitar_amp_core/citrus/dsl cores + `tube_stage.hpp setWithPlate`): every knob event re-runs `setWithPlate` on all stages, zeroing cathode bias `vk` mid-signal → audible blip + CPU spike under automation. Fix = split recalc (SR-dependent vs knob-dependent) or preserve `vk`. Touches the calibrated amps → wants THD/loudness re-validation after.
2. **Mix/level zipper**: `reverb_core` dryMix/wetMix and Flanger/amp-core `outLevel` jump instantly on param change. Fix = one-pole smooth the pure-gain terms.
3. Envelope-follower/one-pole/opto denormal gaps in `ChorusComponents.h`, `eden_core`, `automakeup` (host sets FTZ in-proc; vst-host out-of-proc already patched with ScopedNoDenormals — low urgency).
4. `dsl_core.hpp lowShelf` lacks the `f>sr*0.49` clamp its siblings have (only called with 100 Hz today — latent).
5. `sharke_core.h` `dcAvg += 0.0008*(P-dcAvg)` not SR-scaled (DC-block corner doubles at 96k); `Tube::T` (setT) is dead code.
6. Oversampler adds ~16 samples base-rate latency, no `setLatency` call anywhere (only matters if a host runs a parallel dry path).

### Cross-plugin C++ duplication backlog (from the copy-paste scan; NOT applied)
Ranked by occurrence × divergence risk:
1. Scalar helper preamble in ~102 projects (`clamp01` 102×, `kPi` 92× with **3 precision variants**, `smoothstep` 52×, `softClip` 40×, `clampFreq` 34× with 8/10/18/20 Hz drift, `dn` 18× duplicating `rbtube::dn`) → new `_shared/dsp_common.hpp`; `audioTaper(v, exp)` must stay parameterized (exponent is per-pot voicing).
2. DPF plugin shell (param tables/applyAll/dual-mono wiring) ~82-154 projects → `_shared/table_plugin.hpp` CRTP/macro, mirroring how `pedal_ui.hpp` already reduced pedal UIs to ~16-line files. Incremental migration.
3. TDF2 Biquad ×34 (pedals/racks) + DF1 Biquad ×31 (amp cores) + 5 more copies inside `_shared` itself (rbgtr/citrus/rbdsl/eden/sharke, already diverged: `dB==0` early-outs, lost Nyquist clamp) → one `_shared/biquad.hpp`. ⚠ amps are calibrated — coefficient changes are regression risk; migrate with A/B render.
4. Amp NanoVG UI skeleton ×30 (`Spot`/`drawKnob`/drag handlers; also triplicated inside `_shared` pedal_ui/rack_ui/pedalkit) → `_shared/amp_ui.hpp` base; hoist mechanics, parameterize cosmetics (colors/ticks are intentional per-amp).
5. `RcHighPass`/`RcLowPass` ×20/×17, `DelayBuffer` ×14 (adopt the safer min-size guards from mod_delay), `OnePole` ×13 → `dsp_common.hpp` / small headers.
6. LFO skeletons ×31 — mostly intentional voicing divergence. Lowest priority, migrate opportunistically.

## routes.py fixes (applied by fix agent — see git diff; statuses in final report)
Atomic JSON writes helper (`_atomic_write_json`) at 5 persist sites; `_rs_map_lock`
hoisted + used in `override_query` (was mutating the shared cache + non-atomic write);
error handling in `amp_variants_upsert/delete`; `_load_cached_json` catches
`(ValueError, OSError)` + `encoding="utf-8"` sweep (~20 read/write_text sites);
`local_files`/legacy-rename use `.as_posix()` (Windows backslash refs); `html.escape`
in OAuth result page (reflected XSS via `?error=`); IR download writes tmp+`os.replace`
(partial file poisoned the cache forever); RIFF parsers catch `struct.error`;
mega-chain VST dedupe key includes slot identity (two same-VSTs in one tone collapsed
to one slot); `vst_suggest` copies before mutating the seed-catalog cache;
migration-marker read-modify-write (was clobbering sibling flags); settings lock
around load-merge-write (token-refresh vs settings-save lost refresh tokens);
chunked SQL `IN` lists (purge of >999 files → OperationalError AFTER unlinking);
stale-assignment disk check in `_auto_download_for_song`; shadowed
`_invalidate_rs_to_real` removed; `_compute_vst_state_for_piece` uses the cached
knob table (was re-parsing JSON per piece per tone).

### routes.py DEFERRED
- **B5 sqlite races**: one shared conn, several write+commit paths outside `_lock`
  (`_wire_curated_variants_to_presets`, `_wire_cabs_to_presets`, watcher vs preload
  worker...). Proper fix = per-thread connections (WAL is on) or lock every
  write+commit (~25 sites). Big, needs its own pass.
- **B6 `_batch_disk_bytes`** global corrupted by concurrent flows (budget overshoot) →
  small `_DiskBudget` class with lock.
- Duplication backlog D1–D14 (chain-stage builder ×3, cab-mic self-heal ×2 literal,
  seed resolution ×2 ~150 lines each, variant-filename resolution ×5, triple DB
  file-ref rewrite ×5, sentinel-preset get-or-create ×2, migration boilerplate ×3,
  static-asset serving ×4, RIFF walker ×2, master-exclusion SQL ×2). D1 (stage
  builder) is the prerequisite that makes the B4-class fixes one-line.

## screen.js fixes (applied by fix agent — see git diff; statuses in final report)
`rbClampDb` duplicate declaration (1-arg version shadowed the 3-arg one → normalizer
ignored user min/max settings) → renamed `rbClampLevelDb`; `rbJsStr()` helper + fixed
`JSON.stringify`-in-onclick broken HTML (Manage-tab delete buttons were dead);
staleness guards in `rbLoadSongTones`/`rbAutoDownloadSong` (rapid song switches
persisted tone data under the WRONG song); studio monitor busy-guard now re-invokes
for the latest requested view; rbInit mousedown listener once-flag + idle default-tone
load bails when preview/audition/mega-chain active (was clobbering active preview at
t+5s); normalizer interval leak guard; bypass matching disambiguates duplicated gear;
null-id audition toggle sentinel; override-tone key set only after successful load
(failed load was never retried); saved-tone persist surfaces errors (was silently
losing edits); rbInit/batch-poll null guards; `rbAudioApi()` helper replacing ~35
`window.slopsmithDesktop && window.slopsmithDesktop.audio` repeats.

### screen.js DEFERRED
- **B3 slot alignment**: `rbReapplyVstParamsToChain` / `rbReapplyBypassToChain` /
  `rbChainSlotIdForPiece` front-align `getChainState()` while mega-chain empirically
  tail-aligns (stale slots appear at the FRONT). Fix = one `rbAlignedEngineSlots()`
  used by all 4 sites. Subtle — needs live-engine testing before changing.
- **B6 catalog auto-open**: every catalog re-render (each search keystroke) re-fires
  the crash-prone standalone `loadVST` path when a VST gear is selected. Fix = only
  auto-open on explicit selection change + serialize loads. Needs UX check.
- **B9 AMP-toggle path** skips mute + input-drive/normalizer re-application (loud
  transient, then everything at drive 1.0) — fix = mirror `rbStudioFinishMonitorLoad`.
- Dead code deletion (rbRenderCatalogCard/Compact, rbLoadPending) + big dedup
  clusters D1 (fetch-json ×15), D3–D5 (inline VST param rows/capture/snapshot ×3 each),
  D7 (vst stem ×15), D12 (standalone bring-up ×3), D16 (catalog fetch ×3), D19.

## pedal_canvas.js fixes APPLIED (commit `pedal_canvas: fix listener leak...`)
- `attach()` listener leak: every editor open added a permanent window
  mousemove/mouseup pair (editor attaches twice per open — screen.js:12512) whose
  closure pinned the old `values` object → input silently routed to stale state.
  Now re-attach replaces the previous wiring (`canvas.__rbWire`), and stale
  handlers self-remove when their canvas leaves the DOM.
- Stuck drag when mouse released outside the window: `e.buttons & 1` check at the
  top of mousemove resets drag state.
- Bat-lever toggles declared in `knobs` (attackoftheclones FLANGE id 3,
  dejachorus SPD SEL id 3 / MODE id 5) were drag-only (needed ~85 px silent drag);
  now click-to-flip like switches.
- `toSpec` divide-by-zero when canvas hidden (clientWidth 0 → Infinity coords).
- `RB_AMP_FONT_SCALE` typo `boxdc30` → `boxac30` (AC30 labels were unscaled).

### pedal_canvas.js DEFERRED
- Hit-testing polish: nearest-knob tie-break on dense faces (samplegvh140c, dsl100);
  minimum switch/sw3 hit size in CSS px on wide amp faces (today ~7-9 px clickable);
  slider vertical tolerance smaller than the drawn cap (ringmod).
- No resize handling: canvas redraws only on interaction → stretched/blurry after
  window resize until next value change. Fix = ResizeObserver in attach (tie its
  disconnect to the same __rbWire teardown).
- citrusjimmybean CHANNEL (id 6) is a continuous knob that only flips its CH1/CH2
  label across 0.5 — should be a `two:true` sw3 (or `style:'bat'`).
- meve1073 declares red/blue `cap` colors that the `moog` knob style ignores;
  its `select:` knobs can't be dragged (click-to-step only, no detent marks).
- Unset `two:true` sw3 levers draw at 0.5 (impossible mid position) until clicked.
- Duplication (~1500-2000 removable lines, all byte-similar): `bolt()` ×20+,
  `jack()` ×30, `corner()` ×15, lab/lbl wrappers ×28, tolex/brushed-panel blocks
  ×25, input-cable picker ×6-9, chrome-script logo ×7, pull-button ×4,
  mini-toggle ×8, power rocker ×6; whole-face dupes: JC90 vs JC120 (~95%),
  Lovolt trio ×3, Mark II/III pair, 4 Marsten heads not using the existing
  `marstenGoldHead()` helper (plexi, jcm800, dsl100, vs100); wah trio.
  Proposed: one "amp chassis kit" of module-level helpers, zero visual change.
  Also: 9× repeated `values[id]=v; drawSpec(); onChange(); preventDefault()`
  commit sequence inside attach() → one `commit(id, v, e)` closure.

## RBFinalLeveler verification (2026-07-01)
Method: faithful Python port of processBlock's math (same K-weighting coefficients,
same envelopes/gates/limiter — scratchpad `leveler_sim.py`), 4 scenarios at defaults
(target −14, gate −50→eff −44, boost/cut ±18).

| Scenario | Result |
|---|---|
| T1 two tones (−29.3 vs −17.0 LUFS in, different spectra) | ✅ out −14.1 / −14.6 — mission "same volume" works |
| T2 stop playing, noise floor −60 dB | ✅ output gate ducks it to −97 dB (AGC holds +15.7 but gate closes). ⚠ during the 300 ms hold + 250 ms release the boosted hiss IS audible (−44.9 dB out) — a ~0.5 s "breath" after you stop |
| T3 noise floor −40 dB (ABOVE the fixed −44 gate) | ❌ hiss treated as signal: AGC rides to +18, gate never closes → hiss amplified −40→−22 dB. Noisy high-gain/fuzz chains defeat the anti-noise design |
| T4 resume after 1 s pause | ~OK: loudness detector decays during silence → AGC overshoots +14.3 vs steady +10.2 for <200 ms; audible impact small (out −15.3 vs −14.8 RMS) because the limiter+integration masks it |

Conclusions: mission 1 (equal loudness) verified good. Mission 2 worked ONLY if the
chain's idle floor was below −44 dB raw RMS.

### Improvements IMPLEMENTED (2026-07-01, commit "RBFinalLeveler: adaptive noise gate...")
1. **Noise-floor tracker + adaptive gate.** Tracks the chain's own idle floor:
   falls fast (τ 0.2 s) toward quieter blocks; rises (τ 1.5 s) only toward blocks
   that are NOISE-like = very stationary (block-level variance < 0.5 dB², i.e.
   sd < 0.7 dB — white/fuzz hiss ≈ 0.1, a steady musical pad ≈ 2.4, a decaying
   note tail far more) AND quiet (< −34 dB). On a step drop > 8 dB (playing just
   stopped) the stationarity stats re-seed at the new level so learning starts
   immediately. Gate = clamp(floor + 10 dB, [−44, −32]), still raisable by the
   gate_db param. Old fixed behavior (−44) is the exact fallback until a floor
   is learned.
2. **Loudness detector frozen during silence.** K-filters keep running; msEnv/
   rawMsEnv integrate only while the block carries signal (instantaneous raw
   level ≥ gate, computed in a cheap pre-pass so attacks have no 1-block lag).
   Resume-after-pause gain is now exact.

Sim results after (same scenarios): T1 −14.1/−14.6 unchanged; T2 idle noise
−60→−103 dB out (was −97) and the hold-window "breath" −50 dB (was −45); T3 hiss
at −40 dB now gets gated ~3–4 s after playing stops (was: boosted +18 forever),
then stays learned for the instance; T4 resume overshoot +14.3→+10.3 (= steady,
i.e. eliminated); regressions checked: steady −36 dB lp-pad and a 6 s decaying
note tail are NOT gated; a normal tone still levels to −14.1 after the gate
adapted on a noisy chain. Known residual (accepted): a perfectly stationary
white-noise-like "note" below −34 dB would be learned as noise — musically
unrealistic; and hiss in the −44..−32 window is audible for the first ~3 s
after the first pause while the floor is learned.
Deferred: shorten gate hold (300→150 ms) if the ~0.5 s post-note "breath" on
high-boost chains bothers in practice.
Rebuilt + deployed: `vst/racks/RB Final Leveler.vst3` (arm64, ad-hoc signed).

## Verification status
- [x] Headers compile: studio_verb, marshall_guvnor_plus, sharke_hb5000 (arm64)
- [x] `python3 -m py_compile routes.py`; pytest 9 passed, 3 failures pre-existing
      (reproduce identically on the pre-change tree; screen.js-content/watcher asserts)
- [x] `node --check screen.js` + pedal_canvas.js
- [ ] App smoke test (restart app, load a song tone, Manage-tab delete buttons)
- [ ] Rebuild + redeploy affected VST binaries (2 pedals, 3 reverb racks, 1 amp);
      cross-builds (Linux/Win) per CROSS_BUILD notes when releasing
