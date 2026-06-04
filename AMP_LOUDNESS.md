# Amp loudness + EQ-headroom standard

Goal: **every amp VST sounds at the same volume**, and **boosting tone/EQ never
hard-clips**. This doc is the recipe so each new amp matches the rest.

## The one rule

Every amp's per-channel output ends with the **same final stage**:

```cpp
// transparent below ±0.80, soft-saturates to a ±0.98 ceiling (no hard clip)
static inline float rbAmpLvl(float x){ const float t=0.80f,c=0.98f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }

// in run(): wrap the channel output
oL[i] = rbAmpLvl(kLvl * core.process(iL[i]));
```

Two parts:

1. **`kLvl`** — a per-amp constant that makes the amp hit the common loudness
   **target ≈ 0.30 RMS** (a 110 Hz–1.8 kHz multitone, measured at the amp's real
   Rocksmith song settings). That's what equalizes volume across amps.
2. **`rbAmpLvl`** — a soft ceiling: it does nothing below ±0.80 (so the matched
   tone is untouched), then rounds off to ±0.98 above. So an EQ/tone boost
   soft-saturates near full scale instead of hard-clipping the engine. ~0.30 RMS
   leaves ≈10 dB of headroom before it engages.

`0.30 RMS` (≈ −10.5 dBFS) is the house reference — low enough for EQ headroom,
loud enough to not bury the rig. Don't change it per-amp; only `kLvl` changes.

## Tuning `kLvl` for a NEW amp (5 steps)

1. Build the amp with `kLvl = 1.0f` first.
2. Measure its **multitone RMS at the real RS settings** (the knob values the
   game actually sends — pull them from `nam_tone.db` `preset_pieces.vst_state`,
   take the median across songs that use the gear). Harness below.
3. `kLvl = 0.30 / measured_RMS`.
4. Rebuild, re-measure → should read ~0.30. Re-measure the **EQ-max peak** (all
   tone/EQ knobs at 1.0): must be **≤ ~0.98** (rbAmpLvl guarantees it).
5. Verify against another amp by ear — they should feel equal.

> Measure at REAL settings, not "noon": bass amps sit at low gain / high Ultra-Lo,
> guitar amps at high gain — noon lies. White noise / a single sine also lie
> (they ignore the amp's voicing); a **multitone 110 Hz–1.8 kHz** is the honest
> proxy. (This is why earlier white-noise passes drifted ~10–15 dB.)

## Measurement harness (offline, no host)

Include the amp's `*Plugin.cpp`, derive a `Probe` to reach protected
`run()`/`setParameterValue()`/`initParameter()`/`sampleRateChanged()`, feed a
multitone at input RMS 0.1, read output RMS of the settled half. Compile with
`/usr/bin/clang++ -isysroot $(xcrun --show-sdk-path) -std=c++14 -I. -I.. \
-I../../DPF/distrho -I../../DPF/dgl harness.cpp ../../DPF/distrho/src/DistrhoPlugin.cpp`.
Call `sampleRateChanged(48000)` (host normally sets SR; offline it's 0). Set the
gain-equivalent param by NAME — it's "Gain" for most, **"Solid State"** for
Hartke (Sharke), **"Volume"** for the GK (FK800). See [[reference_build_bundled_vsts]].

## Current kLvl values (target 0.30, 2026-06-04)

| amp | kLvl | | amp | kLvl |
|---|---|---|---|---|
| DSL100 | 1.012 | | MarkIII | 0.659 |
| DualRect | 1.192 | | MarkIV | 0.680 |
| EN30 / Box DC30 | 0.767 | | FK800 (GK) | 1.356 |
| TW22 / SuperNova | 0.735 | | Sharke HB3500 | 0.283 |
| TW26 / Deluxe | 0.894 | | Sharke HB5000 | 0.396 |
| TW40 | 1.692 | | Sampleg SVT | 0.579 |

Result: real-settings loudness spread **0.01 dB** across all 12 (was 15.5 dB);
EQ-max peaks all ≤ 0.98 (FK800 was 6.59, HB3500 3.55 → now bounded).

## Gotchas

- **No hot reload + orphan backend**: closing Slopsmith doesn't kill the uvicorn
  backend; the next open serves STALE code. After rebuilding, `Cmd+Q` →
  `pkill -f "uvicorn server:app"` → reopen. (See [[project_slopsmith_plugins]].)
- After editing source, rebuild → copy binary into `vst/amps/<Bundle>.vst3/Contents/MacOS/<BIN>`
  → `codesign --force --sign -`. Renamed bundles (Box DC30 etc.) keep the build
  NAME as the inner binary (EN30) but a parody folder name.
- The amps the colleague restructured (EN30/TW22/TW26 → Core.h) keep their
  internal makeup; `rbAmpLvl(kLvl·…)` just wraps it — that's fine.
