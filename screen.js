// NAM Rig Builder plugin — the game tone → NAM preset mapping UI.
window.RB_API = window.RB_API || '/api/plugins/rig_builder';

window.NAM_API = window.NAM_API || '/api/plugins/nam_tone';

function rbScopeCss(css) {
    const prefixSelector = (selector) => {
        const s = selector.trim();
        if (!s || s.startsWith('#rb-root')) return s;
        if (s === 'html' || s === 'body' || s === ':root') return '#rb-root';
        return `#rb-root ${s}`;
    };
    let out = '';
    let i = 0;
    while (i < css.length) {
        const open = css.indexOf('{', i);
        if (open < 0) {
            out += css.slice(i);
            break;
        }
        const prelude = css.slice(i, open).trim();
        let depth = 1;
        let j = open + 1;
        while (j < css.length && depth > 0) {
            const ch = css[j++];
            if (ch === '{') depth += 1;
            else if (ch === '}') depth -= 1;
        }
        const body = css.slice(open + 1, j - 1);
        if (/^@(media|supports|container)\b/.test(prelude)) {
            out += `${prelude}{${rbScopeCss(body)}}`;
        } else if (/^@(keyframes|-webkit-keyframes|font-face)\b/.test(prelude)) {
            out += `${prelude}{${body}}`;
        } else {
            out += `${prelude.split(',').map(prefixSelector).join(',')}{${body}}`;
        }
        i = j;
    }
    return out;
}

function rbEnsureScopedCss() {
    const existing = document.getElementById('rb-css');
    if (existing && existing.tagName === 'STYLE') return;
    if (window.__rbCssLoading) return;
    if (existing) existing.remove();
    window.__rbCssLoading = true;
    fetch('/api/plugins/rig_builder/asset/rb.css')
        .then(r => r.ok ? r.text() : Promise.reject(new Error(`HTTP ${r.status}`)))
        .then(css => {
            const st = document.createElement('style');
            st.id = 'rb-css';
            st.textContent = rbScopeCss(css);
            document.head.appendChild(st);
        })
        .catch(e => console.warn('[rig_builder] failed to load scoped CSS:', e))
        .finally(() => { window.__rbCssLoading = false; });
}

(function () {
    // Idempotency: showScreen is wrapped at most once even if screen.js
    // is re-evaluated by the host. Without this guard, each re-eval
    // captures the previous wrapper and we leak closures + run rbInit
    // multiple times per navigation.
    const HOOK_KEY = '__slopsmithRigBuilderInstalled';
    if (window[HOOK_KEY]) return;
    window[HOOK_KEY] = true;

    // Load pedal_canvas.js (in-app canvas recreations of the bundled pedal UIs).
    // plugin.json only loads screen.js, so we inject it here and warm the fonts.
    if (!document.getElementById('rb-pedal-canvas-js')) {
        const sc = document.createElement('script');
        sc.id = 'rb-pedal-canvas-js';
        sc.src = '/api/plugins/rig_builder/asset/pedal_canvas.js';
        sc.onload = () => {
            try {
                window.RBPedalCanvas && window.RBPedalCanvas.ready().then(() => {
                    try { if (rbState.currentTab === 'gear') rbApplyGearFilters(); } catch (_) {}
                });
            } catch (_) {}
        };
        document.head.appendChild(sc);
    }

    const origShowScreen = window.showScreen;

    if (typeof origShowScreen === 'function') {
        window.showScreen = function (...args) {
            const id = args[0];

            const result = origShowScreen.apply(this, args);

            try {
                if (id === 'plugin-rig_builder') {
                    rbInit();
                } else if (typeof rbOnLeaveRigBuilder === 'function') {
                    rbOnLeaveRigBuilder();
                }
            } catch (e) {
                console.warn('[rig_builder] showScreen hook failed:', e);
            }

            return result;
        };
    }
    // The host always emits 'screen:changed' on navigation — even when it calls
    // its own lexically-scoped showScreen, which would bypass the wrapper above.
    // Use it as the authoritative "we left Rig Builder" signal so the master
    // VST editor's native window is reliably closed before the song player
    // loads a preset (the intermittent "edit master VST → play song → crash").
    if (window.slopsmith && typeof window.slopsmith.on === 'function') {
        window.slopsmith.on('screen:changed', (e) => {
            const id = e && e.detail && e.detail.id;
            if (id && id !== 'plugin-rig_builder') rbOnLeaveRigBuilder();
        });
    }
})();

// ── Full-chain playback (no bundle edit, survives app updates) ─────────
// Real song playback resolves a tone → preset_id and fetches nam_tone's
// /native-preset/{id}, which the bundle builds from the 2-column presets
// table (single amp + cab). We transparently redirect just that GET to
// rig_builder's /native_preset_full/{id} (identical response shape) so the
// engine receives EVERY NAM stage (pedal → amp → … → cab). Scoped to that
// one URL; everything else passes through untouched. Kill-switch:
// window.__rbChainPlayback = false.
(function () {
    if (window.__rbFetchPatched) return;
    window.__rbFetchPatched = true;
    const origFetch = window.fetch.bind(window);
    const RE = /\/api\/plugins\/nam_tone\/native-preset\/(\d+)(?:[/?#]|$)/;
    window.fetch = function (input, init) {
        let url;
        try { url = typeof input === 'string' ? input : (input && input.url); } catch (_) { url = null; }
        const m = (typeof url === 'string') ? url.match(RE) : null;
        if (!m || window.__rbChainPlayback === false) {
            return origFetch(input, init);
        }
        const fullUrl = `/api/plugins/rig_builder/native_preset_full/${m[1]}`;
        return origFetch(fullUrl, init).then(async (r) => {
            if (!r.ok) return origFetch(input, init);            // build failed → original 2-stage
            const txt = await r.text();
            try {
                const data = JSON.parse(txt);
                const chain = data && data.native_preset && data.native_preset.chain;
                if (!Array.isArray(chain) || chain.length === 0) return origFetch(input, init);
                void rbSyncAudioEffectsCapability('full-chain-playback', { chain, mode: 'song-playback', bridge: true });
                // Including how many master_pre / master_post stages were
                // injected — handy for the "master chain not heard in song
                // playback" diagnostic. If both counts are zero here but
                // the master tab shows pieces, those pieces are missing
                // files on disk (silently skipped by _build_master_stages).
                const mPre  = data.master_pre_count  || 0;
                const mPost = data.master_post_count || 0;
                console.log(`[rig_builder] full-chain playback: preset ${m[1]} → ${chain.length} stages`
                    + ` (${data.nam_stage_count} NAM + ${chain.length - data.nam_stage_count - mPre - mPost} song IR/VST`
                    + ` + ${mPre} master_pre + ${mPost} master_post)`
                    + (data.missing && data.missing.length ? ` · missing files: ${data.missing.join(', ')}` : ''));
                // PROACTIVE TRANSIENT KILL: the bundle calls loadPreset ~1ms
                // after we return this response. We can't monkey-patch
                // `slopsmithDesktop.audio.loadPreset` directly because the
                // object is frozen by Electron's contextBridge (we verified
                // this: assignments silently no-op). But the exposed methods
                // *are* callable from here, so we mute right now — before
                // returning — so by the time loadPreset starts processing
                // its first audio buffer the chain output is already at 0.
                // Restore happens on a timer (~300ms covers a 4-NAM standard
                // chain at buffer 256). Kill-switch:
                // `window.__rbMutePreLoad = false`.
                rbPreLoadMute(chain.length, rbChainGainTargetFor(chain)).catch(() => {});
                // Schedule a VST-param re-apply after the bundle's
                // loadPreset finishes. Without this, VSTs in the chain
                // play at plug-in defaults until the user opens each
                // VST editor (which itself triggers a setParameter
                // walk). Delay = hold time + 50 ms cushion. Matches
                // rbPreLoadMute's new 100 + 50/stage baseline.
                const reapplyDelay = (100 + 50 * Math.max(1, chain.length | 0)) + 50;
                rbReapplyVstParamsAfterLoad(chain, reapplyDelay);
                // Re-apply the chain-input drive after the bundle's
                // loadPreset finishes. The engine resets `input` to 1.0
                // on every chain reload — without this re-apply, amp
                // NAMs sit in their clean operating region and the
                // entire library sounds "very clean and similar".
                // Same delay strategy as the VST param re-apply so it
                // lands after the chain has settled in the engine.
                setTimeout(() => { rbApplyChainInputDrive({ chain }); }, reapplyDelay);

                setTimeout(() => {
                    rbStartFinalChainNormalizer(chain);
                }, reapplyDelay + 150);

            } catch (_) {
                return origFetch(input, init);
            }
            return new Response(txt, { status: 200, headers: { 'Content-Type': 'application/json' } });
        }).catch(() => origFetch(input, init));               // any error → original
    };
})();

// Engine input drive — pre-NAM gain set via setGain('input', X). The
// audio engine's `state.inputLevel` on each NAM stage was empirically
// confirmed to be a no-op (raising it from 1.0 to 8.0 had zero effect
// on tone). The chain-level input gain DOES work: setting it to 8.0
// (≈+18 dB) drives the amp NAMs from their clean operating region into
// the saturation captured at -3 dBFS test tones, restoring the actual
// "JCM800 at gain 10" character the captures contain.
//
// Read from /settings (`nam_chain_input_drive`, default 8.0). Cached
// in `window.__rbChainInputDrive` so repeated calls (4 hooks below)
// don't all refetch — the boot-time fetch in rbInit / mega-chain hook
// populates it. Falls back to 8.0 if the cache hasn't loaded yet.
//
// The old rule was "all guitars get 8×". That fixes high-gain amps, but it
// also pushes clean amp captures into breakup. Prefer the active amp's stored
// the game Gain when the chain JSON has it, and fall back to the old
// guitar/bass split only when the chain has no useful amp metadata.
//
// The engine resets input gain to 1.0 on every chain reload, so we
// have to re-apply after each loadPreset. Hooks:
//   - fetch interceptor (bundle's chain load)
//   - mega-chain build (initial preload at song start)
//   - rbListenTone (Listen ▶ in per-song view)
//   - rbAuditionFile (▶ in Gear catalog)
function rbNumberOrNull(value) {
    const n = Number(value);
    return Number.isFinite(n) ? n : null;
}

function rbSmoothstep01(value) {
    const t = Math.max(0, Math.min(1, Number(value) || 0));
    return t * t * (3 - 2 * t);
}

function rbConfiguredChainInputDrive() {
    return (typeof window.__rbChainInputDrive === 'number' && window.__rbChainInputDrive >= 0)
        ? window.__rbChainInputDrive : 8.0;
}

function rbCleanGuitarChainInputDrive(maxDrive) {
    // The backend's NAM output normalization and the captures themselves
    // expect a guitar-level push even for clean amps. Unity made clean tones
    // too quiet and made crunch/dist never reach their captured breakup.
    return Math.min(maxDrive, Math.max(3.5, maxDrive * 0.68));
}

function rbLooksLikeBassFromHighway() {
    try {
        const hw = window.highway;
        const sc = hw && typeof hw.getStringCount === 'function'
            ? hw.getStringCount() : null;
        return (typeof sc === 'number' && sc > 0 && sc <= 4);
    } catch (_) {
        return false;
    }
}

function rbCleanishAmpDrive(stage, maxDrive) {
    const gear = String(stage && stage.rs_gear || '');
    if (gear.startsWith('Bass_') || gear.startsWith('DI_Amp_')) return 1.0;

    const cleanDrive = rbCleanGuitarChainInputDrive(maxDrive);
    const gain = rbNumberOrNull(stage && (stage.rs_gain ?? stage.rsGain));
    if (gain !== null) {
        if (gain <= 20) return cleanDrive * 0.82;
        return cleanDrive + (maxDrive - cleanDrive) * rbSmoothstep01((gain - 30.0) / 45.0);
    }

    // Metadata fallback for catalog audition or older cached chains.
    const haystack = [
        stage && stage.name,
        stage && stage.path,
        stage && stage.rs_gear,
    ].filter(Boolean).join(' ').toLowerCase();
    if (/\bclean\b/.test(haystack)) return cleanDrive;
    if (/amp_en30/i.test(gear) && /_v0?3(?:_|\.|$)/i.test(haystack)) return cleanDrive;
    return maxDrive;
}

function rbActiveAmpStageForChain(chain) {
    if (!Array.isArray(chain)) return null;
    for (const stage of chain) {
        if (!stage || stage.bypassed) continue;
        const isAmp = Number(stage.type) === 1 && String(stage.slot || '').toLowerCase() === 'amp';
        if (isAmp) return stage;
    }
    return null;
}

function rbPostAmpMakeupForChain(chainSpec) {
    const amp = rbActiveAmpStageForChain(chainSpec);
    if (!amp) return 1.0;
    const gear = String(amp.rs_gear || '');
    if (gear.startsWith('Bass_') || gear.startsWith('DI_Amp_')) return 1.0;

    const maxDrive = rbConfiguredChainInputDrive();
    const drive = rbCleanishAmpDrive(amp, maxDrive);
    const ratio = maxDrive / Math.max(1.0, drive);
    let makeup = Math.pow(Math.max(1.0, ratio), 0.9);

    const gain = rbNumberOrNull(amp.rs_gain ?? amp.rsGain);
    if (gain !== null) {
        // Clean amps need their level recovered after we reduce pre-NAM drive
        // to keep them clean. Do that post-amp so volume comes back without
        // pushing the model into breakup again.
        if (gain <= 20) makeup *= 1.70;
        else if (gain <= 45) makeup *= 1.42;
        else if (gain <= 60) makeup *= 1.16;
    }
    return Math.max(1.0, Math.min(3.25, makeup));
}

function rbDriveForChainInput(opts) {
    const maxDrive = rbConfiguredChainInputDrive();
    if (opts && opts.isBass === true) return 1.0;

    const chain = opts && Array.isArray(opts.chain) ? opts.chain : null;
    if (chain) {
        const activeAmp = rbActiveAmpStageForChain(chain);
        if (activeAmp) return rbCleanishAmpDrive(activeAmp, maxDrive);
        return 1.0;
    }

    if (opts && opts.isBass === false) return maxDrive;
    return rbLooksLikeBassFromHighway() ? 1.0 : maxDrive;
}

function rbApplyChainInputDrive(opts) {
    const drive = rbDriveForChainInput(opts);
    // Re-poll guard: the song-playback callers fire this ~600 ms after
    // the bundle's chain load — but `highway.getStringCount()` may not
    // have absorbed the song_info WS message yet (it defaults to 6
    // until the first song_info arrives). For a bass arrangement that
    // means we'd land here once with guitar drive (8×), distort the
    // bass amp, and never re-check. Schedule two cheap re-applies at
    // +1500 ms and +3500 ms post-initial-call; each one re-runs the
    // detection. If stringCount has flipped to 4 by then we update the
    // gain. No-op if it's still guitar (setGain to the same value is
    // idempotent on the engine side). Skipped when the caller passed
    // an explicit isBass — they already KNOW the answer (catalog
    // audition path).
    const calledExplicitly = opts && (opts.isBass === true || opts.isBass === false || Array.isArray(opts.chain));
    if (!calledExplicitly && !(opts && opts._isRepoll)) {
        setTimeout(() => rbApplyChainInputDrive({ _isRepoll: true }), 1500);
        setTimeout(() => rbApplyChainInputDrive({ _isRepoll: true }), 3500);
    }
    return rbSetRouteGainsWithHost({ input: drive }, 'chain-input-drive').then((handled) => {
        if (handled) return;
        const audio = window.slopsmithDesktop && window.slopsmithDesktop.audio;
        if (!audio || typeof audio.setGain !== 'function') return;
        return audio.setGain('input', drive).catch((e) => {
            console.warn('[rig_builder] setGain(input,', drive, ') failed:', e);
        });
    });
}

// Compute the chain-gain target for a given chain spec: looks at what's
// actually active to estimate how much output level the chain will
// produce, and returns a multiplier that brings it to a perceived-flat
// level. Solves the "amp raw is loud / amp through cab is quiet"
// asymmetry without a slider — the caller passes this to rbPreLoadMute
// so the fade-in lands at the right level for whatever this chain has.
//
//   active amp + the game cab IR → ×2.0 (RS cabs are raw/quiet — boost +6 dB)
//   active amp + non-RS cab IR    → ×1.0 (tone3000 IRs are already loudness-
//                                         normalized — boosting them over-drove
//                                         the output, the "too boosted/saturated
//                                         without the game cab" report)
//   active amp + no cab IR        → ×0.5 (knock the raw-amp spike down)
//   no active amp / fallback      → ×1.0 (don't change anything)
// Extra lift for VST-amp chains (they output below the NAM loudness reference).
const RB_VST_AMP_BOOST = 10.0;   // ~+10 dB
// Acoustic / DI "bare" cabs (e.g. PA600C) play with NO amp, so they never get
// the amp-path +6 dB RS-cab boost and come out too quiet. Lift an active
// the game cab IR that has no amp in front of it. Tunable: window.__rbBareCabBoost.
const RB_BARE_CAB_BOOST = 2.5;   // ~+8 dB
function rbBareCabBoostFor(chainSpec) {
    if (!Array.isArray(chainSpec)) return 1.0;
    let hasRsCab = false, hasAmp = false;
    for (const s of chainSpec) {
        if (!s || s.bypassed) continue;
        if (s.slot === 'amp' && (s.type === 1 || s.type === 0)) hasAmp = true;
        if (s.type === 2 && String(s.path || '').toLowerCase().includes('rocksmith')) hasRsCab = true;
    }
    if (hasRsCab && !hasAmp) {
        return (typeof window.__rbBareCabBoost === 'number') ? window.__rbBareCabBoost : RB_BARE_CAB_BOOST;
    }
    return 1.0;
}
function rbChainGainTargetFor(chainSpec) {
    // User "Chain volume" trim (chain_makeup, default 1.0) — the ONLY level
    // the engine respects (per-stage IR gain is ignored). Multiplies the
    // auto-leveled base below.
    const makeup = (typeof window.__rbChainMakeup === 'number') ? window.__rbChainMakeup : 1.0;
    if (rbChainHasFinalLeveler(chainSpec)) {
    window.__rbChainBaseTarget = 1.0;
    // Chain Volume is applied POST-leveler (baked into the leveler's Output
    // Trim at build time + live via RbMegaChain.setOutputTrimDb), so the
    // pre-leveler chain bus must NOT also apply `makeup` — the AGC would just
    // cancel it (that was the "x5 does nothing / everything quiet" bug).
    return rbBareCabBoostFor(chainSpec);
    }

    let base = 1.0;
    if (Array.isArray(chainSpec)) {
        let hasActiveAmp = false, hasActiveVstAmp = false, hasRsCab = false, hasOtherCab = false, activeNamCount = 0;
        let rsCabMakeup = 1.0;
        for (const stage of chainSpec) {
            if (!stage || stage.bypassed) continue;
            // The per-amp loudness trim is a clean unit-impulse IR stage; it's
            // not a cab — skip it so it doesn't flip hasOtherCab and drop the
            // amp-only -6 dB. (Only reachable when the final leveler is absent.)
            if (stage.rs_gear === '__rb_amp_trim') continue;
            if (stage.type === 1) {
                activeNamCount++;
                if (stage.slot === 'amp') hasActiveAmp = true;
            }
            if (stage.type === 0 && stage.slot === 'amp') hasActiveVstAmp = true;
            // type 2 = IR. A game cab IR lives under nam_irs/rocksmith/ and
            // is RAW (quiet → needs +6 dB). A tone3000 IR is already normalized
            // (boosting it is what saturated non-RS-cab tones), so 0 dB.
            if (stage.type === 2) {
                if (String(stage.path || '').toLowerCase().includes('rocksmith')) {
                    hasRsCab = true;
                    // Per-cab RMS-match factor from the backend (target_L2 / ‖IR‖₂).
                    // Equalizes broadband output RMS across cabs/mics so the
                    // peakiest IRs (pulled ~8 dB down by the clip-safe peak cap)
                    // don't play quieter than the rest. Last active RS cab wins.
                    if (typeof stage.cab_rms_makeup === 'number' && stage.cab_rms_makeup > 0) {
                        rsCabMakeup = stage.cab_rms_makeup;
                    }
                } else hasOtherCab = true;
            }
        }
        // Auto makeup (dB): +6 for a game cab, 0 for a non-RS (tone3000)
        // cab, -6 if amp-only; +2 per extra NAM beyond the first; capped at +18.
        // Applies to an active NAM amp OR a VST amp — the VST-amp case used to be
        // skipped, so VST-amp cabs got no boost and played far quieter than the
        // direct (cab-bypassed) signal. The cab IR convolution is heavily
        // attenuated by the engine, so this +6 dB (× the per-cab RMS match) is
        // what brings the cab back up to the DI level instead of being lost.
        if (hasActiveAmp || hasActiveVstAmp) {
            const cabDb = hasRsCab ? 6 : (hasOtherCab ? 0 : -6);
            let dB = cabDb + 2 * Math.max(0, activeNamCount - 1);
            dB = Math.max(-12, Math.min(18, dB));
            base = Math.pow(10, dB / 20);
            // Apply the per-cab RMS match OUTSIDE the dB clamp above (which caps
            // the multi-NAM stack, a different axis) so the level equalization is
            // never clipped. rbClampChainGainTarget still bounds the final target.
            if (hasRsCab) base *= rsCabMakeup;
            base *= rbPostAmpMakeupForChain(chainSpec);
            // VST amps output lower than the loudness-normalized NAM reference the
            // chain gain is calibrated for, so the whole VST-amp chain (amp+cab)
            // plays quiet — users had to crank the Chain volume well above default.
            // Lift pure VST-amp chains so the DEFAULT Chain volume already sounds
            // right. (Empirical: a user sitting at ~×10 needed ~+8 dB to reach the
            // ×4 default. Tunable via RB_VST_AMP_BOOST.)
            if (hasActiveVstAmp && !hasActiveAmp) base *= RB_VST_AMP_BOOST;
        }
    }
    window.__rbChainBaseTarget = base;   // remember (pre-trim) for live makeup changes
    return base * makeup * rbBareCabBoostFor(chainSpec);
}

function rbClampChainGainTarget(targetGain) {
    return (typeof targetGain === 'number' && isFinite(targetGain) && targetGain >= 0)
        ? Math.max(0, Math.min(64, targetGain))
        : 1.0;
}

function rbAudioEffectsLoadOptionsForChain(chain, opts) {
    const targetGain = rbClampChainGainTarget(rbChainGainTargetFor(chain));
    const chainLen = Array.isArray(chain) ? chain.length : 0;
    const hold = (typeof window.__rbMutePreLoadHold === 'number')
        ? Math.max(20, window.__rbMutePreLoadHold | 0)
        : 250 + 120 * Math.max(1, chainLen | 0);
    return {
        preloadMute: {
            enabled: window.__rbMutePreLoad !== false,
            dryDuringLoad: window.__rbDryDuringLoad !== false,
            targetGain,
            holdMs: hold,
        },
        gains: {
            input: rbDriveForChainInput({ chain }),
            chain: targetGain,
        },
        startAudio: !!(opts && opts.startAudio),
    };
}

async function rbSetRouteGainsWithHost(gains, reason) {
    rbRegisterAudioEffectsCapability();
    const audioEffects = rbAudioEffectsApi();
    if (!audioEffects || typeof audioEffects.setRouteGain !== 'function') return false;
    try {
        const result = await audioEffects.setRouteGain({
            routeKey: RB_EFFECTS_ROUTE_KEY,
            authorization: 'playback-session',
            gains,
            summary: { reason: reason || 'rig-builder-gain' },
        });
        if (result && result.outcome === 'handled') return true;
        if (result && result.outcome !== 'no-target' && result.outcome !== 'no-handler') {
            console.warn('[rig_builder] audio-effects route gain was not handled:', result);
        }
    } catch (e) {
        console.warn('[rig_builder] audio-effects route gain failed:', e);
    }
    return false;
}

async function rbApplyChainOutputGain(opts) {
    const chain = opts && Array.isArray(opts.chain) ? opts.chain : null;
    if (!chain) return;
    const target = rbClampChainGainTarget(rbChainGainTargetFor(chain));
    window.__rbPendingChainGainTarget = target;
    if (await rbSetRouteGainsWithHost({ chain: target }, 'chain-output-gain')) return;
    const audio = window.slopsmithDesktop && window.slopsmithDesktop.audio;
    if (!audio || typeof audio.setGain !== 'function') return;
    return audio.setGain('chain', target).catch((e) => {
        console.warn('[rig_builder] setGain(chain,', target, ') failed:', e);
    });
}

// ── Final chain normalizer ─────────────────────────────────────────────
// Normalizes the COMPLETE chain output, independent of whether the stages
// are NAM, VST or IR. This is the correct level-matching layer:
//
//   pre_pedal → amp → post_pedal → rack → cabinet → FINAL NORMALIZER
//
// It requires the native audio engine to expose an output meter such as:
//   audio.getOutputMeter() → { rmsDb, peakDb }
//
// If the current engine build has no meter API, this falls back to the old
// rbApplyChainOutputGain heuristic so playback never breaks.
const RB_FINAL_NORM_DEFAULTS = {
    enabled: true,
    targetRmsDb: -14.0,
    minGainDb: -20.0,
    maxGainDb: 20.0,
    gateDb: -45.0,
    attackMs: 12,
    releaseMs: 120,
};

function rbIsFinalLevelerStage(stage) {
    if (!stage || Number(stage.type) !== 0) return false;

    const hay = [
        stage.rs_gear,
        stage.name,
        stage.path,
    ].filter(Boolean).join(' ').toLowerCase();

    return hay.includes('__rb_final_leveler__')
        || hay.includes('rb final leveler')
        || hay.includes('rbfinalleveler')
        || hay.includes('rb_final_leveler');
}

function rbChainHasFinalLeveler(chainSpec) {
    return Array.isArray(chainSpec)
        && chainSpec.some(stage => stage && !stage.bypassed && rbIsFinalLevelerStage(stage));
}

let _rbFinalNormTimer = null;
let _rbFinalNormRunId = 0;
let _rbFinalNormCorrectionDb = 0;
let _rbFinalNormWarnedNoMeter = false;

function rbDbToGain(db) {
    return Math.pow(10, db / 20);
}

function rbGainToDb(gain) {
    return 20 * Math.log10(Math.max(1e-9, Number(gain) || 0));
}

function rbClampDb(db, minDb, maxDb) {
    return Math.max(minDb, Math.min(maxDb, db));
}

async function rbLoadFinalNormSettings() {
    try {
        const r = await fetch(`${window.RB_API}/settings`);
        const s = await r.json();
        return {
            enabled: s.final_chain_normalize !== false,
            targetRmsDb: Number.isFinite(Number(s.final_chain_target_rms_db)) ? Number(s.final_chain_target_rms_db) : -14.0,
            minGainDb: Number.isFinite(Number(s.final_chain_min_gain_db)) ? Number(s.final_chain_min_gain_db) : -20.0,
            maxGainDb: Number.isFinite(Number(s.final_chain_max_gain_db)) ? Number(s.final_chain_max_gain_db) : 20.0,
            gateDb: Number.isFinite(Number(s.final_chain_gate_db)) ? Number(s.final_chain_gate_db) : -45.0,
            attackMs: Number.isFinite(Number(s.final_chain_attack_ms)) ? Math.min(Number(s.final_chain_attack_ms), 80) : 12,
            releaseMs: Number.isFinite(Number(s.final_chain_release_ms)) ? Math.min(Number(s.final_chain_release_ms), 250) : 120,
        };
    } catch (_) {
        return RB_FINAL_NORM_DEFAULTS;
    }
}

function rbNormalizeMeterShape(raw) {
    if (!raw || typeof raw !== 'object') return null;

    const firstNumber = (...keys) => {
        for (const k of keys) {
            const v = raw[k];
            if (typeof v === 'number' && Number.isFinite(v)) return v;
        }
        return null;
    };

    let rmsDb = firstNumber(
        'rmsDb',
        'rms_db',
        'chainRmsDb',
        'chain_rms_db',
        'outputRmsDb',
        'output_rms_db',
        'postChainRmsDb',
        'post_chain_rms_db'
    );

    let peakDb = firstNumber(
        'peakDb',
        'peak_db',
        'chainPeakDb',
        'chain_peak_db',
        'outputPeakDb',
        'output_peak_db',
        'postChainPeakDb',
        'post_chain_peak_db'
    );

    // Accept linear meters too, if the engine exposes them that way.
    if (rmsDb === null) {
        const rms = firstNumber('rms', 'chainRms', 'outputRms', 'postChainRms');
        if (rms !== null) rmsDb = rbGainToDb(rms);
    }

    if (peakDb === null) {
        const peak = firstNumber('peak', 'chainPeak', 'outputPeak', 'postChainPeak');
        if (peak !== null) peakDb = rbGainToDb(peak);
    }

    if (rmsDb === null) return null;
    if (peakDb === null) peakDb = -999;

    return { rmsDb, peakDb };
}

async function rbReadFinalOutputMeter(audio) {
    if (!audio) return null;

    const candidates = [
        'getOutputMeter',
        'getChainMeter',
        'getPostChainMeter',
        'getMeter',
        'getLevels',
    ];

    for (const fn of candidates) {
        if (typeof audio[fn] !== 'function') continue;
        try {
            const raw = await audio[fn]();
            const meter = rbNormalizeMeterShape(raw);
            if (meter) return meter;
        } catch (_) {}
    }

    return null;
}

function rbStopFinalChainNormalizer() {
    _rbFinalNormRunId += 1;
    if (_rbFinalNormTimer) {
        clearInterval(_rbFinalNormTimer);
        _rbFinalNormTimer = null;
    }
}

async function rbStartFinalChainNormalizer(chainSpec, opts) {
    rbStopFinalChainNormalizer();

    const chain = Array.isArray(chainSpec) ? chainSpec : [];
    const audio = window.slopsmithDesktop && window.slopsmithDesktop.audio;
    if (!audio || typeof audio.setGain !== 'function') return;

    if (rbChainHasFinalLeveler(chain)) {
        const userTrim = (typeof window.__rbChainMakeup === 'number')
            ? window.__rbChainMakeup
            : 1.0;
        // Lift bare/acoustic RS cabs (no amp) that the leveler can't bring up
        // from under its gate — same boost the song-playback path applies.
        const target = userTrim * rbBareCabBoostFor(chain);
        window.__rbChainBaseTarget = 1.0;
        window.__rbPendingChainGainTarget = target;
        try {
            await audio.setGain('chain', rbClampChainGainTarget(target));
        } catch (e) {
            console.warn('[rig_builder final-leveler] setGain(chain) failed:', e);
        }
        return;
    }

    const settings = await rbLoadFinalNormSettings();

    // Bootstrap with the existing smart target. This preserves all the old
    // safety logic and gives the normalizer a sane starting point.
    const initialTarget = rbClampChainGainTarget(rbChainGainTargetFor(chain));
    const baseTarget = (typeof window.__rbChainBaseTarget === 'number')
        ? window.__rbChainBaseTarget
        : Math.max(0.0001, initialTarget / Math.max(0.0001, (typeof window.__rbChainMakeup === 'number' ? window.__rbChainMakeup : 1.0)));

    window.__rbPendingChainGainTarget = initialTarget;

    try {
        await audio.setGain('chain', initialTarget);
    } catch (e) {
        console.warn('[rig_builder final-norm] initial setGain(chain) failed:', e);
    }

    if (!settings.enabled) return;

    const firstMeter = await rbReadFinalOutputMeter(audio);

    // No meter API = cannot do real final loudness normalization.
    // Keep old behaviour instead of breaking playback.
    if (!firstMeter) {
        if (!_rbFinalNormWarnedNoMeter) {
            _rbFinalNormWarnedNoMeter = true;
            console.warn('[rig_builder final-norm] no output meter API found. Falling back to rbChainGainTargetFor(). Add audio.getOutputMeter() to enable true final-chain normalization.');
        }
        return;
    }

    const runId = ++_rbFinalNormRunId;
    _rbFinalNormCorrectionDb = 0;

    const tickMs = 100;

    _rbFinalNormTimer = setInterval(async () => {
        if (runId !== _rbFinalNormRunId) return;

        const meter = await rbReadFinalOutputMeter(audio);
        if (!meter) return;

        const rmsDb = meter.rmsDb;
        const peakDb = meter.peakDb;

        // Do not raise silence/noise. Otherwise the normalizer will crank gain
        // when the player is not playing.
        if (rmsDb < settings.gateDb) return;

        let wantedCorrectionDb = settings.targetRmsDb - rmsDb;
        wantedCorrectionDb = rbClampDb(
            wantedCorrectionDb,
            settings.minGainDb,
            settings.maxGainDb
        );

        // Anti-clip guard: if peaks are already close to 0 dBFS, do not keep
        // increasing gain. Cutting is still allowed.
        if (peakDb > -3.0 && wantedCorrectionDb > _rbFinalNormCorrectionDb) {
            wantedCorrectionDb = _rbFinalNormCorrectionDb;
        }

        const movingUp = wantedCorrectionDb > _rbFinalNormCorrectionDb;
        const smoothMs = movingUp ? settings.attackMs : settings.releaseMs;
        const alpha = Math.min(1, tickMs / Math.max(1, smoothMs));

        _rbFinalNormCorrectionDb += (wantedCorrectionDb - _rbFinalNormCorrectionDb) * alpha;

        const userTrim = (typeof window.__rbChainMakeup === 'number')
            ? window.__rbChainMakeup
            : 1.0;

        const finalGain = rbClampChainGainTarget(
            baseTarget * rbDbToGain(_rbFinalNormCorrectionDb) * userTrim
        );

        window.__rbPendingChainGainTarget = finalGain;

        try {
            await audio.setGain('chain', finalGain);
        } catch (_) {}
    }, tickMs);
}

// Rotary knob widget for the Setup "Levels" controls. Renders an SVG knob into
// `containerId`, vertical-drag to change (full range over ~150 px), double-click
// to reset to default. opts: {min,max,def,value,onChange}. Returns {set,get}.
function rbAttachKnob(containerId, opts) {
    const el = document.getElementById(containerId);
    if (!el) return null;
    const min = opts.min, max = opts.max, def = opts.def;
    let val = (typeof opts.value === 'number') ? opts.value : def;
    const clamp = (v) => Math.max(min, Math.min(max, v));
    el.innerHTML =
        '<svg width="84" height="84" viewBox="0 0 64 64" style="cursor:ns-resize;touch-action:none">' +
        '<circle cx="32" cy="32" r="27" fill="#1a1a1c" stroke="#3f3f46" stroke-width="2"/>' +
        '<circle cx="32" cy="32" r="23" fill="#26262b"/>' +
        '<line class="rb-knob-ptr" x1="32" y1="32" x2="32" y2="11" stroke="#c4b5fd" stroke-width="3" stroke-linecap="round"/>' +
        '</svg>';
    const ptr = el.querySelector('.rb-knob-ptr');
    function render() {
        const t = (max > min) ? (val - min) / (max - min) : 0;
        const a = (-135 + t * 270) * Math.PI / 180;       // -135°(min) .. +135°(max), 0=up
        ptr.setAttribute('x2', (32 + Math.sin(a) * 21).toFixed(2));
        ptr.setAttribute('y2', (32 - Math.cos(a) * 21).toFixed(2));
    }
    let startY = 0, startVal = 0, dragging = false;
    const onMove = (e) => {
        if (!dragging) return;
        const y = (e.touches ? e.touches[0].clientY : e.clientY);
        val = clamp(startVal + (startY - y) / 150 * (max - min));
        render(); if (opts.onChange) opts.onChange(val);
        e.preventDefault();
    };
    const onUp = () => { dragging = false;
        window.removeEventListener('mousemove', onMove); window.removeEventListener('mouseup', onUp);
        window.removeEventListener('touchmove', onMove); window.removeEventListener('touchend', onUp); };
    const onDown = (e) => { dragging = true; startVal = val;
        startY = (e.touches ? e.touches[0].clientY : e.clientY);
        window.addEventListener('mousemove', onMove); window.addEventListener('mouseup', onUp);
        window.addEventListener('touchmove', onMove, { passive: false }); window.addEventListener('touchend', onUp);
        e.preventDefault(); };
    el.addEventListener('mousedown', onDown);
    el.addEventListener('touchstart', onDown, { passive: false });
    el.addEventListener('dblclick', () => { val = def; render(); if (opts.onChange) opts.onChange(val); });
    render();
    return { set(v) { val = clamp(v); render(); }, get() { return val; } };
}

// User cab/chain volume trim. Persists to /settings and applies LIVE via
// setGain('chain', base × trim) — the only gain the engine honours.
// Range 0–5, default 1× (the knob value IS the multiplier).
async function rbSetChainMakeup(v) {
    let n = parseFloat(v); if (!Number.isFinite(n)) n = 1.0;
    const val = Math.max(0, Math.min(5.0, n));
    window.__rbChainMakeup = val;
    const cmVal = document.getElementById('rb-chain-makeup-val');
    if (cmVal) cmVal.textContent = val.toFixed(2) + '×';
    // When a final leveler is in the chain, Chain Volume must be applied
    // AFTER it (the AGC cancels any pre-leveler gain). Drive the leveler's
    // Output Trim live; only fall back to the route/chain bus when no leveler
    // owns the output (e.g. mega-chain off / leveler disabled). On 0.3.0 the
    // bus fallback routes through the audio-effects host first.
    const db = val > 1.0e-4 ? 20 * Math.log10(val) : -24;
    let appliedPostLeveler = false;
    if (typeof RbMegaChain !== 'undefined' && RbMegaChain.isActive && RbMegaChain.isActive()) {
        try { appliedPostLeveler = await RbMegaChain.setOutputTrimDb(db); } catch (_) {}
    }
    const base = (typeof window.__rbChainBaseTarget === 'number') ? window.__rbChainBaseTarget : 1.0;
    if (!appliedPostLeveler && !(await rbSetRouteGainsWithHost({ chain: base * val }, 'chain-makeup'))) {
        const audio = window.slopsmithDesktop && window.slopsmithDesktop.audio;
        if (audio && typeof audio.setGain === 'function') audio.setGain('chain', base * val).catch(() => {});
    }
    fetch(`${window.RB_API}/settings`, {
        method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ chain_makeup: val }),
    }).catch(() => {});
}

// User "Amp drive" / input-chain trim — the pre-NAM input gain for GUITAR amps
// (bass auto-uses 1×). Range 0–5, default 1× (no boost). Persists to /settings
// (nam_chain_input_drive) and re-applies live through rbApplyChainInputDrive
// (which keeps the bass/guitar branch correct).
async function rbSetAmpDrive(v) {
    let n = parseFloat(v); if (!Number.isFinite(n)) n = 1.0;
    const val = Math.max(0, Math.min(5.0, n));
    window.__rbChainInputDrive = val;
    const el = document.getElementById('rb-amp-drive-val');
    if (el) el.textContent = val.toFixed(1) + '×';
    rbApplyChainInputDrive();   // re-applies respecting bass detection
    fetch(`${window.RB_API}/settings`, {
        method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ nam_chain_input_drive: val }),
    }).catch(() => {});
}

// Mute everything the engine can mute just long enough that the bundle's
// clearChain + loadPreset runs at silence, then restore with a short
// fade-in so the un-mute doesn't pop. Called from the fetch interceptor
// right before the bundle pulls the preset JSON, and from rbListenTone /
// rbReloadPreview / RbMegaChain right before clearChain.
//
// `targetGain` controls where the fade-in lands. If omitted, falls back
// to 1.0. Callers should compute it via rbChainGainTargetFor(chain) so
// the chain output is normalised regardless of whether the user has a
// cab IR active or not.
//
// Hold-time tuning: assumed worst case is "engine loads stages
// sequentially and only the last one is in place by the time loadPreset
// resolves". To cover that window with margin we use a more
// conservative 100 ms baseline + 50 ms/stage (chain of 5 → 350 ms).
// Override with `window.__rbMutePreLoadHold` if it feels too long.
let _rbMuteInFlight = false;
let _rbUnmuteRun = null;     // the pending unmute closure (event-driven trigger)
let _rbUnmuteTimer = null;   // safety-fallback timer handle
// Event-driven unmute: a caller that KNOWS the chain finished loading (the
// mega-chain build awaits loadPreset + getChainState + the VST-param re-apply)
// calls this so the un-mute fires on the REAL completion instead of a fixed
// timer. Fixes the "slow PCs still hear the load peaks" report — the old timer
// could fire mid-load and un-mute while NAMs/VSTs were still initialising.
async function rbSignalChainLoaded() {
    if (_rbUnmuteRun) { const f = _rbUnmuteRun; await f(); }
}
async function rbPreLoadMute(chainLen, targetGain, opts) {
    if (window.__rbMutePreLoad === false) return;
    const deferUnmute = !!(opts && opts.deferUnmute);
    const pendingTarget = rbClampChainGainTarget(targetGain);
    window.__rbPendingChainGainTarget = pendingTarget;
    if (_rbMuteInFlight) {
        // Already muted (e.g. the fetch-interceptor's SHORT timer-based mute
        // fired first, at song:loaded). If THIS caller (the mega-chain preload)
        // will signal real completion, UPGRADE the pending unmute to the long
        // safety net so that short timer can't fire mid-load and let the VST/NAM
        // load peaks through — the un-mute then comes from rbSignalChainLoaded()
        // (flag/event-driven, as intended) the moment the load truly finishes.
        if (deferUnmute && _rbUnmuteRun && _rbUnmuteTimer) {
            clearTimeout(_rbUnmuteTimer);
            _rbUnmuteTimer = setTimeout(_rbUnmuteRun, 15000);
        }
        return;                            // coalesce rapid tone changes
    }
    _rbMuteInFlight = true;
    const audio = window.slopsmithDesktop && window.slopsmithDesktop.audio;
    if (!audio) { _rbMuteInFlight = false; return; }
    const target = pendingTarget;   // was 4 — chains can need ~20×
    // Hold the chain muted until the WHOLE chain has loaded AND the post-load
    // VST-param / input-drive re-apply has settled — otherwise the un-mute
    // races the stage-by-stage NAM/VST init and the user hears the load peaks
    // ("se escucha cómo carga cada NAM y VST"). The old `100 + 50·stages` un-
    // muted at ~350 ms while the re-apply walk (`reapplyDelay`, computed in the
    // fetch interceptor) fires at ~400 ms, so its setParameter transients leaked
    // through. This generous estimate stays AHEAD of that re-apply + a settle
    // margin and scales with stage count (NAM loads dominate). Override:
    // `window.__rbMutePreLoadHold`.
    const hold = (typeof window.__rbMutePreLoadHold === 'number')
        ? Math.max(20, window.__rbMutePreLoadHold | 0)
        : 250 + 120 * Math.max(1, chainLen | 0);
    // When the caller will explicitly signal completion (deferUnmute — the
    // mega-chain path awaits the real load), the timer becomes a LONG safety net
    // (the unmute really happens via rbSignalChainLoaded the moment the load
    // resolves, however long that takes on a slow PC). Otherwise the timer IS
    // the unmute, sized to the load estimate.
    const fallbackMs = deferUnmute ? Math.max(hold, 10000) : hold;
    // During load we FULL-MUTE (monitor muted + chain gain 0): silence while the
    // chain loads, then the processed chain fades in. The old default left the
    // input monitor UN-muted ("clean guitar while it loads"), but that played the
    // RAW DI at full, un-leveled volume — the user heard the instrument blast on
    // load and then drop to the processed level ("escucho fuerte el instrumento y
    // después se baja"). Opt back into the dry-passthrough behaviour with
    // `window.__rbDryDuringLoad = true`.
    const dryDuringLoad = window.__rbDryDuringLoad === true;
    let wasMuted = false;
    try { if (typeof audio.isMonitorMuted === 'function') wasMuted = !!(await audio.isMonitorMuted()); } catch (_) {}
    try {
        // `chain` = post-NAM, pre-output. Setting to 0 silences the guitar
        // signal path (and the loading stages' peaks) without touching the
        // song's backing track.
        if (typeof audio.setGain === 'function') await audio.setGain('chain', 0);
        if (typeof audio.setMonitorMute === 'function')
            await audio.setMonitorMute(dryDuringLoad ? false : true);
    } catch (_) {}
    // The actual un-mute. Runs ONCE — whichever fires first wins: the explicit
    // rbSignalChainLoaded() (load really finished) or the safety-fallback timer.
    const doUnmute = async () => {
        if (_rbUnmuteRun !== doUnmute) return;     // already ran / superseded
        _rbUnmuteRun = null;
        if (_rbUnmuteTimer) { clearTimeout(_rbUnmuteTimer); _rbUnmuteTimer = null; }
        try {
            // Restore the monitor to whatever it was before the load (dry mode
            // forced it on; put it back so normal play isn't doubled).
            if (typeof audio.setMonitorMute === 'function') await audio.setMonitorMute(wasMuted);
            // Fade chain gain 0 → target over ~24 ms in 4 steps so the
            // restore doesn't click. Final value is the smart target,
            // not a fixed 1.0 — that's how we normalise across "amp +
            // cab" and "amp only" without a user-facing knob.
            if (typeof audio.setGain === 'function') {
                const restoreTarget = rbClampChainGainTarget(window.__rbPendingChainGainTarget ?? target);
                const steps = [restoreTarget * 0.25, restoreTarget * 0.5, restoreTarget * 0.8, restoreTarget];
                for (const v of steps) {
                    await audio.setGain('chain', v);
                    await new Promise(r => setTimeout(r, 6));
                }
            }
        } catch (_) {}
        _rbMuteInFlight = false;
    };
    _rbUnmuteRun = doUnmute;
    _rbUnmuteTimer = setTimeout(doUnmute, fallbackMs);
}

// NOTE: an earlier version of this file tried to monkey-patch
// `window.slopsmithDesktop.audio.loadPreset` to mute monitor + zero the
// chain gain during load. That approach is dead: Electron's
// contextBridge exposes `slopsmithDesktop.audio` as a frozen object —
// you can call its methods, but `api.loadPreset = function` silently
// no-ops, so the wrap was never actually installed. We confirmed this
// in DevTools (api.__rbWrapped stayed undefined). The transient-kill
// logic now lives in the fetch interceptor above (`rbPreLoadMute`),
// which calls setGain/setMonitorMute from outside the frozen object.

// ── AMP-toggle auto-apply ──────────────────────────────────────────────
// `nam_tone` only applies the chain (with our master pre/post) when AMP
// is on AT SONG LOAD TIME (line 1061 of nam_tone/screen.js gates the
// `_namApplyCurrentSongTone` call on `_namEnabled`). If the user loads a
// song with AMP off and turns it on mid-song, no chain is ever pushed —
// the workaround is "leave + re-enter the song". We can't patch the
// signed bundle, so we replicate the flow ourselves: watch the AMP
// button (`#btn-nam`), and on each OFF→ON edge, look up the song's
// active-tone mapping and call `loadPreset` ourselves with the master-
// wrapped chain. Kill-switch: `window.__rbAmpAutoApply = false`.
//
// The bundle's own `_namBuildGraph` will also run on the toggle — we
// wait ~1200 ms so its build settles first, and then our loadPreset is
// the *last* one to run, winning the chain state.
(function () {
    if (window.__rbAmpHookInstalled) return;
    window.__rbAmpHookInstalled = true;

    let lastEnabled = false;
    let inFlight = false;

    function isAmpEnabled() {
        const btn = document.getElementById('btn-nam');
        if (!btn) return false;
        // Bundle's `_namUpdateAmpButton` sets bg-green-700 when enabled.
        return /(?:^|\s)bg-green-/.test(btn.className);
    }

    function resolveActiveTone() {
        try {
            const hw = window.highway;
            if (!hw || typeof hw.getTime !== 'function') return null;
            const t = hw.getTime();
            const changes = hw.getToneChanges ? hw.getToneChanges() : [];
            const base = hw.getToneBase ? hw.getToneBase() : '';
            let active = base;
            if (Array.isArray(changes)) {
                for (const tc of changes) {
                    if (tc && tc.t <= t) active = tc.name;
                    else break;
                }
            }
            return (active && String(active).trim()) || null;
        } catch (_) { return null; }
    }

    function findMappingForTone(mappings, toneName) {
        if (!Array.isArray(mappings) || !mappings.length) return null;
        if (!toneName) return mappings[0];   // fallback
        const exact = mappings.find(m => m && m.tone_key === toneName);
        if (exact) return exact;
        const wanted = String(toneName).trim().toLowerCase();
        return mappings.find(m =>
            m && String(m.tone_key || '').trim().toLowerCase() === wanted
        ) || mappings[0];
    }

    async function autoApplyChain() {
        if (window.__rbAmpAutoApply === false) return;
        // When mega-chain mode owns the engine, we MUST NOT call
        // loadPreset here — it would clobber the pre-loaded whole-song
        // chain and leave only this single tone's stages loaded. The
        // mega-chain switcher will handle the AMP-on case itself.
        if (typeof RbMegaChain !== 'undefined' && RbMegaChain.isActive && RbMegaChain.isActive()) {
            console.log('[rig_builder] AMP auto-apply skipped — mega-chain owns the engine');
            return;
        }
        if (inFlight) return;
        inFlight = true;
        try {
            const filename = window.slopsmith
                && window.slopsmith.currentSong
                && window.slopsmith.currentSong.filename;
            if (!filename) return;
            const api = window.slopsmithDesktop && window.slopsmithDesktop.audio;
            if (!api || typeof api.loadPreset !== 'function') return;

            const r = await rbFetchLegacyNamToneMappings(filename);
            if (!r.ok) return;
            const mappings = await r.json();
            const tone = resolveActiveTone();
            const mapping = findMappingForTone(mappings, tone);
            if (!mapping) return;
            const presetId = mapping.preset_id ?? mapping.id;
            if (presetId == null) return;

            // Goes through our redirected fetch → master pre+post included.
            const fr = await fetch(`/api/plugins/rig_builder/native_preset_full/${presetId}`);
            if (!fr.ok) return;
            const full = await fr.json();
            const chain = full && full.native_preset && full.native_preset.chain;
            if (!Array.isArray(chain) || chain.length === 0) return;

            rbRecordLegacyNativeLoadBridge('amp auto-apply loaded chain through legacy Desktop audio API');
            // Goes through our patched loadPreset (mute + chain-gain 0)
            // so the AMP-on transient is suppressed just like a tone change.
            await api.loadPreset(JSON.stringify(full.native_preset));
            await rbReapplyVstParamsToChain(api, chain).catch((e) =>
                console.warn('[rig_builder] AMP auto-apply re-apply VST params:', e));
            console.log(`[rig_builder] AMP auto-apply: ${chain.length} stages for tone "${tone || '(base)'}"`
                + ` (master ${full.master_pre_count || 0}+${full.master_post_count || 0})`);
        } catch (e) {
            console.warn('[rig_builder] AMP auto-apply failed:', e);
        } finally {
            inFlight = false;
        }
    }

    function checkAmp() {
        const enabled = isAmpEnabled();
        if (enabled && !lastEnabled) {
            // OFF → ON edge. Wait for the bundle's own _namBuildGraph to
            // finish its load (it's ~600-900 ms with a multi-NAM chain on
            // an M1); ours runs after, so we land last and master wins.
            setTimeout(autoApplyChain, 1200);
        }
        lastEnabled = enabled;
    }

    // Poll every 500 ms — the AMP button is injected by the bundle when
    // a song loads, so it may not exist at page-init time.
    setInterval(checkAmp, 500);
})();

// ── Shared state ────────────────────────────────────────────────────

let rbState = {
    status: null,
    songTones: null,        // currently inspected song
    batchPoll: null,        // setInterval handle while batch is running
    currentTab: 'gear',        // default landing tab = Gear (user pref 2026-06)
    currentGearFilter: 'all',  // chip filter inside the Gear tab
    currentSongFile: null,  // filename of the song open in the per-song view
    listeningTone: null,    // toneIdx currently previewed, or null
    _previewMode: null,     // 'native' (full chain) | 'nam' (WASM fallback)
    _previewStartedAudio: false,
    _previewPayload: null,  // last native_preset_full payload (for bypass reloads)
    _auditionId: null,      // DOM id of the catalog/candidate ▶ button now playing
    knownVsts: [],          // list of installed VST3/AU plugins (synced from engine)
    _vstScanInProgress: false,
    _vstEditorSlot: null,   // engine slotId currently being edited (for Capture State)
};

// ── Effective-assignment readers ────────────────────────────────────
// A chain piece carries two layers: the persisted `assigned` (from the DB)
// and optional in-memory edits (`_uploaded_file`, `_vst_path`, …) staged in
// the editor before save. "Effective" = the staged edit if present, else the
// persisted value. These five helpers replace the same nullish-coalescing
// expression that was copy-pasted ~20× across the song editor, master chain
// and catalog — one place to read "what does this piece actually play?".
function rbEffFile(p)     { return p._uploaded_file || (p.assigned && p.assigned.file) || null; }
function rbEffKind(p)     { return p._uploaded_kind || (p.assigned && p.assigned.kind) || null; }
function rbEffVstPath(p)  { return p._vst_path || (p.assigned && p.assigned.vst_path) || ''; }
function rbEffVstFormat(p){ return p._vst_format || (p.assigned && p.assigned.vst_format) || 'VST3'; }
function rbEffVstState(p) { return p._vst_state ?? (p.assigned && p.assigned.vst_state) ?? null; }

const RB_API = '/api/plugins/rig_builder';
const RB_PLUGIN_ID = 'rig_builder';
const RB_EFFECTS_PARTICIPANT_ID = 'rig_builder.effects';
const RB_EFFECTS_ROUTE_KEY = 'desktop-main';
const RB_EFFECTS_PLAN_SCHEMA = 'slopsmith.audio_effects.chain_plan.v1';
const RB_JOBS_PROVIDER_ID = 'rig_builder.jobs';
const RB_PLAYBACK_OBSERVER_ID = 'rig_builder.playback-observer';

function rbSlopsmith() { return window.slopsmith || null; }
function rbCapabilitiesApi() { const s = rbSlopsmith(); return s && s.capabilities; }
function rbCandidateDomainsApi() { const s = rbSlopsmith(); return s && s.candidateDomains; }
function rbAudioEffectsApi() { const s = rbSlopsmith(); return s && s.audioEffects; }
function rbJobsApi() { const s = rbSlopsmith(); return s && s.jobs; }
function rbPlaybackApi() {
    const s = rbSlopsmith();
    return s && s.playback && s.playback.version === 1 ? s.playback : null;
}
function rbLibraryProvidersApi() {
    const s = rbSlopsmith();
    const api = s && s.libraryProviders;
    return api && typeof api === 'object' ? api : null;
}

let _rbLibraryProvidersLoaded = false;

function rbSafeCapabilityId(value, fallback) {
    return String(value == null ? '' : value)
        .replace(/[^A-Za-z0-9_.:-]+/g, '-')
        .replace(/^-+|-+$/g, '')
        .slice(0, 96) || fallback || 'unknown';
}

function rbAudioEffectsMappingApi() {
    const api = rbAudioEffectsApi();
    return api && typeof api.upsertMapping === 'function' ? api : null;
}

function rbPresetProviderRef(presetId) {
    return `preset:${presetId}`;
}

function rbPresetIdFromProviderRef(providerRef) {
    const value = String(providerRef || '').trim();
    if (!value) return '';
    const local = value.match(/^preset:(.+)$/);
    if (local) return local[1];
    const qualified = value.match(/^rig-builder:preset:(.+)$/);
    if (qualified) return qualified[1];
    return /^\d+$/.test(value) ? value : '';
}

function rbSongKeyForMapping(filename) {
    const key = String(window.__rbPlaybackSettingsKey || '').trim();
    const keyFilename = String(window.__rbPlaybackSettingsFilename || '').trim();
    if (key && (!keyFilename || keyFilename === filename)) return key;
    return filename || '';
}

async function rbUpsertAudioEffectsMapping({ filename, toneKey, presetId, label, source, active }) {
    const api = rbAudioEffectsMappingApi();
    if (!api || typeof api.upsertMapping !== 'function' || !presetId) return null;
    const mappingFilename = String(filename || '');
    return api.upsertMapping({
        song_key: rbSongKeyForMapping(mappingFilename),
        filename: mappingFilename,
        tone_key: String(toneKey || ''),
        provider_id: RB_EFFECTS_PARTICIPANT_ID,
        provider_ref: rbPresetProviderRef(presetId),
        label: String(label || ''),
        source: source || 'manual',
        active: active !== false,
        metadata: { legacy_preset_id: String(presetId) },
    });
}

function rbShortSafeText(value, fallback) {
    const text = String(value == null ? '' : value)
        .replace(/[\u0000-\u001f\u007f]+/g, ' ')
        .replace(/https?:\/\/[^\s?#]+[^\s]*/gi, '[url]')
        .replace(/file:\/\/[^\s]+/gi, '[path]')
        .replace(/\/Users\/[^\s]+/g, '[path]')
        .replace(/[A-Za-z]:\\[^\s]+/g, '[path]')
        .replace(/\b[^\s]+\.(psarc|sloppak|wav|flac|ogg|mp3|nam|json|db|sqlite)\b/gi, '[file]')
        .replace(/\b(token|secret|password|api[_-]?key)=([^\s&]+)/gi, '$1=[redacted]')
        .replace(/\s+/g, ' ')
        .trim();
    return (text || fallback || '').slice(0, 180);
}

function rbChainCapabilitySummary(chain, extra) {
    const stages = Array.isArray(chain) ? chain : [];
    const typeCounts = { vst: 0, nam: 0, ir: 0, other: 0 };
    let bypassedCount = 0;
    let masterPreCount = 0;
    let masterPostCount = 0;
    for (const stage of stages) {
        if (stage && stage.bypassed) bypassedCount += 1;
        if (stage && stage.type === 0) typeCounts.vst += 1;
        else if (stage && stage.type === 1) typeCounts.nam += 1;
        else if (stage && stage.type === 2) typeCounts.ir += 1;
        else typeCounts.other += 1;
        const slot = String((stage && stage.slot) || '');
        if (slot.indexOf('master_pre') === 0) masterPreCount += 1;
        if (slot.indexOf('master_post') === 0) masterPostCount += 1;
    }
    return {
        routeKey: RB_EFFECTS_ROUTE_KEY,
        provider: 'rig-builder',
        processorCount: stages.length,
        bypassedCount,
        activeCount: Math.max(0, stages.length - bypassedCount),
        typeCounts,
        masterPreCount,
        masterPostCount,
        hasNativeChain: stages.length > 0,
        allBypassed: stages.length > 0 && bypassedCount === stages.length,
        ...(extra || {}),
    };
}

function rbNativeStageKind(stage) {
    const type = Number(stage && stage.type);
    if (type === 0) return 'vst';
    if (type === 1) return 'nam';
    if (type === 2) return 'ir';
    return 'utility';
}

function rbStageRole(stage, kind) {
    const slot = String((stage && stage.slot) || '').toLowerCase();
    if (slot.indexOf('master_pre') === 0) return 'master-pre';
    if (slot.indexOf('master_post') === 0) return 'master-post';
    if (slot === 'pre_pedal') return 'pre-pedal';
    if (slot === 'post_pedal') return 'post-pedal';
    if (slot === 'amp') return 'amp';
    if (slot === 'rack') return 'rack';
    if (slot === 'cabinet' || kind === 'ir') return 'cab';
    if (kind === 'vst') return 'utility';
    return 'unknown';
}

function rbAudioEffectsPlanId(prefix, ref) {
    return rbSafeCapabilityId(`rig-builder-${prefix}-${ref || Date.now()}`, 'rig-builder-plan');
}

function rbToneSegmentId(tone, index) {
    const key = tone && (tone.tone_key || tone.toneKey || tone.name || tone.id);
    return rbSafeCapabilityId(`tone-${key || index}`, `tone-${index}`);
}

function rbBuildAudioEffectsRequestFromPayload(payload, options) {
    const opts = options || {};
    const nativePreset = payload && payload.native_preset || {};
    const chain = Array.isArray(nativePreset.chain) ? nativePreset.chain : [];
    const planId = rbAudioEffectsPlanId(opts.mode || 'chain', opts.ref || (payload && payload.id));
    const stages = [];
    const assets = {};

    chain.forEach((stage, index) => {
        const kind = rbNativeStageKind(stage);
        const role = rbStageRole(stage, kind);
        const stageId = rbSafeCapabilityId(`${planId}-stage-${index}`, `stage-${index}`);
        const assetRef = rbSafeCapabilityId(`${planId}-asset-${index}`, `asset-${index}`);
        const stateRef = stage && stage.state ? rbSafeCapabilityId(`${planId}-state-${index}`, `state-${index}`) : '';
        const planStage = {
            stageId,
            kind,
            role,
            assetRef,
            bypassed: !!(stage && stage.bypassed),
            summary: { role, kind },
        };
        if (stateRef) planStage.stateRef = stateRef;
        stages.push(planStage);
        const asset = {
            kind,
            path: stage && stage.path,
            safeName: `${role}-${kind}`,
        };
        if (stage && stage.state) asset.stateBase64 = stage.state;
        assets[assetRef] = asset;
    });

    const stageIds = stages.map(stage => stage.stageId);
    let segments = [{ segmentId: 'base', stageIds, stageBypass: Object.fromEntries(stages.map(stage => [stage.stageId, !!stage.bypassed])) }];
    if (payload && Array.isArray(payload.tones) && payload.tones.length) {
        const indexToStageId = new Map(stages.map((stage, index) => [index, stage.stageId]));
        const masterEntries = [];
        for (const entry of payload.master_pre_slots || []) if (entry && typeof entry.idx === 'number') masterEntries.push(entry);
        for (const entry of payload.master_post_slots || []) if (entry && typeof entry.idx === 'number') masterEntries.push(entry);
        segments = payload.tones.slice(0, 80).map((tone, toneIndex) => {
            const entriesByIdx = new Map();
            for (const entry of masterEntries) entriesByIdx.set(entry.idx, entry);
            for (const entry of tone && tone.slots || []) {
                if (entry && typeof entry.idx === 'number') entriesByIdx.set(entry.idx, entry);
            }
            const orderedIdxs = Array.from(entriesByIdx.keys()).sort((a, b) => a - b);
            const segmentStageIds = orderedIdxs.map(idx => indexToStageId.get(idx)).filter(Boolean);
            const stageBypass = {};
            for (const idx of orderedIdxs) {
                const stageId = indexToStageId.get(idx);
                if (stageId) stageBypass[stageId] = !!(entriesByIdx.get(idx) && entriesByIdx.get(idx).bypassed);
            }
            return {
                segmentId: rbToneSegmentId(tone, toneIndex),
                stageIds: segmentStageIds,
                stageBypass,
            };
        });
    }

    const plan = {
        schema: RB_EFFECTS_PLAN_SCHEMA,
        planId,
        routeKey: RB_EFFECTS_ROUTE_KEY,
        providerId: RB_EFFECTS_PARTICIPANT_ID,
        stages,
        segments,
        summary: rbChainCapabilitySummary(chain, {
            mode: rbSafeCapabilityId(opts.mode || 'native', 'native'),
            segmentCount: segments.length,
            missingCount: Array.isArray(payload && payload.missing) ? payload.missing.length : 0,
        }),
    };
    return { plan, assets, chain };
}

async function rbFetchAudioEffectsPayloadForRequest(request) {
    const target = request && request.target && typeof request.target === 'object' ? request.target : {};
    const planRequest = request && request.planRequest && typeof request.planRequest === 'object' ? request.planRequest : {};
    const providerRef = target.providerRef || target.provider_ref || request && (request.providerRef || request.provider_ref) || planRequest.providerRef || planRequest.provider_ref || '';
    const presetId = target.presetId || target.preset_id || request && request.presetId || planRequest.presetId || rbPresetIdFromProviderRef(providerRef);
    if (providerRef && !presetId) {
        return { outcome: 'no-target', status: 'invalid-provider-ref', reason: 'Rig Builder audio-effects request had an invalid provider ref' };
    }
    if (presetId != null && presetId !== '') {
        const resp = await fetch(`${RB_API}/native_preset_full/${encodeURIComponent(presetId)}`);
        if (!resp.ok) return { outcome: 'no-target', status: 'preset-unavailable', reason: `Rig Builder preset plan unavailable: HTTP ${resp.status}` };
        let payload;
        try { payload = await resp.json(); }
        catch (_) { return { outcome: 'failed', status: 'invalid-response', reason: 'Rig Builder preset plan response was not valid JSON' }; }
        return { outcome: 'handled', payload, mode: 'full-chain', ref: presetId };
    }
    const filename = target.filename || request && request.filename || planRequest.filename || rbState.currentSongFile;
    if (filename) {
        const resp = await fetch(`${RB_API}/mega_chain/${encodeURIComponent(filename)}`);
        if (!resp.ok) return { outcome: 'no-target', status: 'song-unavailable', reason: `Rig Builder song chain unavailable: HTTP ${resp.status}` };
        let payload;
        try { payload = await resp.json(); }
        catch (_) { return { outcome: 'failed', status: 'invalid-response', reason: 'Rig Builder song chain response was not valid JSON' }; }
        return { outcome: 'handled', payload, mode: 'mega-chain', ref: 'song' };
    }
    return { outcome: 'no-target', status: 'no-target', reason: 'No preset or song target is available for Rig Builder audio-effects resolution' };
}

function rbHandledAudioEffectsResult(result, fallbackReason) {
    if (!result || result.outcome !== 'handled') {
        const reason = result && result.reason ? result.reason : fallbackReason;
        throw new Error(reason || 'audio-effects chain load failed');
    }
    const payload = result.payload || {};
    return { success: true, slotsLoaded: payload.slotsLoaded, audioEffects: result };
}

async function rbLoadChainPlanWithHost(payload, options) {
    rbRegisterAudioEffectsCapability();
    const audioEffects = rbAudioEffectsApi();
    if (!audioEffects || typeof audioEffects.loadPlan !== 'function') return null;
    const opts = options || {};
    const targetPresetId = opts.presetId || (payload && payload.id);
    const targetFilename = opts.filename || (opts.mode === 'mega-chain' ? rbState.currentSongFile : '');
    if (!targetPresetId && !targetFilename) return null;
    const result = await audioEffects.loadPlan({
        authorization: opts.authorization || 'user-action',
        routeKey: RB_EFFECTS_ROUTE_KEY,
        providerId: RB_EFFECTS_PARTICIPANT_ID,
        target: {
            presetId: targetPresetId,
            providerRef: targetPresetId ? rbPresetProviderRef(targetPresetId) : '',
            filename: targetFilename,
            source: 'rig-builder-ui',
        },
        planRequest: {
            mode: opts.mode || 'native',
            ref: opts.ref || payload && payload.id || '',
        },
        options: opts.executorOptions || {},
    }, RB_PLUGIN_ID);
    return rbHandledAudioEffectsResult(result, 'Rig Builder audio-effects host load failed');
}

async function rbReleaseAudioEffectsRouteWithHost(reason) {
    rbRegisterAudioEffectsCapability();
    const audioEffects = rbAudioEffectsApi();
    if (!audioEffects || typeof audioEffects.releaseRoute !== 'function') return false;
    try {
        const result = await audioEffects.releaseRoute({
            routeKey: RB_EFFECTS_ROUTE_KEY,
            authorization: 'playback-session',
            summary: { reason: reason || 'rig-builder-teardown' },
        });
        if (result && result.outcome === 'handled') return true;
        if (result && result.outcome !== 'no-target' && result.outcome !== 'no-handler') {
            console.warn('[rig_builder] audio-effects route release was not handled:', result);
        }
    } catch (e) {
        console.warn('[rig_builder] audio-effects route release failed:', e);
    }
    return false;
}

async function rbActivateSegmentWithHost(segmentId, toneKey) {
    rbRegisterAudioEffectsCapability();
    const audioEffects = rbAudioEffectsApi();
    if (!segmentId || !audioEffects || typeof audioEffects.activateSegment !== 'function') return false;
    try {
        const result = await audioEffects.activateSegment({
            routeKey: RB_EFFECTS_ROUTE_KEY,
            segmentId,
            toneKey: toneKey || segmentId,
        });
        if (result && result.outcome === 'handled') return true;
        console.warn('[rig_builder mega-chain] audio-effects segment activation was not handled:', result);
    } catch (e) {
        console.warn('[rig_builder mega-chain] audio-effects segment activation failed:', e);
    }
    return false;
}

async function rbLoadNativePresetPayload(api, payload, options) {
    const ready = await rbEnsureNativeAudioReady(api, options);
    if (!ready.ok) throw new Error(ready.reason || 'Native audio input is not ready');

    let audioEffectsError = null;
    try {
        const result = await rbLoadChainPlanWithHost(payload, options);
        if (result) return { result, viaAudioEffects: true };
    } catch (e) {
        audioEffectsError = e;
        console.warn('[rig_builder] audio-effects executor load failed; using legacy loadPreset:', e);
    }
    const chain = payload && payload.native_preset && Array.isArray(payload.native_preset.chain) ? payload.native_preset.chain : [];
    await rbPreLoadMute(chain.length, rbChainGainTargetFor(chain)).catch(() => {});
    if (api.clearChain) await api.clearChain().catch(() => {});
    const result = await api.loadPreset(JSON.stringify(payload.native_preset));
    if (!result || result.success === false) {
        throw new Error((result && result.error) || (audioEffectsError && audioEffectsError.message) || 'loadPreset failed');
    }
    return { result, viaAudioEffects: false };
}

function rbAudioEffectsOperationHandlers() {
    return {
        'chain.resolve': async (request = {}) => {
            try {
                const fetched = await rbFetchAudioEffectsPayloadForRequest(request);
                if (!fetched || fetched.outcome !== 'handled') return fetched;
                const resolved = rbBuildAudioEffectsRequestFromPayload(fetched.payload, { mode: fetched.mode, ref: fetched.ref });
                return {
                    outcome: 'handled',
                    status: 'resolved',
                    plan: resolved.plan,
                    assets: resolved.assets,
                    summary: resolved.plan.summary,
                };
            } catch (e) {
                const reason = e && e.message ? e.message : 'Rig Builder chain resolution failed';
                rbRecordAudioEffectsBridge(reason);
                return { outcome: 'failed', status: 'resolve-failed', reason: rbShortSafeText(reason, 'Rig Builder chain resolution failed') };
            }
        },
        'chain.inspect': () => ({
            outcome: 'handled',
            status: 'available',
            payload: {
                providerId: RB_EFFECTS_PARTICIPANT_ID,
                routeKey: RB_EFFECTS_ROUTE_KEY,
                currentSong: rbState.currentSongFile ? 'available' : 'none',
                preview: rbState.listeningTone !== null ? 'active' : 'idle',
            },
        }),
        'segment.activate': () => ({ outcome: 'handled', status: 'host-executed' }),
        'stage.set-bypass': () => ({ outcome: 'handled', status: 'host-executed' }),
        'stage.set-parameter': () => ({ outcome: 'handled', status: 'host-executed' }),
        fallback: () => ({ outcome: 'handled', status: 'fallback-ready', payload: { providerId: RB_EFFECTS_PARTICIPANT_ID } }),
    };
}

function rbSelectAudioEffectsRoute(reason) {
    rbRegisterAudioEffectsCapability();
    const audioEffects = rbAudioEffectsApi();
    if (!audioEffects || typeof audioEffects.selectChain !== 'function') return null;
    try {
        return audioEffects.selectChain({
            authorization: 'restore-selection',
            routeKey: RB_EFFECTS_ROUTE_KEY,
            providerId: RB_EFFECTS_PARTICIPANT_ID,
            summary: { mode: 'mega-chain', status: reason || 'pending' },
        }, RB_PLUGIN_ID);
    } catch (e) {
        console.warn('[rig_builder] audio-effects route selection failed:', e);
        return null;
    }
}

function rbCapabilityCommand(capability, command, payload) {
    const api = rbCapabilitiesApi();
    if (!api || typeof api.command !== 'function') return Promise.resolve(null);
    try {
        return Promise.resolve(api.command(capability, command, {
            requester: RB_PLUGIN_ID,
            payload: payload || {},
        })).catch((e) => {
            console.warn(`[rig_builder] ${capability}.${command} failed:`, e);
            return null;
        });
    } catch (e) {
        console.warn(`[rig_builder] ${capability}.${command} failed:`, e);
        return Promise.resolve(null);
    }
}

function rbRegisterUiCapabilities() {
    const nav = rbSlopsmith() && rbSlopsmith().uiNavigation;
    if (!nav || typeof nav.registerScreen !== 'function') return;
    nav.registerScreen({
        pluginId: RB_PLUGIN_ID,
        screenId: 'plugin-rig_builder',
        label: 'Rig Builder',
        lifecyclePolicy: 'mounted-hidden',
        compatibilityMode: 'native',
        logicalKey: 'rig_builder:screen',
        fallbackScreenId: 'home',
    });
    if (typeof nav.registerEntry === 'function') {
        nav.registerEntry({
            pluginId: RB_PLUGIN_ID,
            entryId: 'rig-builder-nav',
            targetScreenId: 'plugin-rig_builder',
            label: 'Rig Builder',
            region: 'primary-nav.plugins',
            lifecyclePolicy: 'mounted-hidden',
            compatibilityMode: 'native',
            logicalKey: 'rig_builder:navigation',
            fallbackScreenId: 'home',
        });
    }
}

function rbRegisterAudioEffectsCapability() {
    const audioEffects = rbAudioEffectsApi();
    if (audioEffects && typeof audioEffects.registerProvider === 'function') {
        audioEffects.registerProvider({
            providerId: RB_EFFECTS_PARTICIPANT_ID,
            ownerPluginId: RB_PLUGIN_ID,
            label: 'Rig Builder full chains',
            priority: 40,
            routeKey: RB_EFFECTS_ROUTE_KEY,
            routeKeys: [RB_EFFECTS_ROUTE_KEY],
            kind: 'full-chain',
            status: 'available',
            availability: 'available',
            sourceMode: 'native',
            requests: ['select-chain', 'bypass', 'restore', 'fallback', 'inspect-route', 'upsert-mapping'],
            operations: ['chain.resolve', 'chain.inspect', 'segment.activate', 'stage.set-bypass', 'stage.set-parameter', 'fallback'],
            operationHandlers: rbAudioEffectsOperationHandlers(),
            dependencies: {
                audioEngine: rbNativeAudio() ? 'available' : 'degraded',
                namTone: 'available',
            },
            summary: { routeKey: RB_EFFECTS_ROUTE_KEY, provider: 'rig-builder', stageKinds: ['nam', 'vst', 'ir'] },
            version: 1,
        });
    }
    const candidates = rbCandidateDomainsApi();
    if (!candidates || typeof candidates.registerParticipant !== 'function') return;
    candidates.registerParticipant({
        domain: 'audio-effects',
        pluginId: RB_PLUGIN_ID,
        participantId: RB_EFFECTS_PARTICIPANT_ID,
        role: 'provider',
        roles: ['provider', 'requester', 'observer'],
        label: 'Rig Builder effects chains',
        availability: 'available',
        sourceMode: 'native',
        logicalKey: 'rig-builder-effects',
        routeKey: RB_EFFECTS_ROUTE_KEY,
        priority: 40,
        capabilities: ['select-chain', 'bypass', 'restore', 'fallback', 'inspect-route', 'chain.resolve', 'chain.inspect', 'segment.activate', 'stage.set-bypass', 'stage.set-parameter'],
        dependencies: {
            audioEngine: rbNativeAudio() ? 'available' : 'degraded',
            namTone: 'available',
        },
        summary: { routeKey: RB_EFFECTS_ROUTE_KEY, provider: 'rig-builder' },
    });
}

function rbRegisterJobsCapability() {
    const jobs = rbJobsApi();
    if (!jobs || typeof jobs.registerProvider !== 'function') return;
    jobs.registerProvider({
        providerId: RB_JOBS_PROVIDER_ID,
        pluginId: RB_PLUGIN_ID,
        label: 'Rig Builder jobs',
        jobTypes: [
            'rig-builder.batch-map',
            'rig-builder.curated-preload',
            'rig-builder.download-capture',
            'rig-builder.auto-download-song',
            'rig-builder.export-defaults',
            'rig-builder.purge-library',
        ],
        actions: ['enqueue', 'inspect', 'cancel'],
        availability: 'available',
        capacity: { maxRunning: 2, maxQueued: 20 },
        recoverySupport: { queued: false, running: false, paused: false },
        operationHandlers: {
            'job.enqueue': () => ({ progress: { mode: 'indeterminate', step: 'delegated', message: 'Rig Builder backend route is running' } }),
            'job.status': () => ({ outcome: 'handled', status: 'delegated' }),
            'job.cancel': () => ({ outcome: 'unsupported-operation', status: 'unsupported' }),
        },
    });
}

function rbRegisterLibraryCapability() {
    const caps = rbCapabilitiesApi();
    if (!caps || typeof caps.registerParticipant !== 'function') return;
    caps.registerParticipant(RB_PLUGIN_ID, {
        library: {
            roles: ['requester', 'observer'],
            requests: ['list-providers', 'refresh-providers', 'get-current', 'select-provider', 'sync-song', 'inspect'],
            observes: ['providers-refreshed', 'source-changed', 'song-sync-started', 'song-sync-succeeded', 'song-sync-failed'],
            mode: 'active',
            compatibility: 'none',
            ownership: 'requester-only',
            safety: 'safe',
            version: 1,
            runtime: true,
            description: 'Uses Slopsmith library providers for Rig Builder song search and remote song sync.',
        },
    });
}

function rbRegisterPlaybackCapability() {
    const caps = rbCapabilitiesApi();
    if (caps && typeof caps.registerParticipant === 'function') {
        caps.registerParticipant(RB_PLUGIN_ID, {
            playback: {
                roles: ['observer'],
                kind: 'lifecycle',
                observes: ['ready', 'stopped', 'ended'],
                mode: 'active',
                compatibility: 'shim-allowed',
                ownership: 'observer-only',
                safety: 'safe',
                version: 1,
                runtime: true,
                description: 'Observes playback readiness for Rig Builder full-chain and mega-chain routing.',
            },
        });
    }
    const playback = rbPlaybackApi();
    if (playback && typeof playback.registerObserver === 'function') {
        playback.registerObserver({
            observerId: RB_PLAYBACK_OBSERVER_ID,
            kind: 'plugin',
            observes: ['ready', 'stopped', 'ended'],
            status: 'available',
        });
    }
}

function rbRegisterPrivilegedCapabilities() {
    const participants = [
        { participantId: 'rig_builder.backend_routes', surface: 'backend-route', roles: ['provider', 'observer'], operationClasses: ['route.inspect', 'route.mutate', 'status'], riskClasses: ['local-data', 'external-service', 'subprocess'], label: 'Rig Builder backend routes', confirmationRequirement: 'not-required-for-inspection', compatibilityMode: 'native' },
        { participantId: 'rig_builder.tone3000_service', surface: 'external-service', roles: ['provider', 'observer'], operationClasses: ['service.request', 'service.download', 'status'], riskClasses: ['external-service'], label: 'tone3000 search and download', confirmationRequirement: 'required', compatibilityMode: 'native' },
        { participantId: 'rig_builder.media_import_export', surface: 'media-import-export', roles: ['provider', 'observer'], operationClasses: ['media.import', 'media.export', 'status'], riskClasses: ['local-data', 'subprocess'], label: 'Gear capture and IR import/export', confirmationRequirement: 'required', compatibilityMode: 'native' },
    ];
    participants.forEach(participant => {
        void rbCapabilityCommand('privileged-capabilities', 'register-participant', {
            participant: { ...participant, pluginId: RB_PLUGIN_ID },
        });
    });
}

function rbRegisterCapabilities() {
    try {
        const caps = rbCapabilitiesApi();
        if (caps && typeof caps.registerParticipant === 'function') {
            caps.registerParticipant(RB_PLUGIN_ID, {
                'audio-effects': { roles: ['provider', 'requester', 'observer'], commands: ['select-chain', 'bypass', 'restore', 'fallback', 'inspect-route'], requests: ['select-chain', 'bypass', 'restore', 'fallback', 'inspect-route', 'upsert-mapping'], operations: ['chain.resolve', 'chain.inspect', 'segment.activate', 'stage.set-bypass', 'stage.set-parameter', 'fallback'], safety: 'sensitive', compatibility: 'shim-allowed', ownership: 'multi-provider', version: 1, runtime: true },
                playback: { roles: ['observer'], kind: 'lifecycle', observes: ['ready', 'stopped', 'ended'], safety: 'safe', compatibility: 'shim-allowed', ownership: 'observer-only', version: 1, runtime: true },
                jobs: { roles: ['provider', 'observer'], operations: ['job.enqueue', 'job.status', 'job.cancel'], safety: 'privileged', compatibility: 'shim-allowed', ownership: 'multi-provider', version: 1, runtime: true },
                'privileged-capabilities': { roles: ['provider', 'requester', 'observer'], requests: ['inspect', 'check-approval-boundary', 'record-outcome', 'record-bridge-hit', 'link-job'], safety: 'privileged', compatibility: 'shim-allowed', ownership: 'privileged', version: 1, runtime: true },
            });
        }
        rbRegisterUiCapabilities();
        rbRegisterLibraryCapability();
        rbRegisterPlaybackCapability();
        rbRegisterAudioEffectsCapability();
        rbRegisterJobsCapability();
        rbRegisterPrivilegedCapabilities();
    } catch (e) {
        console.warn('[rig_builder] capability registration failed:', e);
    }
}

function rbEnsureCapabilitiesRegistered(attempt) {
    const n = Number(attempt || 0);
    rbRegisterCapabilities();
    const audioEffects = rbAudioEffectsApi();
    if (audioEffects && typeof audioEffects.registerProvider === 'function') return;
    if (n < 20) setTimeout(() => rbEnsureCapabilitiesRegistered(n + 1), 250);
}

rbEnsureCapabilitiesRegistered(0);

function rbRecordAudioEffectsBridge(reason) {
    const candidates = rbCandidateDomainsApi();
    if (!candidates || typeof candidates.recordBridgeHit !== 'function') return;
    candidates.recordBridgeHit({
        domain: 'audio-effects',
        bridgeId: 'audio-effects.legacy-nam-routing',
        pluginId: RB_PLUGIN_ID,
        legacySurface: 'nam_tone.native-preset-fetch',
        status: 'used',
        reason: rbShortSafeText(reason || 'Rig Builder chain routing bridge used'),
    });
}

function rbRecordLegacyToneDbBridge(reason, status) {
    const api = rbAudioEffectsApi();
    const recorder = api && typeof api.recordBridgeHit === 'function' ? api : rbCandidateDomainsApi();
    if (!recorder || typeof recorder.recordBridgeHit !== 'function') return;
    recorder.recordBridgeHit({
        domain: 'audio-effects',
        routeKey: RB_EFFECTS_ROUTE_KEY,
        bridgeId: 'audio-effects.legacy-tone-db',
        pluginId: RB_PLUGIN_ID,
        legacySurface: 'nam_tone.db tone_mappings',
        status: status || 'used',
        reason: rbShortSafeText(reason || 'Rig Builder legacy tone mapping database used'),
    });
}

function rbRecordLegacyNativeLoadBridge(reason, status) {
    const api = rbAudioEffectsApi();
    const recorder = api && typeof api.recordBridgeHit === 'function' ? api : rbCandidateDomainsApi();
    if (!recorder || typeof recorder.recordBridgeHit !== 'function') return;
    recorder.recordBridgeHit({
        domain: 'audio-effects',
        routeKey: RB_EFFECTS_ROUTE_KEY,
        bridgeId: 'audio-effects.legacy-native-load',
        pluginId: RB_PLUGIN_ID,
        legacySurface: 'window.slopsmithDesktop.audio.loadPreset',
        status: status || 'used',
        reason: rbShortSafeText(reason || 'Rig Builder direct Desktop loadPreset used'),
    });
}

async function rbFetchLegacyNamToneMappings(filename) {
    rbRecordLegacyToneDbBridge('read legacy nam_tone tone_mappings for amp auto-apply');
    return fetch(`/api/plugins/nam_tone/mappings/${encodeURIComponent(filename)}?owner=rig_builder`);
}

async function rbSyncAudioEffectsCapability(reason, options) {
    try {
        rbRegisterAudioEffectsCapability();
        const candidates = rbCandidateDomainsApi();
        if (!candidates) return;
        const chainSummary = rbChainCapabilitySummary(options && options.chain, {
            reason: rbSafeCapabilityId(reason || 'chain-updated', 'chain-updated'),
            mode: rbSafeCapabilityId(options && options.mode, 'native'),
            fallback: !!(options && options.fallback),
        });
        const payload = {
            providerId: RB_EFFECTS_PARTICIPANT_ID,
            routeKey: RB_EFFECTS_ROUTE_KEY,
            chainSummary,
            dependencies: {
                audioEngine: rbNativeAudio() ? 'available' : 'degraded',
                namTone: 'available',
            },
        };
        if (options && options.userAction && typeof candidates.dispatch === 'function') {
            await candidates.dispatch({
                domain: 'audio-effects',
                command: 'select-chain',
                requester: RB_PLUGIN_ID,
                payload: { ...payload, authorization: 'user-action' },
            });
        } else if (typeof candidates.recordOutcome === 'function') {
            candidates.recordOutcome('audio-effects', {
                operation: rbSafeCapabilityId(reason || 'chain-updated', 'chain-updated'),
                status: options && options.fallback ? 'degraded' : 'handled',
                participantId: RB_EFFECTS_PARTICIPANT_ID,
                pluginId: RB_PLUGIN_ID,
                details: { chainSummary },
            });
        }
        if (options && options.bridge) rbRecordAudioEffectsBridge(reason);
    } catch (e) {
        console.warn('[rig_builder] audio-effects capability sync failed:', e);
    }
}

async function rbStartCapabilityJob(jobType, safeLabel, options) {
    rbRegisterJobsCapability();
    const type = rbSafeCapabilityId(jobType, 'rig-builder.job');
    const jobId = rbSafeCapabilityId((options && options.jobId) || `${type}-${Date.now()}`, type);
    const logicalJobKey = rbSafeCapabilityId((options && options.logicalJobKey) || jobId, jobId);
    const result = await rbCapabilityCommand('jobs', 'enqueue', {
        providerId: RB_JOBS_PROVIDER_ID,
        jobId,
        logicalJobKey,
        jobType: type,
        requester: RB_PLUGIN_ID,
        authorization: options && options.background ? 'background' : 'user-action',
        priority: options && options.background ? 'background-maintenance' : 'user-approved-interactive',
        safeLabel: rbShortSafeText(safeLabel, type),
        target: { kind: rbSafeCapabilityId(options && options.targetKind, type), safeRef: rbSafeCapabilityId(options && options.targetRef, logicalJobKey) },
        inputs: { safeFingerprint: rbSafeCapabilityId(options && options.fingerprint, logicalJobKey) },
    });
    const job = result && result.payload && result.payload.job;
    if (job && job.jobId) return job.jobId;
    rbRecordJobsBridgeHit('jobs.legacy-plugin-queue', type, logicalJobKey, 'Capability job enqueue did not take ownership');
    return null;
}

function rbRecordJobsBridgeHit(bridgeId, operation, logicalJobKey, reason) {
    void rbCapabilityCommand('jobs', 'record-bridge-hit', {
        bridgeId: bridgeId || 'jobs.legacy-backend-route',
        legacySurface: 'rig_builder.backend-route',
        pluginId: RB_PLUGIN_ID,
        providerId: RB_JOBS_PROVIDER_ID,
        operation: rbSafeCapabilityId(operation || 'job', 'job'),
        logicalJobKey: rbSafeCapabilityId(logicalJobKey || operation || 'job', 'job'),
        safeReason: rbShortSafeText(reason || 'Rig Builder legacy route delegated work during capability migration'),
    });
}

function rbUpdateCapabilityJob(jobId, progress) {
    const jobs = rbJobsApi();
    if (!jobId || !jobs || typeof jobs.updateProgress !== 'function') return;
    jobs.updateProgress(RB_JOBS_PROVIDER_ID, jobId, progress || {});
}

function rbFinishCapabilityJob(jobId, ok, summary, category) {
    const jobs = rbJobsApi();
    if (!jobId || !jobs) return;
    if (ok && typeof jobs.complete === 'function') {
        jobs.complete(RB_JOBS_PROVIDER_ID, jobId, { resultSummary: rbShortSafeText(summary || 'Completed', 'Completed') });
    } else if (!ok && typeof jobs.fail === 'function') {
        jobs.fail(RB_JOBS_PROVIDER_ID, jobId, { category: rbSafeCapabilityId(category || 'provider-failure', 'provider-failure'), safeReason: rbShortSafeText(summary || 'Failed', 'Failed'), retryable: false });
    }
}

function rbRecordPrivilegedOutcome(operation, status, reason) {
    void rbCapabilityCommand('privileged-capabilities', 'record-outcome', {
        operation: rbSafeCapabilityId(operation || 'operation', 'operation'),
        status: rbSafeCapabilityId(status || 'handled', 'handled'),
        safeReason: rbShortSafeText(reason || ''),
    });
}

function rbPlaybackTargetFromDetail(detail) {
    const payload = detail && typeof detail === 'object' ? detail : {};
    const target = payload.target && typeof payload.target === 'object' ? payload.target : {};
    const current = window.slopsmith && window.slopsmith.currentSong || {};
    const filename = payload.filename || target.filename || current.filename || window._currentSongFile || rbState.currentSongFile || '';
    return {
        filename: String(filename || ''),
        settingsKey: String(target.settingsKey || payload.settingsKey || ''),
        targetId: String(target.targetId || payload.targetId || ''),
        sourceKind: String(target.sourceKind || payload.sourceKind || ''),
    };
}

function rbLibraryProviderSnapshot() {
    const api = rbLibraryProvidersApi();
    if (api && typeof api.snapshot === 'function') return api.snapshot();
    return { current: 'local', providers: [{ id: 'local', label: 'My Library', kind: 'local', capabilities: ['library.read'], default: true }] };
}

async function rbLibraryCapabilityCommand(command, payload, target) {
    const caps = rbCapabilitiesApi();
    if (!caps || typeof caps.command !== 'function') return null;
    try {
        return await caps.command('library', command, {
            requester: RB_PLUGIN_ID,
            target: target || {},
            payload: payload || {},
        });
    } catch (e) {
        console.warn(`[rig_builder] library.${command} failed:`, e);
        return null;
    }
}

function rbLibraryPayload(result) {
    return result && result.payload && typeof result.payload === 'object' ? result.payload : null;
}

function rbLibraryProviderById(providerId) {
    const api = rbLibraryProvidersApi();
    if (api && typeof api.providerById === 'function') return api.providerById(providerId);
    return (rbLibraryProviderSnapshot().providers || []).find(provider => provider.id === providerId) || null;
}

function rbLibraryProviderIdForSong(song, fallbackProviderId) {
    return String(
        (song && (song.provider_id || song.providerId || song.library_provider_id ||
        song.libraryProviderId || song.provider)) || fallbackProviderId || 'local'
    );
}

function rbLibrarySongId(song) {
    return String((song && (song.song_id || song.songId || song.remote_id || song.remoteId || song.id || song.filename)) || '');
}

function rbLibraryLocalFilename(song, providerId) {
    if (!song) return '';
    if (rbIsLocalLibraryProvider(providerId)) return song.filename ? String(song.filename) : '';
    return String(song.local_filename || song.localFilename || song.synced_filename ||
        song.syncedFilename || song.play_filename || song.playFilename || '');
}

function rbLibraryDisplayFilename(song, providerId) {
    return rbLibraryLocalFilename(song, providerId) || rbLibrarySongId(song) || 'Unknown song';
}

function rbLibrarySongTitle(song, providerId) {
    const fallback = rbLibraryDisplayFilename(song, providerId);
    return (song && song.title) || fallback.replace(/_p\.psarc$/i, '').replace(/_/g, ' ');
}

function rbActiveLibraryProviderId() {
    const select = document.getElementById('rb-library-provider');
    if (select && select.value) return select.value;
    const api = rbLibraryProvidersApi();
    if (api && typeof api.activeProviderId === 'function') return api.activeProviderId();
    return rbLibraryProviderSnapshot().current || 'local';
}

function rbIsLocalLibraryProvider(providerId) {
    const api = rbLibraryProvidersApi();
    if (api && typeof api.isLocal === 'function') return api.isLocal(providerId);
    const provider = rbLibraryProviderById(providerId);
    return providerId === 'local' || (provider && provider.kind === 'local');
}

function rbLibraryProviderSupports(providerId, capability) {
    const api = rbLibraryProvidersApi();
    if (api && typeof api.supports === 'function') return api.supports(providerId, capability);
    const provider = rbLibraryProviderById(providerId);
    return !!provider && Array.isArray(provider.capabilities) && provider.capabilities.includes(capability);
}

async function rbRefreshLibraryProviderSelector() {
    const refreshed = await rbLibraryCapabilityCommand('refresh-providers', { restoreSaved: true });
    _rbLibraryProvidersLoaded = true;
    const select = document.getElementById('rb-library-provider');
    if (!select) return;
    const snapshot = rbLibraryPayload(refreshed) || rbLibraryProviderSnapshot();
    const providers = (snapshot.providers || []).filter(provider =>
        Array.isArray(provider.capabilities) && provider.capabilities.includes('library.read'));
    select.innerHTML = providers.map(provider =>
        `<option value="${rbEsc(provider.id)}">${rbEsc(provider.label || provider.id)}</option>`
    ).join('') || '<option value="local">My Library</option>';
    select.value = providers.some(provider => provider.id === snapshot.current) ? snapshot.current : 'local';
    select.classList.toggle('hidden', providers.length <= 1);
}

async function rbSetLibraryProvider(providerId) {
    const id = String(providerId || 'local');
    const result = await rbLibraryCapabilityCommand('select-provider', {}, { providerId: id });
    if (!rbLibraryPayload(result)) {
        console.warn('[rig_builder] library.select-provider did not return a handled payload');
    }
    await rbRefreshLibraryProviderSelector();
    rbShowSongList();
    rbListSongs();
}

function rbExtractSyncLocalFilename(result) {
    const payload = result && result.result && typeof result.result === 'object' ? result.result : result;
    if (!payload || typeof payload !== 'object') return '';
    return String(payload.filename || payload.localFilename || payload.local_filename ||
        payload.playFilename || payload.play_filename || payload.libraryRelativePath || '');
}

async function rbSyncLibrarySong(providerId, songId, statusEl) {
    if (!providerId || !songId) throw new Error('Missing provider song id');
    if (statusEl) {
        statusEl.className = 'text-xs text-blue-300 ml-2';
        statusEl.textContent = 'syncing...';
    }
    const commandResult = await rbLibraryCapabilityCommand('sync-song', {}, { providerId, songId });
    if (!commandResult) {
        throw new Error('Library sync capability is unavailable');
    }
    if (commandResult.outcome !== 'handled') {
        throw new Error(commandResult.reason || 'Library sync failed');
    }
    let result = rbLibraryPayload(commandResult);
    if (result && result.result) result = result.result;
    const localFilename = rbExtractSyncLocalFilename(result);
    if (statusEl) {
        if (localFilename) {
            statusEl.className = 'text-xs text-green-300 ml-2';
            statusEl.textContent = 'synced';
        } else {
            statusEl.className = 'text-xs text-yellow-300 ml-2';
            statusEl.textContent = 'synced, but no local file returned';
        }
    }
    return { result, localFilename };
}

async function rbOpenLibrarySongFromList(row) {
    const el = row && row.closest ? row.closest('[data-rb-library-song]') : row;
    if (!el) return;
    const providerId = el.dataset.rbProvider || rbActiveLibraryProviderId();
    let filename = el.dataset.rbFilename || '';
    if (filename) {
        await rbLoadSongTones(filename);
        return;
    }
    const songId = el.dataset.rbSongId || '';
    const statusEl = el.querySelector('[data-rb-sync-status]');
    if (!songId || el.dataset.rbCanSync !== '1') {
        if (statusEl) {
            statusEl.className = 'text-xs text-yellow-300 ml-2';
            statusEl.textContent = 'not available locally';
        }
        return;
    }
    try {
        el.classList.add('opacity-70', 'pointer-events-none');
        const synced = await rbSyncLibrarySong(providerId, songId, statusEl);
        filename = synced.localFilename;
        if (!filename) return;
        el.dataset.rbFilename = filename;
        await rbLoadSongTones(filename);
    } catch (e) {
        if (statusEl) {
            statusEl.className = 'text-xs text-red-300 ml-2';
            statusEl.textContent = `sync failed: ${e.message || e}`;
        }
    } finally {
        el.classList.remove('opacity-70', 'pointer-events-none');
    }
}

// Cache-bust query for gear-photo URLs. Set once per session so:
//   - 200 responses still ETag-validate on each refresh (no extra
//     network traffic — the param doesn't change between renders)
//   - 404 responses cached by the browser from BEFORE a fix (e.g.
//     the case-insensitive lookup landed) get a new URL on the next
//     Slopsmith launch, busting the stale cache miss
// The current epoch is plenty unique; we only need it to differ
// across plugin restarts.
const _RB_GEAR_PHOTO_CB = `?cb=${Date.now()}`;

// ── RbMegaChain: pre-loaded whole-song chain with bypass-flip switching
//
// DEFAULT playback path (2026-05-28). Toggle in Settings → "Chain
// preloader" or via the runtime kill-switch `window.__rbMegaChain =
// false`. Replaces the bundle's clearChain +
// loadPreset cycle on every tone change with a single loadPreset at song
// load + setBypass(slot_range, on/off) on each tone change. Result: zero
// tone-change transient (no spike, no mute parche needed) at the cost of
// every NAM staying in memory + processing (bypassed = passthrough,
// still costs a fraction of CPU each).
//
// Coordination with the bundle:
//   - When mega-chain mode is ON, we automatically force the bundle's
//     AMP button OFF (the bundle's _namApplyCurrentSongTone would call
//     clearChain + loadPreset on every tone change, destroying our
//     mega-chain). We drive startAudio + monitor un-mute ourselves.
//   - The fetch interceptor that redirects /native-preset/{id} stops
//     firing in this mode (bundle won't fetch with AMP off).
//   - We replicate _namDuckGuitarStem so the song's stem guitar gets
//     muted just like the bundle would have done.
//
// Lifecycle:
//   - song:loaded → RbMegaChain.buildForSong(filename)
//   - polling-based tone-change detection via window.highway
//   - song:unloaded / song change → RbMegaChain.teardown()
const RbMegaChain = (function () {
    let _active = false;       // are we currently driving the engine for a song
    let _pending = false;      // a build is scheduled/running and owns the next chain transition
    let _pendingFilename = null;
    let _activeFilename = null;
    let _lastError = null;
    let _mega = null;          // last fetched /mega_chain response
    let _buildGen = 0;         // bumped each buildForSong() — lets the 404 retry
                               // loop bail if a newer song load superseded it
    let _seedTried = null;     // filename we already kicked an on-demand seed for
    let _activeToneKey = null; // tone_key currently un-bypassed
    let _pollHandle = null;    // setInterval handle watching highway tone changes
    let _duckedStems = null;   // saved gain nodes to restore on teardown
    // Map from chain-array INDEX (what the backend gives us in
    // active_slots, master_pre_indices etc.) to the ENGINE'S slot ID
    // (what setBypass/setMultiBypass actually uses). The two are not the
    // same — the engine assigns its own IDs during loadPreset. We capture
    // them via getChainState() right after loading.
    let _indexToSlotId = [];   // chain index → engine slotId

    function _settingOn() {
        // Chain preloader is ALWAYS on now (the Setup toggle was removed).
        // The DevTools kill-switch `window.__rbMegaChain = false` still works
        // as an escape hatch for debugging a misbehaving song / weak PC.
        return window.__rbMegaChain !== false;
    }

    function _settingKnown() {
        return typeof window.__rbMegaChainSetting !== 'undefined';
    }

    function _currentSongFilename() {
        return (window.slopsmith && window.slopsmith.currentSong && window.slopsmith.currentSong.filename)
            || window.__rbPlaybackSettingsFilename
            || rbState.currentSongFile
            || '';
    }

    function _state() {
        return {
            active: _active,
            pending: _pending,
            failed: !!_lastError,
            error: _lastError && _lastError.reason || '',
            enabled: _settingOn(),
            activeToneKey: _activeToneKey || '',
            filename: _activeFilename || _pendingFilename || (_lastError && _lastError.filename) || _currentSongFilename(),
        };
    }

    function _emitState() {
        const detail = _state();
        try { window.dispatchEvent(new CustomEvent('rig-builder:tones-state', { detail })); } catch (_) {}
        try {
            if (window.slopsmith && typeof window.slopsmith.emit === 'function') {
                window.slopsmith.emit('rig-builder:tones-state', detail);
            }
        } catch (_) {}
        if (typeof rbUpdatePlayerToneButton === 'function') rbUpdatePlayerToneButton();
    }

    function markPending(filename) {
        _pending = true;
        _pendingFilename = filename || _currentSongFilename() || null;
        _lastError = null;
        rbSelectAudioEffectsRoute('mega-chain-pending');
        _emitState();
    }

    function _markFailed(filename, reason) {
        _pending = false;
        _pendingFilename = null;
        _active = false;
        _activeFilename = null;
        _activeToneKey = null;
        _lastError = {
            filename: filename || _currentSongFilename() || '',
            reason: rbShortSafeText(reason || 'Rig Builder could not load this song\'s tone chain', 'Rig Builder could not load this song\'s tone chain'),
        };
        _emitState();
    }

    function _clearPending(filename) {
        if (!filename || !_pendingFilename || filename === _pendingFilename) {
            _pending = false;
            _pendingFilename = null;
            _emitState();
        }
    }

    function _api() {
        const a = window.slopsmithDesktop && window.slopsmithDesktop.audio;
        return (a && typeof a.loadPreset === 'function') ? a : null;
    }

    // `opts.useFirstChangeIfNoBase`: when set, and the highway didn't
    // publish a tone base but DID publish a non-empty `toneChanges`
    // schedule, use the FIRST scheduled change's tone name as the
    // intro. Some songs (notably Bon Jovi "Livin' on a Prayer", Police
    // "Message in a Bottle", anything where Slopsmith's PSARC parser
    // populated the change list but not the base) leave `getToneBase`
    // empty even though the schedule is fully there — without this
    // option, we'd fall through to a heuristic guess after the 10 s
    // timeout. WITH this option, the intro tone lands ~100 ms after
    // song:loaded, exactly like for well-formed songs.
    //
    // Default off so the regular polling loop still distinguishes
    // "no base + no changes yet" (return null → keep waiting) from
    // "no base + schedule populated" (return first scheduled tone).
    // The recheck schedule + the final-fallback timer pass `true`.
    function _resolveActiveToneKey(opts) {
        try {
            const hw = window.highway;
            if (!hw || typeof hw.getTime !== 'function') return null;
            const t = hw.getTime();
            const changes = hw.getToneChanges ? hw.getToneChanges() : [];
            const base = hw.getToneBase ? hw.getToneBase() : '';
            let active = base;
            if (Array.isArray(changes)) {
                for (const tc of changes) {
                    if (tc && tc.t <= t) active = tc.name;
                    else break;
                }
                if (!active && opts && opts.useFirstChangeIfNoBase
                    && changes.length > 0 && changes[0] && changes[0].name) {
                    active = changes[0].name;
                }
            }
            return (active && String(active).trim()) || null;
        } catch (_) { return null; }
    }

    function _findToneByKey(toneKey) {
        if (!_mega || !Array.isArray(_mega.tones) || !toneKey) return null;
        const exact = _mega.tones.find(t => t.tone_key === toneKey);
        if (exact) return exact;
        const wanted = String(toneKey).trim().toLowerCase();
        return _mega.tones.find(t =>
            String(t.tone_key || '').trim().toLowerCase() === wanted
        ) || null;
    }

    // Mute the song's "guitar" stem so the original DI doesn't double up
    // with our chain output — same job the bundle's _namDuckGuitarStem
    // does when AMP is on. Saves the previous gain values for teardown.
    function _duckGuitarStem() {
        const stems = window._stemsState;
        if (!stems || !Array.isArray(stems)) return;
        _duckedStems = [];
        for (const s of stems) {
            if (/guitar/i.test(s.id || '') && s.gain && s.gain.gain) {
                _duckedStems.push({ stem: s, prevGain: s.gain.gain.value });
                try { s.gain.gain.value = 0; } catch (_) {}
            }
        }
    }
    function _restoreGuitarStem() {
        if (!_duckedStems) return;
        for (const d of _duckedStems) {
            try { if (d.stem && d.stem.gain && d.stem.gain.gain) d.stem.gain.gain.value = d.prevGain; } catch (_) {}
        }
        _duckedStems = null;
    }

    // If the bundle's AMP is on, click it off so it stops doing its own
    // clearChain+loadPreset on every tone change.
    let _ampToggleAllowed = false;
    function _forceBundleAmpOff() {
        const btn = document.getElementById('btn-nam');
        if (!btn) return;
        const isOn = /(?:^|\s)bg-green-/.test(btn.className);
        if (isOn) {
            try {
                _ampToggleAllowed = true;
                btn.click();
            } catch (_) {
            } finally {
                _ampToggleAllowed = false;
            }
        }
    }

    // Apply bypass state across the chain so only `activeToneKey` runs.
    // Each slot has an "intended" bypass set by the user (Master Chain
    // tab toggle, per-song bypass button). We respect that bypass for
    // slots belonging to the active tone + master. Slots that DON'T
    // belong to the active tone get force-bypassed (signal passes
    // through them transparently).
    //
    // Data model (set by the backend):
    //   - tones[i].slots = [{idx, bypassed}, ...]       per-tone slots with persisted bypass
    //   - master_pre_slots / master_post_slots          same shape, always considered "active"
    //
    // The backend gives us chain INDICES (0..N-1), but setBypass/
    // setMultiBypass want the engine's actual slot IDs, which loadPreset
    // assigns dynamically. We translate via _indexToSlotId captured
    // right after loadPreset returned.
    async function _applyActiveTone(activeToneKey) {
        const api = _api();
        if (!api || !_mega) return;
        const tone = _findToneByKey(activeToneKey);
        const toneIndex = tone && Array.isArray(_mega.tones) ? _mega.tones.indexOf(tone) : -1;
        const segmentId = tone ? rbToneSegmentId(tone, toneIndex >= 0 ? toneIndex : 0) : '';
        const chainSpec = (_mega.native_preset && _mega.native_preset.chain) || [];
        const totalStages = chainSpec.length || 0;
        if (!totalStages) return;

        // Build a map: idx → desired bypass. Default for every slot is
        // bypassed=true (passthrough). For master + active-tone slots,
        // use the persisted bypass from the backend.
        const bypassByIdx = new Array(totalStages).fill(true);
        const activeToneSlotByIdx = new Map();
        const applyEntry = (entry) => {
            if (!entry || typeof entry.idx !== 'number') return;
            if (entry.idx < 0 || entry.idx >= totalStages) return;
            bypassByIdx[entry.idx] = !!entry.bypassed;
            activeToneSlotByIdx.set(entry.idx, entry);
        };
        (_mega.master_pre_slots  || []).forEach(applyEntry);
        (_mega.master_post_slots || []).forEach(applyEntry);
        if (tone && Array.isArray(tone.slots)) tone.slots.forEach(applyEntry);

        const activatedByHost = await rbActivateSegmentWithHost(segmentId, tone && tone.tone_key || activeToneKey || '');
        if (!activatedByHost) {
            const changes = [];
            const mapLen = _indexToSlotId.length;
            for (let idx = 0; idx < totalStages; idx++) {
                // Skip chain indices whose stage failed to load (slot ID is
                // null in the map). Firing setBypass with the raw index as
                // fallback would hit the WRONG slot in the engine since
                // engine IDs aren't sequential 0..N-1.
                if (idx >= mapLen || _indexToSlotId[idx] == null) continue;
                const slotId = _indexToSlotId[idx];
                changes.push({ slotId, bypassed: bypassByIdx[idx] });
            }
            try {
                if (typeof api.setMultiBypass === 'function') {
                    await api.setMultiBypass(changes);
                } else if (typeof api.setBypass === 'function') {
                    for (const c of changes) await api.setBypass(c.slotId, c.bypassed);
                }
            } catch (e) {
                console.warn('[rig_builder mega-chain] applyActiveTone failed:', e);
            }
        }
        const effectiveChain = chainSpec.map((stage, idx) => {
            const copy = Object.assign({}, stage, { bypassed: !!bypassByIdx[idx] });
            const activeEntry = activeToneSlotByIdx.get(idx);
            if (activeEntry) {
                if (activeEntry.rs_gain != null) copy.rs_gain = activeEntry.rs_gain;
                if (activeEntry.rs_gear != null) copy.rs_gear = activeEntry.rs_gear;
                if (activeEntry.slot != null) copy.slot = activeEntry.slot;
                if (activeEntry.type != null) copy.type = activeEntry.type;
            }
            return copy;
        });
        await rbApplyChainInputDrive({ chain: effectiveChain });
        await rbStartFinalChainNormalizer(effectiveChain);
        _activeToneKey = activeToneKey;
        _emitState();
    }

    async function buildForSong(filename) {
        if (!_settingOn()) {
            console.log('[rig_builder mega-chain] buildForSong skipped — setting off');
            _clearPending(filename);
            return false;
        }
        markPending(filename);
        const api = _api();
        if (!api) {
            console.warn('[rig_builder mega-chain] buildForSong aborted — no native audio API');
            _markFailed(filename, 'Native audio engine is not available');
            return false;
        }
        if (!filename) {
            console.warn('[rig_builder mega-chain] buildForSong aborted — no filename');
            _markFailed(filename, 'No song filename is available');
            return false;
        }
        // Tear down any previous session before starting a fresh one.
        await teardown(true);   // silent — no stem restore on chained calls
        markPending(filename);

        // Generation guard: if another song loads while we're waiting on the
        // 404-retry loop below, this build is stale — bail instead of loading
        // the wrong chain on top of the new song.
        const myGen = ++_buildGen;

        // Fetch the mega-chain, tolerating the seeding RACE: a freshly
        // materialized/opened song's tone_mappings are written ASYNCHRONOUSLY
        // (background watcher / cloud_loader), so the first build at +600 ms
        // can beat the seed and 404 — which is exactly the "no sound until I
        // exit and reload the song" report. On a 404 we (a) kick a one-shot
        // on-demand seed (idempotent, lock-guarded server-side; needs a
        // tone3000 key) and (b) retry with backoff until it lands, so the user
        // no longer has to reload by hand. A non-404 error is a real failure —
        // don't retry, fall straight back to the cooperative path. On every
        // terminal failure we _markFailed so the player button can explain why
        // rig tones are off (ownership visibility, audio-effects migration).
        const _RETRY_DELAYS = [0, 1200, 2500, 4000, 6000];   // ~13.7 s total
        let mega = null;
        for (let attempt = 0; attempt < _RETRY_DELAYS.length; attempt++) {
            if (_RETRY_DELAYS[attempt]) {
                await new Promise(r => setTimeout(r, _RETRY_DELAYS[attempt]));
            }
            if (myGen !== _buildGen) {
                console.log('[rig_builder mega-chain] build superseded by a newer song load — abandoning retry');
                return false;
            }
            let resp;
            try {
                resp = await fetch(`${RB_API}/mega_chain/${encodeURIComponent(filename)}`);
            } catch (e) {
                console.warn('[rig_builder mega-chain] fetch failed:', e);
                _markFailed(filename, e && e.message ? e.message : 'Could not fetch this song\'s tone chain');
                return false;
            }
            if (resp.ok) {
                mega = await resp.json();
                break;
            }
            if (resp.status !== 404) {
                console.warn(`[rig_builder mega-chain] /mega_chain/${filename} → HTTP ${resp.status} — giving up (real backend error)`);
                _markFailed(filename, `Rig Builder song chain unavailable: HTTP ${resp.status}`);
                return false;
            }
            // 404 → not seeded yet. Kick the on-demand seed ONCE for this song,
            // then keep retrying so the watcher/seed result is picked up.
            if (_seedTried !== filename) {
                _seedTried = filename;
                fetch(`${RB_API}/auto_download_song`, {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ filename }),
                }).then(r => console.log(`[rig_builder mega-chain] on-demand seed for "${filename}" → HTTP ${r.status}`))
                  .catch(e => console.warn('[rig_builder mega-chain] on-demand seed failed:', e));
            }
            console.warn(`[rig_builder mega-chain] /mega_chain/${filename} → 404 (mappings not seeded yet — seeding + retry ${attempt + 1}/${_RETRY_DELAYS.length - 1})`);
        }
        if (!mega) {
            // Still no mappings after the retry window → fall back to the
            // cooperative path (the bundle still plays). Run Batch all or open
            // the song in the per-song tab to seed it (needs a tone3000 key).
            console.warn(`[rig_builder mega-chain] /mega_chain/${filename} → still no mappings after retries (no tone3000 key, or song not mappable? Run Batch all or open it in the per-song tab)`);
            _markFailed(filename, 'No mapped Rig Builder tones were found for this song');
            return false;
        }
        if (!mega || !mega.native_preset
            || !Array.isArray(mega.native_preset.chain)
            || mega.native_preset.chain.length === 0) {
            console.warn('[rig_builder mega-chain] empty chain returned by backend:', mega);
            _markFailed(filename, 'Rig Builder returned an empty tone chain for this song');
            return false;
        }
        _mega = mega;

        // 1. Force the bundle's AMP off so it stops fighting us.
        _forceBundleAmpOff();

        // 2. Mute the bundle's guitar stem so the song's DI doesn't
        //    play through alongside our chain.
        _duckGuitarStem();

        // 3. Load the mega-chain into the engine. On current Desktop this
        //    goes through audio-effects load options so the executor owns
        //    load mute/fade, initial route gain, and startAudio. The legacy
        //    fallback still performs rbPreLoadMute immediately before its
        //    direct clearChain + loadPreset call.
        let loadedViaAudioEffects = false;
        try {
            const loaded = await rbLoadNativePresetPayload(api, mega, {
                mode: 'mega-chain',
                ref: 'song',
                filename,
                authorization: 'playback-session',
                executorOptions: rbAudioEffectsLoadOptionsForChain(mega.native_preset.chain, { startAudio: true }),
            });
            loadedViaAudioEffects = !!loaded.viaAudioEffects;
            const res = loaded.result;
            void rbSyncAudioEffectsCapability('mega-chain-loaded', { chain: mega.native_preset.chain, mode: 'mega-chain', bridge: !loaded.viaAudioEffects });
            // Compute dedupe savings: total active_slot entries across
            // all tones vs unique stages in the chain. A 4-tone song that
            // shares one amp + one cab across all four tones reports
            // something like "20 → 8 stages (60% saved)".
            const sumActiveSlots = (mega.tones || []).reduce((acc, t) =>
                acc + (t.slots ? t.slots.length : 0), 0)
                + (mega.master_pre_count || 0) * (mega.tones || []).length   // master appears in every tone conceptually
                + (mega.master_post_count || 0) * (mega.tones || []).length;
            const totalStages = mega.native_preset.chain.length;
            const savings = sumActiveSlots > 0
                ? Math.round((1 - totalStages / sumActiveSlots) * 100)
                : 0;
            console.log(`[rig_builder mega-chain] loaded ${totalStages} unique stages`
                + ` for "${filename}" — ${mega.tones.length} tones`
                + ` (master ${mega.master_pre_count}+${mega.master_post_count}, ${savings}% deduped)`,
                res, loaded.viaAudioEffects ? '(audio-effects executor)' : '(legacy loadPreset)');
            // Capture the engine's actual slot IDs so _applyActiveTone can
            // bypass the right ones. setBypass uses ENGINE slot IDs, not
            // chain-array indices — and the engine assigns its own IDs
            // during loadPreset (verified: slot.id and slot.slotId in the
            // getChainState() response, mirroring rbReapplyVstParamsToChain).
            // Without this map every bypass call used wrong IDs and the
            // user heard a random mix of stages active.
            _indexToSlotId = [];
            try {
                if (typeof api.getChainState === 'function') {
                    const loaded = await api.getChainState();
                    if (Array.isArray(loaded)) {
                        const expected = mega.native_preset.chain.length;
                        const relevant = loaded.length > expected ? loaded.slice(loaded.length - expected) : loaded;
                        for (let i = 0; i < relevant.length; i++) {
                            const s = relevant[i];
                            const id = (s && (s.id != null ? s.id : s.slotId != null ? s.slotId : i));
                            _indexToSlotId[i] = id;
                        }
                        const got = _indexToSlotId.length;
                        if (loaded.length > expected) {
                            console.warn(`[rig_builder mega-chain] engine reported ${loaded.length} total slots after loading ${expected}; using the most recent ${expected} slot IDs for this chain.`);
                        } else if (got < expected) {
                            // The engine couldn't load every stage we sent. Likely a
                            // missing file or a malformed plugin. Mark the unreachable
                            // chain indices as null so _applyActiveTone skips them
                            // rather than firing setBypass on the WRONG slot ID via
                            // the index-as-fallback path.
                            const skipped = expected - got;
                            console.warn(`[rig_builder mega-chain] STAGE LOAD MISMATCH: sent ${expected} stages but engine reported only ${got} — ${skipped} stage(s) failed to load. Likely culprit: missing NAM/IR file, malformed VST, or a path that no longer exists. The remaining ${skipped} chain index/indices will be skipped in bypass updates so we don't fire setBypass on the wrong slot.`);
                            for (let i = got; i < expected; i++) _indexToSlotId[i] = null;
                        }
                        console.log(`[rig_builder mega-chain] captured ${got} slot IDs (engine assigned IDs vs chain index — first 5: ${_indexToSlotId.slice(0, 5).join(',')}…)`);
                    }
                }
            } catch (e) {
                console.warn('[rig_builder mega-chain] getChainState failed:', e);
            }
            // VST params: walk the freshly-loaded mega-chain and dispatch
            // setParameter so VSTs come up at their saved values, not the
            // plug-in defaults. Without this users had to open each VST
            // editor manually for the saved params to take effect.
            await rbReapplyVstParamsToChain(api, mega.native_preset.chain).catch((e) =>
                console.warn('[rig_builder mega-chain] re-apply VST params:', e));
        } catch (e) {
            console.warn('[rig_builder mega-chain] loadPreset failed, falling back:', e);
            _mega = null;
            _restoreGuitarStem();
            _markFailed(filename, e && e.message ? e.message : 'Native engine could not load this tone chain');
            return false;
        }

        // 4. Set initial bypass: only the song's CURRENT tone runs.
        // The highway may not have populated its tone changes / base yet
        // at this point (we ran 600 ms after song:loaded, but the WS feed
        // arrives in pieces). If _resolveActiveToneKey returns null we
        // DELIBERATELY leave every tone bypassed — silence — instead of
        // falling back to tones[0]. The previous fallback gave us the
        // wrong tone audible for ~1 s on most songs (DB-order != song-
        // intro-order). The initial-recheck schedule below catches the
        // real tone within 100-700 ms once the highway publishes it,
        // and applies it then. Brief silence is a better failure mode
        // than playing the wrong tone confidently.
        const initialKey = _resolveActiveToneKey();
        // Single-tone song: there's nothing to resolve from the highway — apply
        // that one tone NOW so the un-mute below fires with the REAL tone active
        // (not a blast of silence-then-tone). Multi-tone songs fall back to the
        // highway resolve and stay muted until the first real tone lands.
        const initialTone = (Array.isArray(mega.tones) && mega.tones.length === 1)
            ? mega.tones[0]
            : (initialKey ? _findToneByKey(initialKey) : null);
        await _applyActiveTone(initialTone ? initialTone.tone_key : null);
        // The chain is fully loaded, VST params re-applied, and the active tone's
        // bypass is set — un-mute ONLY if a REAL tone is active. If the highway
        // wasn't ready (initialTone null → everything bypassed), STAY muted; the
        // recheck / default-tone path below fires rbSignalChainLoaded() the moment
        // it applies the first real tone. Un-muting on the bypassed/NONE state let
        // the real tone (loud amp) come in un-muted later and blast before the
        // leveler caught up. (The 15 s safety net still backstops a stuck load.)
        if (initialTone) await rbSignalChainLoaded();
        console.log(`[rig_builder mega-chain] initial tone → ${initialTone
            ? `"${initialTone.tone_key}" (from highway)`
            : 'NONE (highway not ready yet — waiting for first recheck)'}`);

        // 5. Start audio if it isn't running yet (bundle would have done this).
        // DO NOT manually un-mute chain/monitor here — rbPreLoadMute's
        // setTimeout will fade chain gain 0→1 + un-mute monitor on its
        // own timer. Doing setGain('chain', 1.0) here would defeat the
        // mute, letting the first-buffer attack of the freshly-loaded
        // NAMs leak through.
        try {
            const wasRunning = api.isAudioRunning ? await api.isAudioRunning().catch(() => true) : true;
            if (!loadedViaAudioEffects && !wasRunning && api.startAudio) await api.startAudio();
            // _applyActiveTone already set the input drive from the active
            // tone's amp metadata; don't overwrite it with a generic value.
        } catch (e) { console.warn('[rig_builder mega-chain] startAudio failed:', e); }

        // 6. Start watching highway for tone changes AND the bundle's
        // AMP button (we have to turn it back off if anything in the
        // bundle re-prends it, or it will tear down our mega-chain).
        _startPolling();
        _startAmpGuard();

        // 7. Re-check the active tone a few times over the next 3 seconds
        // to catch the case where the highway populates its tone base
        // AFTER our 600 ms initial trigger. The polling already does this
        // every 200 ms via lastKey-diff, but we kick it explicitly here
        // with a forced re-apply so the user doesn't hear ~600 ms of the
        // wrong tone before the polling notices.
        // Recheck schedule: front-loaded so we catch the highway tone-base
        // publication as soon as it lands (most of the time inside the
        // first second), without giving up too early if the WS feed lags.
        // Helper: have we received ANY tone metadata from the highway?
        // True iff either a non-empty base or at least one tone change
        // has been published. Used by the early "no-schedule" detector
        // below to distinguish 'song genuinely has no tone-switching'
        // (PSARC didn't pack any) from 'highway still publishing'.
        const _highwayHasAnyToneData = () => {
            try {
                const hw = window.highway;
                if (!hw) return false;
                const base = hw.getToneBase ? hw.getToneBase() : '';
                const changes = hw.getToneChanges ? hw.getToneChanges() : [];
                return !!(
                    (base && String(base).trim())
                    || (Array.isArray(changes) && changes.length > 0)
                );
            } catch (_) { return false; }
        };

        // Schedule extended to 10 s (was 6 s) — gives slow highway WS
        // publishes time to arrive before we commit to the heuristic
        // fallback. Each tick first tries the strict resolver, then
        // (on later ticks) the relaxed resolver that accepts the first
        // scheduled tone-change as the intro when no base is published.
        const recheckSchedule = [
            100, 200, 400, 700, 1000, 1500, 2000, 2700, 3500, 4500, 5500,
            6500, 7500, 8500, 9500,
        ];
        recheckSchedule.forEach((delay, i) => {
            setTimeout(() => {
                if (!_active || !_mega) return;
                // First 4 rechecks: strict mode. After 700 ms, accept
                // first-change-as-base too so songs with missing
                // toneBase metadata get their intro tone within 1 s
                // instead of waiting for the 10 s heuristic fallback.
                const allowFirstChange = delay > 700;
                const key = _resolveActiveToneKey({
                    useFirstChangeIfNoBase: allowFirstChange,
                });
                if (!key || key === _activeToneKey) return;
                const tone = _findToneByKey(key);
                if (!tone) return;
                _applyActiveTone(tone.tone_key).then(() => {
                    rbSignalChainLoaded().catch(() => {});   // un-mute on the first real tone
                    const src = allowFirstChange ? 'first-change-or-base' : 'base';
                    console.log(`[rig_builder mega-chain] initial-recheck #${i+1} (t+${delay}ms, ${src}) → switched to "${tone.tone_key}"`);
                }).catch(() => {});
            }, delay);
        });
        // Helper: pick the song's default tone matched to the user's
        // active arrangement. The bundle's highway exposes
        // `getStringCount()` → 4 = bass, 6/7/8 = guitar, which is the
        // authoritative signal for what the user is plucking right
        // now. Picking the wrong family is the user-visible bug we're
        // here to fix: a bass-playing user got a guitar tone applied
        // 1.5 s into the song (overriding the bundle's correct intro)
        // because the old heuristic blindly preferred non-bass tones.
        //
        // Strategy:
        //   - 4 strings → pick a bass tone (filter to bass-flavored)
        //   - 6+ strings → pick a guitar tone (filter out bass-flavored)
        //   - unknown / no matching tone → fall back to tones[0]
        const _pickDefaultTone = () => {
            const all = (mega.tones || []);
            if (!all.length) return null;
            const isBassFlavored = t =>
                /(^|_)bass(_|\b)/i.test(t.tone_key || '')
                || (Array.isArray(t.chain) && t.chain.some(p => /^Bass_/i.test(p.rs_gear || '')));
            let stringCount = 6;
            try {
                const hw = window.highway;
                if (hw && typeof hw.getStringCount === 'function') {
                    const n = hw.getStringCount();
                    if (typeof n === 'number' && n > 0) stringCount = n;
                }
            } catch (_) {}
            const wantBass = stringCount <= 4;
            const preferred = wantBass
                ? all.find(t => isBassFlavored(t))
                : all.find(t => !isBassFlavored(t));
            return preferred || all[0];
        };

        // Early no-schedule detector: most songs that hit the old
        // "FALLBACK after 10s" warning DON'T have late-arriving tone
        // metadata — they have NONE AT ALL. The PSARC was packed without
        // a section→tone schedule, so the bundle's audio-engine logs
        // 'Song has no rebuildable tone-switching — keeping current
        // chain' and the highway never publishes either base or
        // changes. Waiting the full 10 s for nothing is just dead air +
        // a misleading warning. At t+1500 ms we check: if the highway
        // STILL has zero data, treat it as a no-schedule song, pick the
        // default tone immediately, and log an INFO line (not a
        // warning) explaining the situation. Genuine slow-WS cases
        // (rare) will have published *something* by 1.5 s — even an
        // empty toneChanges array gets populated as soon as the parser
        // runs.
        setTimeout(() => {
            if (!_active || !_mega) return;
            if (_activeToneKey) return;     // a recheck already landed
            if (_highwayHasAnyToneData()) return;  // schedule en route
            const tone = _pickDefaultTone();
            if (!tone) return;
            _applyActiveTone(tone.tone_key).then(() => {
                rbSignalChainLoaded().catch(() => {});   // un-mute on the first real tone
                console.log(
                    `[rig_builder mega-chain] no schedule in PSARC for this song — `
                    + `applying default tone "${tone.tone_key}". `
                    + `Single-tone behaviour (no mid-song switching) is by design.`);
            }).catch(() => {});
        }, 1500);

        // Last-chance fallback: if after 10 s the highway still hasn't
        // given us a tone (broken WS, unmapped song, truly exotic
        // arrangement with no schedule at all), pick a guitar tone
        // (or whatever's available) so the user isn't stuck in dead
        // silence forever. Prefer GUITAR over BASS: the tones array's
        // order comes from DB insertion (often alphabetical by tone_key)
        // which sometimes lists bass tones first — e.g. Reptilia →
        // tones[0] is "Reptilia_bass", which made the user hear a bass
        // tone when they were playing guitar. The instrument hint we
        // can extract is whether the tone_key looks bass-flavored.
        // Matches the strings nam_tone names bass tones with: "_bass",
        // "Bass_", or the gear referenced is in the Bass_* family.
        setTimeout(() => {
            if (!_active || !_mega) return;
            if (_activeToneKey) return;     // any recheck already landed
            // One more shot at the relaxed resolver before guessing —
            // catches songs where the change schedule arrived between
            // the last recheck (t+9500) and now (t+10000).
            const lastShot = _resolveActiveToneKey({ useFirstChangeIfNoBase: true });
            if (lastShot) {
                const tone = _findToneByKey(lastShot);
                if (tone) {
                    _applyActiveTone(tone.tone_key).then(() => {
                        rbSignalChainLoaded().catch(() => {});   // un-mute on the first real tone
                        console.log(`[rig_builder mega-chain] late base/first-change → "${tone.tone_key}"`);
                    }).catch(() => {});
                    return;
                }
            }
            const fallback = _pickDefaultTone();
            if (!fallback) return;
            _applyActiveTone(fallback.tone_key).then(() => {
                rbSignalChainLoaded().catch(() => {});   // un-mute on the first real tone
                console.warn(
                    `[rig_builder mega-chain] FALLBACK after 10s: applying `
                    + `"${fallback.tone_key}" — highway never published a tone `
                    + `base OR a tone change schedule, AND the early `
                    + `no-schedule detector at t+1500ms didn't fire (so highway `
                    + `looked like it might still be loading). Edge case.`);
            }).catch(() => {});
        }, 10000);

        _active = true;
        _activeFilename = filename;
        _clearPending(filename);
        _emitState();
        return true;
    }

    // Watch the bundle's AMP button and click it back off if anything
    // turns it on while we're active. The bundle re-prends AMP on some
    // events (tone-mapping reload, song restart, MIDI mode toggles…)
    // and once on it will call _namApplyCurrentSongTone, which does a
    // clearChain + loadPreset that destroys our mega-chain. Mute monitor
    // momentarily so the click of the toggle isn't audible.
    let _ampGuardHandle = null;
    let _ampRecoveryTimer = null;
    function _startAmpGuard() {
        _stopAmpGuard();
        _ampGuardHandle = setInterval(() => {
            if (!_active) return;
            const btn = document.getElementById('btn-nam');
            if (!btn) return;
            const isOn = /(?:^|\s)bg-green-/.test(btn.className);
            if (isOn) {
                console.warn('[rig_builder mega-chain] AMP turned on by bundle — turning it back off');
                _forceBundleAmpOff();
                // After AMP-off the bundle has already done clearChain;
                // rebuild our mega-chain so audio comes back.
                const filename = window.slopsmith && window.slopsmith.currentSong
                    && window.slopsmith.currentSong.filename;
                if (filename && !_ampRecoveryTimer) {
                    _ampRecoveryTimer = setTimeout(() => {
                        _ampRecoveryTimer = null;
                        buildForSong(filename).catch(e =>
                            console.warn('[rig_builder mega-chain] re-build after AMP-off failed:', e));
                    }, 350);
                }
            }
        }, 500);
    }
    function _stopAmpGuard() {
        if (_ampGuardHandle) { clearInterval(_ampGuardHandle); _ampGuardHandle = null; }
        if (_ampRecoveryTimer) { clearTimeout(_ampRecoveryTimer); _ampRecoveryTimer = null; }
    }

    if (!window.__rbAmpClickBlockerInstalled) {
        window.__rbAmpClickBlockerInstalled = true;
        document.addEventListener('click', (event) => {
            const target = event.target && event.target.closest ? event.target.closest('#btn-nam') : null;
            if (!target || _ampToggleAllowed || !_active) return;
            event.preventDefault();
            event.stopImmediatePropagation();
            console.log('[rig_builder mega-chain] AMP button click ignored — mega-chain owns the engine');
            _forceBundleAmpOff();
        }, true);
    }

    function _startPolling() {
        _stopPolling();
        let lastKey = _activeToneKey;
        _pollHandle = setInterval(async () => {
            if (!_active || !_mega) return;
            // Relaxed resolver: accepts first scheduled tone-change as
            // intro when base is missing. Safe in steady-state polling
            // because the resolver still walks all changes <= t first
            // — useFirstChangeIfNoBase only kicks in when NO change has
            // fired yet (i.e. we're before the song's first scheduled
            // tone). After that, the regular "last change <= t" branch
            // gives the right answer regardless.
            const key = _resolveActiveToneKey({ useFirstChangeIfNoBase: true });
            if (!key || key === lastKey) return;
            const tone = _findToneByKey(key);
            if (!tone) return;
            lastKey = key;
            await _applyActiveTone(tone.tone_key);
            const slots = Array.isArray(tone.slots) ? tone.slots : [];
            console.log(`[rig_builder mega-chain] switch → "${tone.tone_key}" (${slots.length} slots)`);
        }, 200);
    }

    function _stopPolling() {
        if (_pollHandle) { clearInterval(_pollHandle); _pollHandle = null; }
    }

    async function teardown(silent) {
        rbStopFinalChainNormalizer();
        _stopPolling();
        _stopAmpGuard();
        if (!silent) _restoreGuitarStem();
        if (_active) {
            const api = _api();
            const releasedByHost = await rbReleaseAudioEffectsRouteWithHost('mega-chain-teardown');
            if (!releasedByHost && api && api.clearChain) {
                try { await api.clearChain(); } catch (_) {}
            }
        }
        _active = false;
        _activeFilename = null;
        _mega = null;
        _activeToneKey = null;
        _pending = false;
        _pendingFilename = null;
        if (!silent) _lastError = null;
        _indexToSlotId = [];
        _emitState();
    }

    function isActive() { return _active; }
    function isPending() { return _pending; }
    function settingOn() { return _settingOn(); }
    function settingKnown() { return _settingKnown(); }
    async function toggleEnabled() {
        if (_active || _pending) {
            window.__rbMegaChain = false;
            await teardown(false);
            return false;
        }
        delete window.__rbMegaChain;
        const filename = _currentSongFilename();
        if (!filename) { _emitState(); return false; }
        markPending(filename);
        return buildForSong(filename);
    }
    function state() { return _state(); }

    // Live "Chain volume": set the final leveler's Output Trim (which is
    // applied AFTER the leveler's AGC, so it actually changes loudness instead
    // of being normalized away). Resolves the param by NAME to dodge the
    // engine's Buffer-Size/Sample-Rate prefix. Returns true if applied.
    async function setOutputTrimDb(db) {
        const api = _api();
        if (!api || !_active || !_mega) return false;
        if (typeof api.getParameters !== 'function' || typeof api.setParameter !== 'function') return false;
        const chain = (_mega.native_preset && _mega.native_preset.chain) || [];
        let idx = -1;
        for (let i = 0; i < chain.length; i++) {
            const g = chain[i] && chain[i].rs_gear;
            if (g && String(g).includes('__rb_final_leveler__')) { idx = i; break; }
        }
        if (idx < 0 || idx >= _indexToSlotId.length) return false;
        const slotId = _indexToSlotId[idx];
        if (slotId == null) return false;
        let trimId = null;
        try {
            const plist = await api.getParameters(slotId);
            if (Array.isArray(plist)) {
                plist.forEach((p, i2) => {
                    const nm = (p.name ?? p.label ?? '').toLowerCase();
                    if (nm.includes('output trim') || nm.includes('trim')) trimId = p.id ?? p.paramId ?? p.index ?? i2;
                });
            }
        } catch (_) { return false; }
        if (trimId == null) return false;
        // leveler trim range is -24..+18 dB (see PluginProcessor.cpp).
        const norm = Math.max(0, Math.min(1, (db - (-24)) / (18 - (-24))));
        try { await api.setParameter(slotId, trimId, norm); return true; }
        catch (_) { return false; }
    }

    const api = { buildForSong, teardown, isActive, isPending, settingOn, settingKnown, markPending, toggleEnabled, state, setOutputTrimDb };
    window.RbMegaChain = api;
    return api;
})();

function rbInjectPlayerToneButton() {
    const controls = document.getElementById('player-controls');
    if (!controls) return;
    const state = window.RbMegaChain && typeof window.RbMegaChain.state === 'function'
        ? window.RbMegaChain.state()
        : { active: false, pending: false, failed: false, enabled: false };
    const shouldShow = !!(state.active || state.pending || state.failed || state.enabled || window.__rbMegaChain === false);
    const existing = document.getElementById('btn-rig-tones');
    if (!shouldShow) {
        if (existing) existing.remove();
        return;
    }
    if (existing && existing.parentElement === controls) {
        rbUpdatePlayerToneButton();
        return;
    }
    if (existing) existing.remove();
    const closeBtn = Array.from(controls.children).find(child => (
        child.tagName === 'BUTTON'
        && /showScreen\(['"]home['"]\)/.test(child.getAttribute('onclick') || '')
    )) || controls.querySelector('button[onclick*="showScreen"]');
    const btn = document.createElement('button');
    btn.id = 'btn-rig-tones';
    btn.type = 'button';
    btn.onclick = async () => {
        btn.disabled = true;
        try {
            if (window.RbMegaChain && typeof window.RbMegaChain.toggleEnabled === 'function') {
                await window.RbMegaChain.toggleEnabled();
            }
        } finally {
            btn.disabled = false;
            rbUpdatePlayerToneButton();
        }
    };
    if (closeBtn && closeBtn.parentElement === controls) controls.insertBefore(btn, closeBtn);
    else controls.appendChild(btn);
    rbUpdatePlayerToneButton();
}

function rbUpdatePlayerToneButton() {
    const btn = document.getElementById('btn-rig-tones');
    if (!btn) return;
    const state = window.RbMegaChain && typeof window.RbMegaChain.state === 'function'
        ? window.RbMegaChain.state()
        : { active: false, pending: false, failed: false, error: '', enabled: false, activeToneKey: '' };
    if (state.pending) {
        btn.textContent = 'Rig Tones Loading';
        btn.title = 'Rig Builder is loading this song\'s tone chain. Click to cancel and turn tones off for this session.';
        btn.className = 'px-3 py-1.5 bg-amber-700/40 hover:bg-amber-700/60 rounded-lg text-xs text-amber-200 transition';
    } else if (state.failed) {
        btn.textContent = 'Rig Tones Failed';
        const reason = state.error ? ` ${state.error}` : '';
        btn.title = `Rig Builder could not load this song\'s tone chain.${reason} Click to retry.`;
        btn.className = 'px-3 py-1.5 bg-red-700/50 hover:bg-red-700/70 rounded-lg text-xs text-red-100 transition';
    } else if (state.active) {
        btn.textContent = 'Rig Tones On';
        const active = state.activeToneKey ? ` Active tone: ${state.activeToneKey}.` : '';
        btn.title = `Rig Builder is playing this song\'s mapped tones.${active} Click to turn them off for this session.`;
        btn.className = 'px-3 py-1.5 bg-emerald-700/40 hover:bg-emerald-700/60 rounded-lg text-xs text-emerald-200 transition';
    } else {
        btn.textContent = 'Rig Tones Off';
        btn.title = 'Rig Builder song tones are off for this session. Click to load the mapped tones for this song.';
        btn.className = 'px-3 py-1.5 bg-dark-600 hover:bg-dark-500 rounded-lg text-xs text-gray-400 transition';
    }
}

window.addEventListener('rig-builder:tones-state', () => rbInjectPlayerToneButton());

// Hook into the slopsmith song lifecycle. `song:loaded` fires from
// highway.js whenever the in-game player has fully loaded a CDLC.
// `song:unloaded` doesn't appear to fire reliably across all builds, so
// we also tear down whenever buildForSong is called again (the body of
// buildForSong does teardown(true) before starting a new session).
//
// Also fall back to polling `window.slopsmith.currentSong` for cases
// where the song was loaded BEFORE this hook installed itself (the
// EventEmitter doesn't replay missed events, so a song:loaded fired
// during plugin boot would otherwise be lost).
(function () {
    if (window.__rbMegaChainHookInstalled) return;
    window.__rbMegaChainHookInstalled = true;

    // Initialise window.__rbMegaChainSetting from the persisted /settings
    // value AS EARLY AS POSSIBLE. rbLoadSettings (called from rbInit when
    // the user opens the Rig Builder plugin) is normally what writes this
    // flag, but if the user loads a song before ever opening Rig Builder
    // the flag stays undefined and the hook below thinks the setting is
    // off. Fire-and-forget — the polling fallback will pick up the song
    // as soon as the flag flips.
    fetch(`${window.RB_API}/settings`).then(r => r.json()).then(s => {
        if (s && typeof s.mega_chain_mode !== 'undefined') {
            window.__rbMegaChainSetting = !!s.mega_chain_mode;
            console.log(`[rig_builder mega-chain] boot setting=${window.__rbMegaChainSetting} (read from /settings)`);
            const cur = window.slopsmith && window.slopsmith.currentSong;
            const filename = cur && cur.filename;
            if (window.__rbMegaChainSetting && filename) {
                triggerBuild(filename, 'settings-ready catch-up');
            } else if (!window.__rbMegaChainSetting && RbMegaChain.isPending()) {
                RbMegaChain.teardown(true).catch(() => {});
            }
        }
        // Cache the chain-input drive so rbApplyChainInputDrive (called
        // from many hooks) doesn't have to refetch /settings every time.
        if (s && typeof s.nam_chain_input_drive === 'number') {
            window.__rbChainInputDrive = s.nam_chain_input_drive;
            console.log(`[rig_builder] chain input drive = ${window.__rbChainInputDrive} (read from /settings)`);
        }
        if (s && typeof s.chain_makeup === 'number') {
            window.__rbChainMakeup = s.chain_makeup;
        }
    }).catch(() => {});

    let _pendingBuildTimer = null;
    let _pendingBuildFile = null;
    let _buildingFile = null;

    function triggerBuild(filename, source) {
        if (!RbMegaChain.settingKnown()) {
            if (filename) {
                RbMegaChain.markPending(filename);
                rbInjectPlayerToneButton();
                console.log('[rig_builder mega-chain] waiting for /settings before build from', source);
            }
            return;
        }
        if (!RbMegaChain.settingOn()) {
            console.log('[rig_builder mega-chain] skip — setting off');
            return;
        }
        if (!filename) {
            console.log('[rig_builder mega-chain] skip — no filename from', source);
            return;
        }
        if (filename === _pendingBuildFile && _pendingBuildTimer) {
            console.log(`[rig_builder mega-chain] duplicate ${source} for ${filename} — build already scheduled`);
            return;
        }
        if (filename === _buildingFile) {
            console.log(`[rig_builder mega-chain] duplicate ${source} for ${filename} — build already running`);
            return;
        }
        if (filename === _lastSeenFile && RbMegaChain.isActive()) {
            console.log(`[rig_builder mega-chain] duplicate ${source} for ${filename} — chain already active`);
            return;
        }
        if (_pendingBuildTimer) {
            clearTimeout(_pendingBuildTimer);
            _pendingBuildTimer = null;
        }
        _lastSeenFile = filename;
        _pendingBuildFile = filename;
        RbMegaChain.markPending(filename);
        rbInjectPlayerToneButton();
        console.log(`[rig_builder mega-chain] song detected via ${source}: ${filename} — scheduling buildForSong in 600 ms`);
        // Give the bundle ~600 ms to inject its #btn-nam etc. so our
        // AMP-off click hits a real button. Also lets the highway
        // stabilise so resolveActiveToneKey reads a sensible value.
        _pendingBuildTimer = setTimeout(() => {
            _pendingBuildTimer = null;
            if (_pendingBuildFile !== filename) return;
            _buildingFile = filename;
            RbMegaChain.buildForSong(filename).then(ok => {
                if (!ok) console.warn(`[rig_builder mega-chain] buildForSong returned false for "${filename}" (no mappings? bundle interfered?)`);
            }).catch(e =>
                console.warn('[rig_builder mega-chain] buildForSong threw:', e))
                .finally(() => {
                    if (_buildingFile === filename) _buildingFile = null;
                    if (!RbMegaChain.isActive() && _pendingBuildFile === filename) _pendingBuildFile = null;
                });
        }, 600);
    }

    let _lastSeenFile = null;

    function hook() {
        if (!window.slopsmith || typeof window.slopsmith.on !== 'function') {
            setTimeout(hook, 500);
            return;
        }
        function installPlaybackLifecycle() {
            rbRegisterPlaybackCapability();
            const playback = rbPlaybackApi();
            if (!playback || window.__rbPlaybackLifecycleInstalled || !window.slopsmith || typeof window.slopsmith.on !== 'function') return;
            window.__rbPlaybackLifecycleInstalled = true;
            window.slopsmith.on('playback:ready', (event) => {
                const target = rbPlaybackTargetFromDetail(event && event.detail || {});
                if (target.settingsKey) window.__rbPlaybackSettingsKey = target.settingsKey;
                if (target.filename) window.__rbPlaybackSettingsFilename = target.filename;
                if (!target.filename) {
                    console.log('[rig_builder mega-chain] playback:ready observed but no local filename is available; waiting for legacy/currentSong fallback');
                    return;
                }
                triggerBuild(target.filename, target.settingsKey ? 'playback:ready event (settingsKey present)' : 'playback:ready event');
            });
            window.slopsmith.on('playback:stopped', () => {
                _lastSeenFile = null;
                _pendingBuildFile = null;
                _buildingFile = null;
                if (_pendingBuildTimer) { clearTimeout(_pendingBuildTimer); _pendingBuildTimer = null; }
                window.__rbPlaybackSettingsKey = '';
                window.__rbPlaybackSettingsFilename = '';
                    if (RbMegaChain.isActive() || RbMegaChain.isPending()) RbMegaChain.teardown(false).catch(() => {});
            });
            window.slopsmith.on('playback:ended', () => {
                _lastSeenFile = null;
                _pendingBuildFile = null;
                _buildingFile = null;
                if (_pendingBuildTimer) { clearTimeout(_pendingBuildTimer); _pendingBuildTimer = null; }
                window.__rbPlaybackSettingsKey = '';
                window.__rbPlaybackSettingsFilename = '';
                    if (RbMegaChain.isActive() || RbMegaChain.isPending()) RbMegaChain.teardown(false).catch(() => {});
            });
        }
        installPlaybackLifecycle();
        if (!rbPlaybackApi() && !window.__rbPlaybackLifecycleReadyListenerPending) {
            window.__rbPlaybackLifecycleReadyListenerPending = true;
            window.addEventListener('slopsmith:capabilities:ready', installPlaybackLifecycle, { once: true });
        }
        window.slopsmith.on('song:loaded', (info) => {
            // Some Slopsmith builds emit song:loaded with no payload (or a
            // payload missing `filename`). Fall back to currentSong before
            // giving up — same info, different source.
            const filename = (info && info.filename)
                || (window.slopsmith.currentSong && window.slopsmith.currentSong.filename);
            triggerBuild(filename, info && info.filename ? 'song:loaded event' : 'song:loaded event (fallback to currentSong)');
        });
        window.slopsmith.on('song:unloaded', () => {
            _lastSeenFile = null;
            _pendingBuildFile = null;
            _buildingFile = null;
            if (_pendingBuildTimer) { clearTimeout(_pendingBuildTimer); _pendingBuildTimer = null; }
            if (RbMegaChain.isActive() || RbMegaChain.isPending()) RbMegaChain.teardown(false).catch(() => {});
        });
        // Catch up on a song that was already loaded when we hooked in:
        // the event has already fired and EventEmitter won't replay it.
        const cur = window.slopsmith.currentSong;
        if (cur && cur.filename && cur.filename !== _lastSeenFile) {
            triggerBuild(cur.filename, 'currentSong catch-up');
        }
        // Belt-and-suspenders: poll every 2 s for currentSong changes the
        // event might miss (or fire while the setting was off and was then
        // flipped on mid-song).
        setInterval(() => {
            if (!RbMegaChain.settingOn()) return;
            if (RbMegaChain.isActive()) return;
            const c = window.slopsmith && window.slopsmith.currentSong;
            const f = c && c.filename;
            if (!f || f === _lastSeenFile) return;
            triggerBuild(f, 'currentSong poll');
        }, 2000);
    }
    hook();
})();

// ── HTML helper ─────────────────────────────────────────────────────

function rbEsc(s) {
    return String(s ?? '').replace(/[&<>"']/g, (c) => ({
        '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'
    }[c]));
}

// ── Init / status ───────────────────────────────────────────────────

async function rbInit() {
    rbEnsureScopedCss();
    rbRegisterCapabilities();
    rbRefreshLibraryProviderSelector().catch(() => null);
    try {
        const r = await fetch(`${window.RB_API}/status`);
        rbState.status = await r.json();
    } catch (e) {
        document.getElementById('rb-status').innerHTML = rbBanner(
            'red', 'Error', `Couldn't load /status: ${rbEsc(e.message)}`
        );
        return;
    }
    rbRenderStatus();
    rbShowTab(rbState.currentTab);
    // Best-effort load known VSTs at init so the per-piece dropdown is
    // populated as soon as the user opens a song. Failure is non-fatal
    // (they'll see "no VSTs scanned yet" hint and can Scan from the panel).
    rbLoadKnownVsts().catch(() => {});
}

function rbBanner(color, title, body) {
    const palette = {
        red: 'bg-red-900/20 border-red-800/30 text-red-400',
        yellow: 'bg-yellow-900/20 border-yellow-800/30 text-yellow-400',
        green: 'bg-green-900/20 border-green-800/30 text-green-400',
        blue: 'bg-blue-900/20 border-blue-800/30 text-blue-400',
    }[color] || 'bg-dark-700/50 border-gray-800/50 text-gray-300';
    return `
        <div class="${palette} border rounded-xl p-4 text-sm">
            <p class="font-semibold mb-1">${rbEsc(title)}</p>
            <p class="text-gray-400">${body}</p>
        </div>`;
}

function rbRenderStatus() {
    const s = rbState.status;
    const el = document.getElementById('rb-status');
    if (!s.rs_to_real_loaded) {
        el.innerHTML = rbBanner(
            'yellow', 'Gear map not found',
            `Missing <code class="bg-dark-800 px-1 rounded">rs_to_real.json</code> (the bundled gear map). Reinstall Rig Builder to restore it.`
        );
        return;
    }
    const cats = s.rs_to_real_by_category || {};
    let apiLine;
    if (s.tone3000_connected) {
        apiLine = `<span class="text-green-400">tone3000 connected${s.tone3000_username ? ' as ' + rbEsc(s.tone3000_username) : ''}</span>`;
    } else if (s.has_tone3000_key) {
        apiLine = s.tone3000_api_works
            ? '<span class="text-green-400">tone3000 API connected</span>'
            : '<span class="text-red-400">tone3000 key invalid</span>';
    } else {
        apiLine = '<span class="text-gray-500">not connected (deep-link mode)</span>';
    }
    // Three states for the game-IR line:
    //   1. JSON loaded + .wav files on disk → green, count of disk-resident IRs
    //   2. JSON loaded but no .wav (fresh install / no the game)  → yellow nudge
    //   3. No JSON at all                                          → just hidden
    let irLine = '';
    if (s.rs_cab_to_ir_loaded && s.rs_irs_on_disk > 0) {
        irLine = `<span class="text-green-400">${s.rs_irs_on_disk} cab IRs on disk</span>`;
    } else if (s.rs_cab_to_ir_loaded) {
        irLine = '<span class="text-yellow-400">Cab IR map loaded, but the .wav files are not on disk.</span>';
    }
    el.innerHTML = `
        <div class="bg-dark-700/50 border border-gray-800/50 rounded-xl p-3 text-xs text-gray-500 flex flex-wrap gap-x-4 gap-y-1">
            <span>${s.rs_to_real_count} gear mapped</span>
            <span>amps: ${cats.amp || 0} · cabs: ${cats.cab || 0} · pedals: ${cats.pedal || 0} · racks: ${cats.rack || 0}</span>
            <span>${irLine}</span>
            <span>${apiLine}</span>
        </div>`;
}

// ── Tabs ────────────────────────────────────────────────────────────

function rbShowTab(name) {
    // Leaving any view tears down an open inline VST editor first so its
    // orphaned native window can't crash the host on the next chain load.
    rbCloseActiveVstEditor();
    rbState.currentTab = name;
    document.getElementById('rb-root')?.classList.toggle('rb-wide', name === 'gear');
    document.querySelectorAll('.rb-tab-panel').forEach(el => el.classList.add('hidden'));
    const panel = document.getElementById(`rb-tab-${name}`);
    if (panel) panel.classList.remove('hidden');

    document.querySelectorAll('.rb-tab').forEach(b => {
        const active = b.dataset.rbTab === name;
        b.classList.toggle('text-white', active);
        b.classList.toggle('border-accent', active);
        b.classList.toggle('text-gray-400', !active);
        b.classList.toggle('border-transparent', !active);
    });

    // Post-restructure: only 4 active tabs. The old dashboard/pending/
    // manage are absorbed — dashboard → settings (top), pending and
    // manage → gear (chip-filtered sub-views).
    if (name === 'gear') rbGearFilter(rbState.currentGearFilter || 'all');
    if (name === 'master') rbLoadMasterChain();
    if (name === 'settings') {
        rbLoadCoverage();        // batch / coverage panel (was dashboard)
        rbLoadSettings();        // tone3000 + prefs
        rbUpdateScanStatus();
    }
}

// Chip filter inside the Gear tab. Toggles between the catalog, the
// pending list, and the file inventory — all three share the same
// top-level tab so the user doesn't ping-pong between two tabs to
// resolve a gear and inspect its file.
function rbGearFilter(filter) {
    if (!['all', 'pending', 'files'].includes(filter)) filter = 'all';
    rbState.currentGearFilter = filter;
    document.querySelectorAll('.rb-gear-view').forEach(v => v.classList.add('hidden'));
    const view = document.getElementById(`rb-gear-view-${filter}`);
    if (view) view.classList.remove('hidden');
    document.querySelectorAll('.rb-gear-filter-btn').forEach(b => {
        const active = b.dataset.rbGearFilter === filter;
        b.classList.toggle('bg-dark-700', active);
        b.classList.toggle('text-white', active);
        b.classList.toggle('text-gray-400', !active);
    });
    if (filter === 'all') rbLoadCatalog();
    else if (filter === 'pending') rbLoadPending();
    else if (filter === 'files') rbLoadManageTab();
}

// ── Manage tab: inventory of downloaded NAM/IR files ────────────────
//
// Lists every file currently on disk under nam_models/* and nam_irs/*,
// grouped by category subdir (amps/pedals/racks/cabs/other). Each row
// shows the file's gear assignment(s), size, and how many presets
// reference it. The user can delete a single file or purge a whole
// bucket from here.

const RB_BUCKET_META = {
    amps:   { label: 'Amps',   icon: '🎛️', color: 'bg-orange-900/20 border-orange-700/40' },
    pedals: { label: 'Pedals', icon: '🎚️', color: 'bg-blue-900/20 border-blue-700/40' },
    racks:  { label: 'Racks',  icon: '🗄️', color: 'bg-purple-900/20 border-purple-700/40' },
    cabs:   { label: 'Cabs',   icon: '📦', color: 'bg-yellow-900/20 border-yellow-700/40' },
    other:  { label: 'Other',  icon: '❓', color: 'bg-gray-700/30 border-gray-600/40' },
};

function rbFmtBytes(n) {
    if (!n) return '0 B';
    if (n < 1024) return `${n} B`;
    if (n < 1024 * 1024) return `${(n / 1024).toFixed(1)} KB`;
    if (n < 1024 * 1024 * 1024) return `${(n / 1024 / 1024).toFixed(1)} MB`;
    return `${(n / 1024 / 1024 / 1024).toFixed(2)} GB`;
}

async function rbLoadManageTab() {
    const summary = document.getElementById('rb-manage-summary');
    const root = document.getElementById('rb-manage-buckets');
    if (!summary || !root) return;
    // If a preload is in flight, latch onto its polling instead of
    // overwriting the live progress line with a "Loading inventory…"
    // flash. The poll will fill `summary` on the next tick.
    try {
        const st = await (await fetch(`${window.RB_API}/preload_status`)).json();
        if (st && st.running) {
            rbPreloadStartPolling();
        }
    } catch (_) { /* non-fatal */ }
    summary.textContent = 'Loading inventory…';
    root.innerHTML = '';
    let data;
    try {
        const r = await fetch(`${window.RB_API}/nam_inventory`);
        data = await r.json();
        if (!r.ok) throw new Error(data.error || r.status);
    } catch (e) {
        summary.textContent = `Inventory failed: ${e.message || e}`;
        summary.className = 'text-red-400 text-sm mt-1';
        return;
    }
    const totals = data.totals || { count: 0, total_bytes: 0 };
    summary.textContent = `${totals.count} files, ${rbFmtBytes(totals.total_bytes)} total on disk`;
    summary.className = 'text-gray-500 text-sm mt-1';

    const buckets = data.buckets || {};
    const order = ['amps', 'pedals', 'racks', 'cabs', 'other'];
    const ordered = [
        ...order.filter(k => k in buckets).map(k => [k, buckets[k]]),
        ...Object.entries(buckets).filter(([k]) => !order.includes(k)),
    ];
    if (!ordered.length) {
        root.innerHTML = `<div class="text-center text-gray-500 py-8">
            No downloaded files yet.</div>`;
        return;
    }
    root.innerHTML = ordered.map(([bucket, b]) => rbRenderManageBucket(bucket, b)).join('');
}

function rbRenderManageBucket(bucket, b) {
    const meta = RB_BUCKET_META[bucket] || RB_BUCKET_META.other;
    const filesHtml = b.files.map(f => rbRenderManageFile(f)).join('');
    return `<div class="rounded-xl border ${meta.color}">
        <div class="flex items-center justify-between p-4 border-b border-gray-800/40">
            <div class="flex items-center gap-3">
                <span class="text-2xl">${meta.icon}</span>
                <div>
                    <div class="text-white font-semibold">${meta.label}</div>
                    <div class="text-xs text-gray-500">
                        ${b.count} files · ${rbFmtBytes(b.total_bytes)}
                    </div>
                </div>
            </div>
            <button onclick="rbPurgeNams({bucket: ${JSON.stringify(bucket)}}, ${JSON.stringify(meta.label + ' (' + b.count + ' files)')})"
                    class="bg-red-900/20 hover:bg-red-900/50 text-red-300 border border-red-800/30 px-2.5 py-1 rounded text-xs transition">
                🗑 Delete all
            </button>
        </div>
        <div class="divide-y divide-gray-800/30">${filesHtml}</div>
    </div>`;
}

function rbRenderManageFile(f) {
    const gears = (f.real_names && f.real_names.length)
        ? f.real_names.map(rbEsc).join(', ')
        : '<span class="text-gray-600 italic">orphan</span>';
    const presetHint = f.preset_count
        ? `<span class="text-xs text-gray-500">used by ${f.preset_count} preset${f.preset_count === 1 ? '' : 's'}</span>`
        : '<span class="text-xs text-gray-600 italic">no preset references this file</span>';
    const tone3000 = (f.tone3000_ids && f.tone3000_ids.length)
        ? `<a href="https://www.tone3000.com/tones/${f.tone3000_ids[0]}" target="_blank"
              class="text-xs text-cyan-500 hover:text-cyan-300">tone ${f.tone3000_ids[0]}</a>`
        : '';
    const orphanClass = f.orphan ? 'bg-amber-900/10' : '';
    return `<div class="flex items-center justify-between gap-3 p-3 hover:bg-gray-800/30 ${orphanClass}">
        <div class="min-w-0 flex-1">
            <div class="text-sm text-gray-200 truncate" title="${rbEsc(f.name)}">${gears}</div>
            <div class="text-xs text-gray-500 truncate font-mono" title="${rbEsc(f.name)}">${rbEsc(f.name)}</div>
            <div class="flex items-center gap-3 mt-0.5">
                <span class="text-xs text-gray-500">${rbFmtBytes(f.size_bytes)}</span>
                ${presetHint}
                ${tone3000}
            </div>
        </div>
        <button onclick="rbDeleteNamFile(${JSON.stringify(f.name)})"
                class="bg-red-900/20 hover:bg-red-900/50 text-red-300 border border-red-800/30 px-2.5 py-1 rounded text-xs transition shrink-0">
            🗑
        </button>
    </div>`;
}

async function rbDeleteNamFile(path) {
    if (!confirm(`Delete this file?\n\n${path}\n\nGears using it will revert to Pending. (The download can be re-fetched anytime.)`)) {
        return;
    }
    try {
        const r = await fetch(`${window.RB_API}/nam_file?path=${encodeURIComponent(path)}`, {
            method: 'DELETE',
        });
        const d = await r.json();
        if (!r.ok) throw new Error(d.error || r.status);
        rbLoadManageTab();
    } catch (e) {
        alert(`Delete failed: ${e.message || e}`);
    }
}

async function rbPreloadCuratedVariants() {
    if (!confirm('One-click curate:\n\n'
               + '1. Rename any legacy cryptic filenames to readable titles\n'
               + '2. Download every curated amp variant from rs_to_real.json\n'
               + '   (files already on disk skip the network)\n'
               + '3. Wire each variant to the preset rows that need it\n\n'
               + 'Live progress shown below. Continue?')) {
        return;
    }
    let jobId = null;
    try {
        jobId = await rbStartCapabilityJob('rig-builder.curated-preload', 'Download curated Rig Builder variants', {
            logicalJobKey: 'rig-builder.curated-preload',
            targetKind: 'curated-captures',
            targetRef: 'all-curated-variants',
        });
        rbState._preloadJobId = jobId;
        const r = await fetch(`${RB_API}/preload_curated_variants`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({}),
        });
        const d = await r.json();
        if (!r.ok) throw new Error(d.error || r.status);
        if (d.started === false) {
            rbFinishCapabilityJob(jobId, true, 'Curated preload was already running');
            alert('Already running — current progress is shown live in the Manage tab.');
            rbPreloadStartPolling();
            return;
        }
        rbPreloadStartPolling();
    } catch (e) {
        rbFinishCapabilityJob(jobId, false, e.message || e, 'external-dependency');
        alert(`Could not start preload: ${e.message || e}`);
    }
}

// Live progress polling for the curated-variants preload. Polls the
// backend's /preload_status every 500ms while a run is in flight,
// stops automatically when `running` flips to false, and surfaces the
// final summary in an alert.
let _rbPreloadPollTimer = null;

function rbPreloadStartPolling() {
    if (_rbPreloadPollTimer) return;   // already polling
    rbPreloadPollOnce();
    _rbPreloadPollTimer = setInterval(rbPreloadPollOnce, 500);
}

function rbPreloadStopPolling() {
    if (_rbPreloadPollTimer) {
        clearInterval(_rbPreloadPollTimer);
        _rbPreloadPollTimer = null;
    }
}

async function rbPreloadPollOnce() {
    let st;
    try {
        st = await (await fetch(`${window.RB_API}/preload_status`)).json();
    } catch (e) {
        return;
    }
    const summary = document.getElementById('rb-manage-summary');
    const total = st.total || 0;
    const done = st.done || 0;
    const pct = total > 0 ? Math.round((done / total) * 100) : 0;
    if (st.running) {
        rbUpdateCapabilityJob(rbState._preloadJobId, { mode: total > 0 ? 'determinate' : 'indeterminate', percent: pct, step: 'download', message: `${done} / ${total}` });
        if (summary) {
            summary.className = 'text-emerald-300 text-sm mt-1';
            summary.innerHTML = `Downloading ${done} / ${total} (${pct}%) — `
                              + `<span class="text-gray-400">${rbEsc(st.current || '…')}</span>`;
        }
    } else if (st.started_at) {
        // Finished. Stop polling, refresh manage list, show final tally.
        rbPreloadStopPolling();
        rbLoadManageTab();
        const lines = [
            `${st.downloaded} newly downloaded`,
            `${st.already_present} already cached`,
        ];
        if ((st.failed || []).length) {
            lines.push(`${st.failed.length} failed:\n  ` + st.failed.slice(0, 5).join('\n  '));
        }
        if ((st.errors || []).length) {
            lines.push(`${st.errors.length} errors:\n  ` + st.errors.slice(0, 5).join('\n  '));
        }
        const elapsed = ((st.finished_at - st.started_at) || 0).toFixed(1);
        lines.push(`\nElapsed: ${elapsed}s`);
        rbFinishCapabilityJob(rbState._preloadJobId, !(st.failed || []).length && !(st.errors || []).length, `Curated preload finished in ${elapsed}s`, (st.failed || []).length ? 'external-dependency' : 'provider-failure');
        rbState._preloadJobId = null;
        alert('Done.\n\n' + lines.join('\n'));
    }
}

async function rbPurgeNams(filter, label) {
    if (!confirm(`Purge ${label}?\n\nThis deletes the file(s) AND reverts every gear using them to Pending. Cannot be undone.`)) {
        return;
    }
    let jobId = null;
    try {
        jobId = await rbStartCapabilityJob('rig-builder.purge-library', 'Purge Rig Builder downloaded captures', {
            logicalJobKey: `rig-builder.purge-${rbSafeCapabilityId(label, 'library')}`,
            targetKind: 'capture-library',
            targetRef: rbSafeCapabilityId(label, 'library'),
        });
        const r = await fetch(`${RB_API}/nam_purge`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ ...filter, confirm: true }),
        });
        const d = await r.json();
        if (!r.ok) throw new Error(d.error || r.status);
        rbLoadManageTab();
        rbFinishCapabilityJob(jobId, true, `Purged ${d.deleted_count || 0} cached capture files`);
        if ((d.errors || []).length) {
            alert(`Purged ${d.deleted_count} files. ${d.errors.length} errors:\n` +
                  d.errors.slice(0, 5).join('\n'));
        }
    } catch (e) {
        rbFinishCapabilityJob(jobId, false, e.message || e, 'storage');
        alert(`Purge failed: ${e.message || e}`);
    }
}

// ── Dashboard: coverage stats ──────────────────────────────────────

async function rbLoadCoverage() {
    const el = document.getElementById('rb-gear-coverage');
    if (!el) return;   // coverage card removed from Settings
    const s = rbState.status;
    if (!s || !s.rs_to_real_loaded) {
        el.innerHTML = '<span class="text-yellow-500">rs_to_real.json no cargado.</span>';
        return;
    }
    const cats = s.rs_to_real_by_category || {};
    el.innerHTML = Object.entries(cats).sort()
        .map(([cat, n]) => `<div class="flex justify-between border-b border-gray-800/50 py-1">
            <span class="capitalize">${rbEsc(cat)}</span><span class="text-gray-500">${n}</span></div>`)
        .join('');
}

// ── Dashboard: batch ───────────────────────────────────────────────

// "Reset to factory": re-resolve EVERY tone to its default mapping, ignoring
// assigned_mode (manual swaps are discarded). Destructive → confirm first.
async function rbFactoryReset() {
    if (!confirm(
        'Reset ALL tones to the factory mapping?\n\n' +
        'Every piece is re-resolved to its default bundled VST / capture / IR. '
        + 'Your manual gear swaps are DISCARDED (assigned_mode is ignored). '
        + 'Per-tone bypass is kept. This cannot be undone.'
    )) return;
    await rbStartBatch('factory');
}

async function rbStartBatch(mode) {
    mode = mode || 'all';
    const btns = document.querySelectorAll('.rb-batch-btn');
    btns.forEach(b => { b.disabled = true; });
    let jobId = null;
    try {
        jobId = await rbStartCapabilityJob('rig-builder.batch-map', `Rig Builder batch map (${mode})`, {
            logicalJobKey: `rig-builder.batch-${rbSafeCapabilityId(mode, 'all')}`,
            targetKind: 'song-library',
            targetRef: rbSafeCapabilityId(mode, 'all'),
        });
        rbState._batchJobId = jobId;
        const r = await fetch(`${RB_API}/batch_all`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ mode }),
        });
        if (!r.ok) {
            const err = await r.json().catch(() => ({}));
            rbFinishCapabilityJob(jobId, false, err.error || `HTTP ${r.status}`, 'provider-failure');
            alert(`Couldn't start batch: ${err.error || r.status}`);
            return;
        }
    } catch (e) {
        rbFinishCapabilityJob(jobId, false, e.message || e, 'provider-failure');
        alert(`Couldn't start batch: ${e.message || e}`);
        return;
    } finally {
        btns.forEach(b => { b.disabled = false; });
    }
    document.getElementById('rb-batch-progress').classList.remove('hidden');
    if (rbState.batchPoll) clearInterval(rbState.batchPoll);
    rbState.batchPoll = setInterval(rbPollBatch, 1000);
    rbPollBatch();
}

async function rbPollBatch() {
    let st;
    try {
        const r = await fetch(`${window.RB_API}/batch_status`);
        st = await r.json();
    } catch (e) {
        return;
    }
    const pct = st.total ? Math.round(100 * st.progress / st.total) : 0;
    rbUpdateCapabilityJob(rbState._batchJobId, { mode: st.total ? 'determinate' : 'indeterminate', percent: pct, step: 'mapping', message: `${st.progress || 0} / ${st.total || 0}` });
    document.getElementById('rb-batch-pct').textContent = `${pct}%`;
    document.getElementById('rb-batch-count').textContent = `${st.progress} / ${st.total}`;
    document.getElementById('rb-batch-bar').style.width = `${pct}%`;

    const assignedEl = document.getElementById('rb-batch-assigned');
    if (st.assigned) {
        assignedEl.textContent = `${st.assigned} tones persisted`;
        assignedEl.classList.remove('hidden');
    }

    const log = document.getElementById('rb-batch-log');
    log.textContent = (st.log || []).join('\n');
    log.scrollTop = log.scrollHeight;

    if (!st.running && rbState.batchPoll) {
        clearInterval(rbState.batchPoll);
        rbState.batchPoll = null;
        rbFinishCapabilityJob(rbState._batchJobId, true, `${st.assigned || 0} tones persisted`);
        rbState._batchJobId = null;
    }
}

// ── Pending ────────────────────────────────────────────────────────

async function rbLoadPending() {
    const el = document.getElementById('rb-pending-list');
    el.innerHTML = '<span class="text-gray-500">Loading…</span>';
    let data;
    try {
        const r = await fetch(`${window.RB_API}/coverage`);
        data = await r.json();
    } catch (e) {
        el.innerHTML = `<span class="text-red-400">Error: ${rbEsc(e.message)}</span>`;
        return;
    }
    const pending = (data.items || []).filter(i => i.pending_chain_slots > 0);
    // Update the chip badge so the user sees the count without leaving the
    // current sub-view. Hidden when zero so it doesn't add visual noise.
    const badge = document.getElementById('rb-gear-pending-badge');
    if (badge) {
        if (pending.length) {
            badge.textContent = pending.length;
            badge.classList.remove('hidden');
        } else {
            badge.classList.add('hidden');
        }
    }
    if (!pending.length) {
        el.innerHTML = '<p class="text-gray-500">No pending gear. (Have you run the batch yet?)</p>';
        return;
    }
    el.innerHTML = pending.map(it => `
        <div class="flex items-center justify-between bg-dark-700/50 border border-gray-800/50 rounded-lg px-3 py-2">
            <div class="min-w-0">
                <div class="text-gray-200">${rbEsc(it.name)}</div>
                <div class="text-xs text-gray-500">
                    ${rbEsc(it.rs_gear)} · <span class="capitalize">${rbEsc(it.category)}</span> ·
                    ${it.pending_chain_slots}/${it.total_chain_slots} pending
                </div>
            </div>
            <button onclick="rbOpenSuggest('${rbEsc(it.rs_gear)}')"
                    class="bg-accent hover:bg-accent/80 text-white px-3 py-1 rounded-lg text-xs transition">
                Search
            </button>
        </div>
    `).join('');
}

// ── Suggest modal (manual search per gear) ─────────────────────────

async function rbOpenSuggest(rsGear, queryOverride = '', gearsOverride = '') {
    // Build URL with optional overrides so the same modal can be
    // re-invoked when the user edits the query and re-searches.
    const qs = new URLSearchParams({ rs_gear: rsGear });
    if (queryOverride) qs.set('query_override', queryOverride);
    if (gearsOverride) qs.set('gears_override', gearsOverride);
    let data;
    try {
        const r = await fetch(`${window.RB_API}/search?${qs}`);
        data = await r.json();
    } catch (e) {
        alert(`Search failed: ${e.message}`);
        return;
    }

    // Remove any existing modal so a re-search replaces the previous
    // open one instead of stacking new instances on top.
    document.querySelectorAll('.rb-suggest-modal').forEach(m => m.remove());

    const modal = document.createElement('div');
    modal.className = 'rb-suggest-modal fixed inset-0 z-50 bg-black/70 flex items-center justify-center p-6';
    modal.onclick = (e) => { if (e.target === modal) modal.remove(); };
    let candidatesHtml = '';
    if (!data.has_api_access) {
        candidatesHtml = `<p class="text-gray-500 text-sm mb-4">
            No API key — use the deep-link to search tone3000.com manually and then upload the file from the "By song" tab.
        </p>`;
    } else if (!data.candidates.length) {
        candidatesHtml = `
            <div class="bg-yellow-900/15 border border-yellow-800/30 rounded-lg p-3 mb-4 text-sm">
                <p class="text-yellow-400 font-semibold mb-1">tone3000 returned no candidates for this search</p>
                <p class="text-gray-400">The query probably doesn't represent a real amp/pedal. Edit the query above with the brand/model you think this gear is modeled on (e.g. <code class="bg-dark-800 px-1 rounded">Ampeg SVT</code> or <code class="bg-dark-800 px-1 rounded">Markbass Little Mark</code>) and click "Search again".</p>
            </div>`;
    } else {
        candidatesHtml = data.candidates.map(c => {
            const photo = (c.images && c.images[0])
                ? `<img src="${rbEsc(c.images[0])}" alt="" loading="lazy" style="width:48px;height:48px;object-fit:cover" class="w-12 h-12 rounded object-cover bg-dark-900 flex-shrink-0" onerror="this.style.visibility='hidden'">`
                : `<div class="w-12 h-12 rounded bg-dark-900 flex items-center justify-center text-gray-700 text-[10px] flex-shrink-0">no photo</div>`;
            return `
            <div class="bg-dark-800 border border-gray-800 rounded-lg p-3 flex items-center gap-3">
                ${photo}
                <a href="${rbEsc(c.url)}" target="_blank" class="flex-1 min-w-0 hover:text-white transition">
                    <div class="text-gray-200 text-sm truncate">${rbEsc(c.title)}</div>
                    <div class="text-xs text-gray-500">
                        license: ${rbEsc(c.license || 'unknown')} · ${c.downloads_count || 0} dl · ${c.favorites_count || 0} ♥
                    </div>
                </a>
                <button onclick="rbAuditionCandidate(this, '${rbEsc(data.rs_gear)}', ${c.id})"
                        title="Download and listen" class="bg-dark-600 hover:bg-dark-500 text-gray-200 text-xs px-2.5 py-1.5 rounded flex-shrink-0">▶</button>
                <button onclick="rbDownloadForGear(this, '${rbEsc(data.rs_gear)}', ${c.id})"
                        class="bg-green-700 hover:bg-green-600 text-white text-xs px-3 py-1.5 rounded whitespace-nowrap flex-shrink-0">
                    Download and assign
                </button>
            </div>`;
        }).join('');
    }

    modal.innerHTML = `
        <div class="bg-dark-700 border border-gray-800 rounded-xl p-6 max-w-2xl w-full max-h-[80vh] overflow-auto">
            <div class="flex items-start justify-between mb-4">
                <div>
                    <h3 class="text-white font-semibold">${rbEsc(data.rs_gear)}</h3>
                    <p class="text-gray-500 text-xs">platform: ${rbEsc(data.platform)}</p>
                </div>
                <button onclick="this.closest('.fixed').remove()" class="text-gray-500 hover:text-white">✕</button>
            </div>

            <!-- Editable query controls. The user can override what
                 the plugin sends to tone3000 — useful when the
                 auto-generated query (a game pseudonym) doesn't
                 match anything real. "Search" re-runs in place;
                 "Save override" persists the discovery to
                 rs_to_real.json so future batches benefit. -->
            <div class="bg-dark-800 border border-gray-800/50 rounded-lg p-3 mb-4 flex gap-2 flex-wrap items-center">
                <div class="flex-1 min-w-0">
                    <label class="text-xs text-gray-500 block mb-1">tone3000 query</label>
                    <input type="text" id="rb-suggest-query" value="${rbEsc(data.query)}"
                           class="w-full bg-dark-900 border border-gray-800 rounded px-2 py-1.5 text-sm text-gray-200">
                </div>
                <div class="w-32">
                    <label class="text-xs text-gray-500 block mb-1">gears</label>
                    <select id="rb-suggest-gears" class="w-full bg-dark-900 border border-gray-800 rounded px-2 py-1.5 text-sm text-gray-200">
                        ${['amp','pedal','outboard','ir','full-rig'].map(g =>
                            `<option value="${g}"${data.gears===g?' selected':''}>${g}</option>`
                        ).join('')}
                    </select>
                </div>
                <div class="flex gap-2 w-full">
                    <button onclick="rbSuggestRerun('${rbEsc(data.rs_gear)}')"
                            class="bg-accent hover:bg-accent/80 text-white px-3 py-1.5 rounded text-xs transition">
                        Search again
                    </button>
                    <button onclick="rbSuggestSaveOverride('${rbEsc(data.rs_gear)}')"
                            class="bg-dark-600 hover:bg-dark-500 text-gray-200 px-3 py-1.5 rounded text-xs transition">
                        Save override to rs_to_real.json
                    </button>
                </div>
            </div>

            <div class="space-y-2 mb-4">${candidatesHtml}</div>
            <a href="${rbEsc(data.deep_link)}" target="_blank"
               class="inline-block bg-accent hover:bg-accent/80 text-white px-4 py-2 rounded-lg text-sm transition">
                Open tone3000.com with these filters ↗
            </a>
        </div>`;
    (document.getElementById('rb-root') || document.body).appendChild(modal);
}

function rbSuggestRerun(rsGear) {
    const q = document.getElementById('rb-suggest-query').value.trim();
    const g = document.getElementById('rb-suggest-gears').value.trim();
    rbOpenSuggest(rsGear, q, g);
}

async function rbSuggestSaveOverride(rsGear) {
    const q = document.getElementById('rb-suggest-query').value.trim();
    const g = document.getElementById('rb-suggest-gears').value.trim();
    if (!q) { alert('Query required'); return; }
    try {
        const r = await fetch(`${window.RB_API}/override_query`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ rs_gear: rsGear, query: q, gears: g }),
        });
        if (!r.ok) {
            const err = await r.json().catch(() => ({}));
            alert(`Save failed: ${err.error || r.status}`);
            return;
        }
        alert(`Override saved for ${rsGear}. Future searches and the batch will use "${q}".`);
        // Re-run with the new persisted query to show updated candidates.
        rbOpenSuggest(rsGear, q, g);
    } catch (e) {
        alert(`Error: ${e.message}`);
    }
}

// ── By song ────────────────────────────────────────────────────────

// Show / hide of the song list panel above the editor. Hidden after a
// song is opened so the editor takes the whole tab; reappears as soon
// as the user touches the search box (focus or input).
function rbHideSongList() {
    const el = document.getElementById('rb-song-list');
    if (el) el.classList.add('hidden');
}

function rbShowSongList() {
    const el = document.getElementById('rb-song-list');
    if (el) el.classList.remove('hidden');
}

// Called from the search input's oninput. Shows the list right away
// (the user just started typing — they expect to see candidates) and
// debounces the provider-backed library query so we don't spam the backend
// on every keystroke. 250 ms is the sweet spot between "feels live" and
// "doesn't fire 8 fetches for a single word".
let _rbSongSearchDebounce = null;
function rbOnSongSearchInput() {
    rbShowSongList();
    if (_rbSongSearchDebounce) clearTimeout(_rbSongSearchDebounce);
    _rbSongSearchDebounce = setTimeout(() => {
        _rbSongSearchDebounce = null;
        rbListSongs();
    }, 250);
}

async function rbListSongs() {
    const q = document.getElementById('rb-song-search').value.trim();
    const el = document.getElementById('rb-song-list');
    if (!_rbLibraryProvidersLoaded) {
        await rbRefreshLibraryProviderSelector();
    }
    const providerId = rbActiveLibraryProviderId();
    const params = new URLSearchParams({ q, page: '0', size: '50', sort: 'artist', provider: providerId });
    let data;
    try {
        const r = await fetch(`/api/library?${params}`);
        data = await r.json().catch(() => ({}));
        if (!r.ok) throw new Error(data.detail || data.error || `HTTP ${r.status}`);
    } catch (e) {
        el.innerHTML = `<p class="text-red-400 text-sm">Library search failed: ${rbEsc(e.message || e)}</p>`;
        return;
    }
    const songs = Array.isArray(data.songs) ? data.songs : [];
    if (!songs.length) {
        el.innerHTML = '<p class="text-gray-500 text-sm">No matches</p>';
        return;
    }
    el.innerHTML = songs.map(song => rbRenderLibrarySongListItem(song, providerId)).join('');
}

function rbRenderLibrarySongListItem(song, fallbackProviderId) {
    const providerId = rbLibraryProviderIdForSong(song, fallbackProviderId);
    const provider = rbLibraryProviderById(providerId);
    const localFilename = rbLibraryLocalFilename(song, providerId);
    const songId = rbLibrarySongId(song);
    const title = rbLibrarySongTitle(song, providerId);
    const artist = (song && song.artist) || '';
    const album = (song && song.album) || '';
    const year = (song && song.year) || '';
    const format = ((song && song.format) || '').toUpperCase();
    const canSync = !localFilename && !rbIsLocalLibraryProvider(providerId) && rbLibraryProviderSupports(providerId, 'song.sync');
    const interactive = !!localFilename || canSync;
    const textColor = interactive ? 'text-gray-300' : 'text-gray-500';
    const cursor = interactive ? 'cursor-pointer' : 'cursor-not-allowed';
    const sourceLabel = providerId === 'local' ? '' : (provider && provider.label) || providerId;
    const yearTag = year ? ` <span class="text-gray-600">(${rbEsc(year)})</span>` : '';
    const secondary = [artist || '(unknown artist)', album].filter(Boolean).join(' / ');
    const syncLabel = localFilename
        ? ''
        : canSync
            ? '<span data-rb-sync-status class="text-xs text-blue-300 ml-2">sync on open</span>'
            : '<span data-rb-sync-status class="text-xs text-yellow-300 ml-2">remote only</span>';
    return `
        <div onclick="rbOpenLibrarySongFromList(this)"
             data-rb-library-song="1"
             data-rb-provider="${rbEsc(providerId)}"
             data-rb-song-id="${rbEsc(songId)}"
             data-rb-filename="${rbEsc(localFilename)}"
             data-rb-can-sync="${canSync ? '1' : '0'}"
             class="${cursor} hover:bg-dark-700/50 px-3 py-2 rounded text-sm ${textColor} flex items-center gap-3">
            <div class="flex-1 min-w-0">
                <div class="truncate">${rbEsc(title)}${yearTag}</div>
                <div class="text-xs text-gray-500 truncate" title="${rbEsc(rbLibraryDisplayFilename(song, providerId))}">${rbEsc(secondary)}</div>
            </div>
            <div class="flex items-center gap-2 shrink-0">
                ${format ? `<span class="text-[10px] text-gray-500 border border-gray-800 rounded px-1.5 py-0.5">${rbEsc(format)}</span>` : ''}
                ${sourceLabel ? `<span class="text-[10px] text-cyan-300 border border-cyan-900/50 rounded px-1.5 py-0.5">${rbEsc(sourceLabel)}</span>` : ''}
                ${syncLabel}
            </div>
        </div>`;
}

// Seed each piece's _bypassed (the UI/persist flag) from the persisted
// `bypassed` returned by /song, so the Bypass buttons reflect what was
// saved. MUST run after every /song fetch (initial load AND the
// auto-download re-fetch) or a re-render shows bypass as off.
function rbSeedBypass(data) {
    if (data && Array.isArray(data.tones)) {
        data.tones.forEach(t => (t.chain || []).forEach(p => { p._bypassed = !!p.bypassed; }));
    }
}

async function rbLoadSongTones(filename) {
    // Close any inline VST editor (and its native window) before loading a new
    // song — otherwise the editor's slot gets cleared underneath it and crashes.
    await rbCloseActiveVstEditor();
    const el = document.getElementById('rb-song-tones');
    rbState.currentSongFile = filename;
    el.innerHTML = '<p class="text-gray-500">Loading…</p>';

    // Try once. If the server signals cloud_only, fire cloud_loader's
    // materialize endpoint and retry once the download finishes.
    let data = await rbFetchSong(filename);
    if (data && data.error === 'cloud_only') {
        el.innerHTML = `<p class="text-blue-400">☁ Downloading "${rbEsc(filename)}" from Google Drive…</p>`;
        const ok = await rbMaterializeFromCloud(filename, el);
        if (!ok) return;
        data = await rbFetchSong(filename);
    }
    if (!data) {
        el.innerHTML = '<p class="text-red-400">Network error loading the song</p>';
        return;
    }
    if (data.error) {
        el.innerHTML = `<p class="text-red-400">${rbEsc(data.error)}</p>`;
        return;
    }
    // Re-rendering the tone list discards the old "Stop" buttons, so
    // stop any in-flight preview to keep engine + UI state consistent.
    if (rbState.listeningTone !== null) {
        rbStopPreview();
    }
    rbState.songTones = data;
    rbSeedBypass(data);
    // Fresh song = fresh selection (always start at tone 0, piece 0).
    rbResetEditorState();
    try {
        el.innerHTML = rbRenderSongEditor(data, filename);
    } catch (e) {
        // Never leave the panel stuck on "Loading…" if a render throws.
        console.error('[rig_builder] render of tones failed', e);
        el.innerHTML = `<p class="text-red-400">Error rendering tones: ${rbEsc(e.message)}</p>`;
        return;
    }
    // Hide the song list now that we're inside a specific song. Typing
    // in the search box (or focusing it) brings the list back.
    rbHideSongList();
    // Auto-scroll the editor into view so picking a song from the (long)
    // song list doesn't leave the user staring at the same list — they
    // expect to land in the editor immediately. requestAnimationFrame
    // lets the layout settle before measuring.
    requestAnimationFrame(() => {
        try { el.scrollIntoView({ behavior: 'smooth', block: 'start' }); } catch (_) {}
    });

    // Auto-download trigger: if the user has an API key and any chain
    // piece is unassigned, kick off the song-scoped download flow.
    // The backend skips pieces that already have a file, so re-opening
    // a song with everything mapped is a near-instant no-op.
    if (rbState.status && rbState.status.has_tone3000_key && rbState.status.tone3000_api_works) {
        const unmapped = data.tones.flatMap(t => t.chain).filter(p => !(p.assigned && p.assigned.file)).length;
        if (unmapped > 0) {
            rbAutoDownloadSong(filename, unmapped, el);
        }
    }
}

// Inserts a status banner above the rendered chain and fires the
// backend auto-download. When the backend returns, refreshes the chain
// so the new file assignments are visible without the user having to
// click anything.
async function rbAutoDownloadSong(filename, unmappedCount, container) {
    rbRecordJobsBridgeHit('jobs.legacy-backend-route', 'rig-builder.auto-download-song', 'rig-builder.auto-download-song', 'Song-scoped auto-download is delegated to the plugin backend after the user opens a song.');
    rbRecordPrivilegedOutcome('service.download', 'handled', 'Song-scoped tone3000 auto-download delegated to Rig Builder backend');
    const banner = document.createElement('div');
    banner.className = 'rb-autodl-banner bg-blue-900/15 border border-blue-800/30 rounded-lg p-3 text-sm mb-4';
    banner.innerHTML = `<p class="text-blue-400">⬇ Auto-downloading ${unmappedCount} unassigned piece(s) from tone3000…</p>`;
    container.prepend(banner);
    try {
        const r = await fetch(`${window.RB_API}/auto_download_song`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ filename }),
        });
        const result = await r.json();
        if (!r.ok) {
            banner.innerHTML = `<p class="text-red-400">Auto-download failed: ${rbEsc(result.error || r.status)}</p>`;
            return;
        }
        const parts = [];
        if (result.downloaded) parts.push(`${result.downloaded} downloaded`);
        if (result.rs_ir_used) parts.push(`${result.rs_ir_used} IRs`);
        if (result.skipped_assigned) parts.push(`${result.skipped_assigned} reused`);
        if (result.skipped_no_candidate) parts.push(`${result.skipped_no_candidate} unmatched`);
        if (result.failed) parts.push(`${result.failed} failed`);
        banner.innerHTML = `<p class="text-green-400">✓ Auto-download done — ${parts.join(' · ') || 'nothing to do'}</p>`;

        // Refresh chain so the new file assignments are visible.
        const refreshed = await rbFetchSong(filename);
        if (refreshed && !refreshed.error) {
            rbState.songTones = refreshed;
            rbSeedBypass(refreshed);   // re-seed bypass after the re-fetch (was the bug)
            // Wipe out the previous chain HTML and re-render under the banner.
            const stillBanner = banner.cloneNode(true);
            container.innerHTML = '';
            container.appendChild(stillBanner);
            const wrap = document.createElement('div');
            wrap.innerHTML = rbRenderSongEditor(refreshed, filename);
            // Append every top-level child the editor returned (it's a
            // single root <div> for now, but be defensive).
            while (wrap.firstChild) container.appendChild(wrap.firstChild);
        }
    } catch (e) {
        banner.innerHTML = `<p class="text-red-400">Auto-download error: ${rbEsc(e.message)}</p>`;
    }
}

async function rbFetchSong(filename) {
    try {
        const r = await fetch(`${window.RB_API}/song/${encodeURIComponent(filename)}`);
        return await r.json();
    } catch (e) {
        return null;
    }
}

// Hits cloud_loader's materialize endpoint to pull a 0-byte stub down
// from Drive. Updates the inline status as it goes; returns false on
// failure so the caller can leave a clear message in place.
async function rbMaterializeFromCloud(filename, statusEl) {
    try {
        const url = `/api/cloud_loader/materialize?filename=${encodeURIComponent(filename)}`;
        const r = await fetch(url, { method: 'POST' });
        if (!r.ok) {
            const text = await r.text().catch(() => '');
            statusEl.innerHTML = `
                <div class="bg-yellow-900/20 border border-yellow-800/30 rounded-lg p-3 text-sm">
                    <p class="text-yellow-400 font-semibold mb-1">Could not materialize from Drive</p>
                    <p class="text-gray-400">${rbEsc(text || `HTTP ${r.status}`)}</p>
                    <p class="text-gray-500 text-xs mt-2">Make sure the <code class="bg-dark-800 px-1 rounded">cloud_loader</code> plugin is authenticated.</p>
                </div>`;
            return false;
        }
        const body = await r.json();
        statusEl.innerHTML = `<p class="text-blue-400">☁ Downloaded (${body.size_mb || '?'} MB) — parsing tones…</p>`;
        return true;
    } catch (e) {
        statusEl.innerHTML = `<p class="text-red-400">Error: ${rbEsc(e.message)}</p>`;
        return false;
    }
}

// ── Song editor v2: tone tabs + horizontal chain strip + detail panel ──
//
// The old layout stacked every tone vertically with each chain piece in
// a 2-col grid. That was ~5 screens of scrolling for a song with 3
// tones x 6 pieces, and every action button (Bypass, Swap, file upload,
// VST edit, Suggest, etc.) lived ON the card → visual noise.
//
// v2 layout:
//   ┌ Tone tabs (one per tone in the .psarc) ─────────────┐
//   │ [Clean*] [Crunch ] [Lead]              ✎ edited     │
//   ├ Chain strip (signal flow L→R, photo cards) ─────────┤
//   │ ◀ [photo] [photo] [photo*] [photo] [photo] ▶       │
//   ├ Detail editor (the SELECTED piece) ─────────────────┤
//   │  big photo │ name • type                    [Bypass]│
//   │            │ Gain: [clean][crunch][dist] ↺ auto     │
//   │            │ 🔁 Swap…   ⬇ Replace file              │
//   │            │ ⬅ position ➡   ✗ Remove                │
//   │            │ the game knobs: Rate=50 …             │
//   ├ Footer ─────────────────────────────────────────────┤
//   │ ＋ Add piece                       ▶ Listen  💾 Save │
//   └─────────────────────────────────────────────────────┘
//
// Photos come from RB_API/gear_photo/<rs_gear> served by routes.py
// (the game art extracted via extract_gear_photos.py). Missing photos
// fall back to a small text placeholder via onerror.
//
// Selection state lives on rbState.editor; it's cleared when a new
// song is loaded so opening a different .psarc always starts on tone 0
// piece 0.

function rbEnsureEditorState() {
    rbState.editor = rbState.editor || { selectedToneIdx: 0, selectedPIdx: 0 };
    return rbState.editor;
}

function rbResetEditorState() {
    rbState.editor = { selectedToneIdx: 0, selectedPIdx: 0 };
}

function rbRenderSongEditor(data, filename) {
    const ed = rbEnsureEditorState();
    if (!data || !Array.isArray(data.tones) || data.tones.length === 0) {
        return '<p class="text-gray-500 text-sm">No tones in this song.</p>';
    }
    // Clamp selection so a chain shrink (remove piece) doesn't leave a
    // dangling selected index that re-renders blank.
    if (ed.selectedToneIdx >= data.tones.length) ed.selectedToneIdx = 0;
    const tone = data.tones[ed.selectedToneIdx];
    const chainLen = (tone.chain || []).length;
    if (ed.selectedPIdx >= chainLen) ed.selectedPIdx = Math.max(0, chainLen - 1);
    return `
        <div class="bg-dark-700/40 border border-gray-800/50 rounded-xl overflow-hidden">
            ${rbRenderToneTabs(data.tones, ed.selectedToneIdx, filename)}
            ${rbRenderToneHeader(tone, ed.selectedToneIdx, filename)}
            ${rbRenderChainStrip(tone, ed.selectedToneIdx, ed.selectedPIdx)}
            <div id="rb-detail-panel">${
                chainLen > 0
                    ? rbRenderPieceEditor(tone.chain[ed.selectedPIdx], ed.selectedToneIdx, ed.selectedPIdx, filename)
                    : '<p class="text-gray-500 text-sm p-4">No pieces in this tone. Add one below.</p>'
            }</div>
            ${rbRenderEditorFooter(ed.selectedToneIdx, filename)}
            <div id="rb-addpiece-modal-${ed.selectedToneIdx}" class="hidden m-3 bg-emerald-900/10 border border-emerald-800/30 rounded p-3"></div>
        </div>`;
}

function rbRenderToneTabs(tones, selectedIdx, filename) {
    const tabs = tones.map((t, idx) => {
        const active = idx === selectedIdx;
        const cls = active
            ? 'bg-accent text-white border-accent'
            : 'bg-dark-800 text-gray-400 border-gray-800 hover:bg-dark-700 hover:text-gray-200';
        // Small visual signal for edited/PSARC-default and a piece count.
        const pieces = (t.chain || []).length;
        const editedMark = t.chain_source === 'edited' ? ' ✎' : '';
        return `<button onclick="rbSelectTone(${idx}, '${rbEsc(filename).replace(/'/g,"\\'")}')"
                        title="${rbEsc(t.key)} · ${pieces} piece${pieces === 1 ? '' : 's'}"
                        class="flex-shrink-0 px-3 py-2 rounded-lg border text-xs transition ${cls}">
                    ${rbEsc(t.name)}${editedMark}
                    <span class="ml-1 text-[10px] opacity-70">${pieces}</span>
                </button>`;
    }).join('');
    return `<div class="flex items-center gap-1 overflow-x-auto px-3 pt-3 pb-2 border-b border-gray-800/40"
                 style="scrollbar-width: thin;">
                ${tabs}
            </div>`;
}

function rbRenderToneHeader(tone, toneIdx, filename) {
    const editedBadge = tone.chain_source === 'edited'
        ? `<span class="text-[10px] text-purple-300/80 bg-purple-900/20 border border-purple-800/30 rounded px-1.5 py-0.5"
                title="This tone's chain has been edited from the PSARC default">✎ edited</span>`
        : `<span class="text-[10px] text-gray-500" title="Untouched — matches the PSARC's original GearList">PSARC default</span>`;
    return `
        <div class="flex items-baseline justify-between px-4 py-3">
            <div class="flex items-baseline gap-2 min-w-0">
                <h3 class="text-white font-semibold truncate">${rbEsc(tone.name)}</h3>
                <span class="text-xs text-gray-500 truncate">${rbEsc(tone.key)}</span>
            </div>
            ${editedBadge}
        </div>`;
}

function rbRenderChainStrip(tone, toneIdx, selectedPIdx) {
    const chain = tone.chain || [];
    const total = chain.length;
    const filename = rbState.currentSongFile || '';
    // Build the strip piece-by-piece so we can interleave:
    //   ◀  (only on the LEFT side of the selected card, if not first)
    //   card
    //   ▶  (only on the RIGHT side of the selected card, if not last)
    //   →  (signal-flow arrow between adjacent cards)
    //   ＋ (always at the very end — adds a new piece)
    const parts = [];
    chain.forEach((p, pIdx) => {
        const isSelected = pIdx === selectedPIdx;
        const prevSelected = (pIdx > 0 && selectedPIdx === pIdx - 1);
        const isFirst = pIdx === 0;
        const isLast = pIdx === total - 1;
        // What goes IN FRONT of this card:
        //   - ◀ button if THIS card is the selected one (and not first)
        //   - nothing if the PREVIOUS card was selected (its ▶ button
        //     already sits in that slot)
        //   - → otherwise (the normal signal-flow arrow between
        //     adjacent stages)
        if (pIdx > 0) {
            if (isSelected && !isFirst) {
                parts.push(`<button onclick="event.stopPropagation(); rbMovePiece(${toneIdx}, ${pIdx}, -1)"
                                    title="Move this piece earlier in the chain"
                                    class="flex-shrink-0 self-stretch w-7 rounded-md bg-dark-700 hover:bg-accent/30 text-gray-300 hover:text-white text-sm transition flex items-center justify-center">◀</button>`);
            } else if (!prevSelected) {
                parts.push('<div class="flex-shrink-0 flex items-center text-gray-700 text-lg select-none" aria-hidden="true">→</div>');
            }
        }
        parts.push(rbRenderPieceCard(p, toneIdx, pIdx, isSelected, total));
        // ▶ Move-right button glued to the selected card's right side
        // (so visually the selected card always wears its reorder
        // controls on either flank).
        if (isSelected && !isLast) {
            parts.push(`<button onclick="event.stopPropagation(); rbMovePiece(${toneIdx}, ${pIdx}, 1)"
                                title="Move this piece later in the chain"
                                class="flex-shrink-0 self-stretch w-7 rounded-md bg-dark-700 hover:bg-accent/30 text-gray-300 hover:text-white text-sm transition flex items-center justify-center">▶</button>`);
        }
    });
    // ＋ Add-piece dropzone at the end of the chain — replaces the old
    // footer button so the "insert a new gear" affordance lives where
    // the user's eye already is (in the signal flow itself).
    parts.push(`<button onclick="rbOpenAddPiecePicker(${toneIdx}, '${rbEsc(filename).replace(/'/g,"\\'")}')"
                        title="Insert a new gear at the end of this tone's chain"
                        class="flex-shrink-0 w-16 self-stretch rounded-lg border-2 border-dashed border-emerald-800/40 hover:border-emerald-500 hover:bg-emerald-900/20 text-emerald-400 text-2xl transition flex items-center justify-center"
                        aria-label="Add piece to chain">＋</button>`);
    return `
        <div class="px-3 pb-3">
            <div class="text-[10px] text-gray-500 mb-1.5">
                Signal flow (${total} stage${total === 1 ? '' : 's'}, L → R) — click a piece to edit · ◀ ▶ to reorder the selected one · ＋ to add.
            </div>
            <div id="rb-chain-${toneIdx}"
                 class="flex items-stretch gap-2 overflow-x-auto pb-2"
                 style="scrollbar-width: thin;">
                ${parts.join('') || '<div class="text-xs text-gray-600 italic">empty chain</div>'}
            </div>
        </div>`;
}

function rbRenderPieceCard(p, toneIdx, pIdx, isSelected, total) {
    const bypassed = !!p._bypassed;
    // Effective assignment — drives the status dot colour.
    const hasVst = (p._vst_kind === 'vst' || (p.assigned && p.assigned.kind === 'vst' && p.assigned.vst_path));
    const hasFile = !hasVst && !!(p._uploaded_file || (p.assigned && p.assigned.file));
    // Status dot at the top-right. When bypassed, the dot "turns off":
    // a small ringed gray pip mirroring the unassigned style, so the
    // user knows the stage is dark even when it's still wired up.
    // Inline colour (not just a Tailwind class) so the dot is always visible
    // even if a purged/older CSS build drops the bg-* utility — and z-10 keeps
    // it above the thumbnail. This is the "missing status dot" fix.
    let dotHex, dotTitle;
    if (bypassed)      { dotHex = '#374151'; dotTitle = 'Bypassed — stage skipped (signal passes through)'; }
    else if (hasVst)   { dotHex = '#c084fc'; dotTitle = 'VST plugin loaded'; }
    else if (hasFile)  { dotHex = '#4ade80'; dotTitle = 'NAM/IR assigned'; }
    else               { dotHex = '#4b5563'; dotTitle = 'Unassigned'; }
    const statusDot = `<span class="absolute top-1 right-1 w-2 h-2 rounded-full z-10 ring-1 ring-black/30" style="background-color:${dotHex}" title="${rbEsc(dotTitle)}"></span>`;
    const selCls = isSelected
        ? 'border-accent ring-2 ring-accent/40 bg-dark-700'
        : 'border-gray-800 hover:border-gray-600 bg-dark-800/70';
    // When bypassed, drop the photo to grayscale + dim it so the card
    // visually reads as "off" — pairs nicely with the dimmed status dot.
    const imgBypassCls = bypassed ? 'grayscale opacity-40' : '';
    // Photo lookup: backend returns 404 when no the game art exists for
    // this rs_gear. The onerror swaps the broken <img> for the sibling
    // placeholder via plain DOM properties — avoids HTML-in-attribute
    // escaping bugs.
    const imgUrl = `${window.RB_API}/gear_photo/${encodeURIComponent(p.type)}${_RB_GEAR_PHOTO_CB}`;
    const onerr = "this.style.display='none'; var n=this.nextElementSibling; if(n) n.classList.remove('hidden');";
    // For pieces backed by one of our canvas-UI VSTs, show the recreated
    // plugin face (at the piece's current param values) instead of RS art.
    const pStem = hasVst ? rbCanvasStem(p) : '';
    const pCanvasArt = (pStem && window.RBPedalCanvas && window.RBPedalCanvas.has(pStem))
        ? window.RBPedalCanvas.dataURL(pStem, rbCanvasThumbValues(p)) : null;
    const pCanvasTag = pCanvasArt
        ? `<img src="${pCanvasArt}" alt="" style="max-width:100%;max-height:100%;object-fit:contain"
               class="relative max-w-full max-h-full object-contain transition ${imgBypassCls}">`
        : '';
    return `
        <button onclick="rbSelectPiece(${toneIdx}, ${pIdx})"
                class="relative flex-shrink-0 w-28 rounded-lg border ${selCls} p-2 text-left transition focus:outline-none"
                style="width:112px">
            <div class="text-[9px] text-gray-500 mb-1 flex items-center justify-between">
                <span class="font-mono">${pIdx + 1}/${total}</span>
                <span class="uppercase tracking-wide">${rbEsc(p.rs_category || '')}</span>
            </div>
            <div class="relative flex justify-center items-center mb-1.5 h-20 rounded bg-dark-900 overflow-hidden" style="height:80px">
                <div class="absolute inset-0 flex items-center justify-center text-[10px] text-gray-600 text-center px-1 leading-tight ${imgBypassCls}">
                    ${rbEsc(p.rs_category || 'gear')}
                </div>
                ${pCanvasTag}
                <img src="${imgUrl}" alt="" loading="lazy"
                     style="${pCanvasArt ? 'display:none;' : ''}max-width:100%;max-height:100%;object-fit:contain"
                     class="relative max-w-full max-h-full object-contain transition ${imgBypassCls}"
                     onerror="this.style.display='none';">
            </div>
            <div class="text-[11px] ${bypassed ? 'text-gray-500' : 'text-gray-200'} leading-tight line-clamp-2 min-h-[2.2em]" title="${rbEsc(p.real_name || p.type)}">
                ${rbEsc(p.real_name || p.type)}
            </div>
            ${statusDot}
        </button>`;
}

function rbRenderEditorFooter(toneIdx, filename) {
    // The "＋ Add piece" button used to live here, but it moved to the
    // tail of the chain strip (rbRenderChainStrip) so the affordance
    // sits where the user is already looking when planning the signal
    // flow. The footer is now just the playback controls.
    return `
        <div class="flex flex-wrap justify-end items-center gap-2 px-4 py-3 border-t border-gray-800/40 bg-dark-800/30">
            <button id="rb-listen-${toneIdx}"
                    onclick="rbListenTone(${toneIdx}, '${rbEsc(filename).replace(/'/g,"\\'")}')"
                    title="Saves the tone and plays it live through the NAM engine"
                    class="bg-dark-600 hover:bg-dark-500 text-gray-200 px-4 py-1.5 rounded-lg text-xs transition">
                ▶ Listen
            </button>
            <button onclick="rbSaveTonePreset(${toneIdx}, '${rbEsc(filename).replace(/'/g,"\\'")}')"
                    class="bg-accent hover:bg-accent/80 text-white px-4 py-1.5 rounded-lg text-xs transition">
                💾 Save preset
            </button>
        </div>`;
}

function rbRenderPieceEditor(p, toneIdx, pIdx, filename) {
    // NAM / IR uploads and Library picks happen exclusively from the
    // All Gear tab now — they're catalog-level operations, not
    // per-song. The song editor only deals with chain-level decisions
    // (variant override, gear swap, reorder, bypass, VST params).
    const isCab = p.rs_category === 'cab';
    const pendingKind = p._uploaded_kind || p._vst_kind;
    const assignedKind = p.assigned && p.assigned.kind;
    const effKind = pendingKind || assignedKind || (isCab ? 'ir' : 'nam');
    const effVstPath = rbEffVstPath(p);
    const effVstFormat = rbEffVstFormat(p);
    const effFile = rbEffFile(p);
    const hasVst = effKind === 'vst' && !!effVstPath;
    const hasFile = !hasVst && !!effFile;
    const hasNam = !hasVst && effKind === 'nam' && !!effFile;
    const mode = (p.assigned && p.assigned.assigned_mode) || (p._uploaded_file ? 'manual' : '');
    const bypassed = !!p._bypassed;

    let stageLabel, stageClass;
    if (hasVst) {
        stageLabel = `✓ VST: ${effVstPath.split('/').pop()}`;
        stageClass = 'text-purple-300';
    } else if (hasFile) {
        const a = p.assigned;
        const title = (!p._uploaded_file && a && a.file === effFile && a.tone3000_title) ? a.tone3000_title : '';
        stageLabel = `✓ ${title || rbLibShortName(effFile)}`;
        stageClass = 'text-green-400';
    } else {
        stageLabel = '(unassigned)';
        stageClass = 'text-gray-500';
    }

    // Cab mic-position picker — clickable buttons per mic resolved
    // from rs_cab_mic_map (Dynamic Cone, Condenser Edge, Tube Off-axis,
    // …). Falls back to the legacy "the game IR (N):" filename dropdown
    // for cabs whose mic_variants the extractor couldn't resolve
    // (e.g. the user hasn't re-run extract_irs since we added the map).
    let rsIrControl = '';
    const micVariants = p.cab_mic_variants || [];
    if (micVariants.length > 0) {
        const activeFile = effFile;
        const btns = micVariants.map(v => {
            const active = v.ir_file === activeFile;
            if (!v.available || !v.ir_file) {
                return `<button disabled title="IR not extracted"
                                class="px-2.5 py-0.5 rounded border text-[11px] bg-dark-800/40 text-gray-600 border-gray-800 cursor-not-allowed">${rbEsc(v.label || v.suffix)}</button>`;
            }
            const cls = active
                ? 'bg-sky-700/60 text-sky-100 border-sky-500/60 font-semibold'
                : 'bg-dark-800 text-gray-300 border-gray-700 hover:bg-sky-900/40 hover:text-sky-200 hover:border-sky-700/40';
            return `<button onclick="rbPickCabMic(${toneIdx}, ${pIdx}, '${rbEsc(v.ir_file).replace(/'/g,"\\'")}')"
                            title="${rbEsc(v.mic_type || '')} · ${rbEsc(v.position || '')} (suffix ${rbEsc(v.suffix)})"
                            class="px-2.5 py-0.5 rounded border text-[11px] transition ${cls}">${rbEsc(v.label || v.suffix)}</button>`;
        }).join(' ');
        rsIrControl = `
            <div class="bg-sky-900/15 border border-sky-800/30 rounded p-2.5 mt-2">
                <div class="flex items-center gap-2 mb-1.5">
                    <span class="text-xs text-sky-400">🎙 Mic position</span>
                    <span class="text-[10px] text-gray-500">Cab IRs — click to switch</span>
                </div>
                <div class="flex items-center gap-1.5 flex-wrap">${btns}</div>
            </div>`;
    } else if ((p.rs_irs || []).length > 0) {
        // Legacy fallback: raw dropdown for cabs without a mic map.
        const rsIrs = p.rs_irs;
        const options = rsIrs.map(f => `<option value="${rbEsc(f)}">${rbEsc(f.split('/').pop())}</option>`).join('');
        rsIrControl = `
            <div class="flex items-center gap-2 bg-green-900/15 border border-green-800/30 rounded px-2 py-1.5 mt-2">
                <span class="text-xs text-green-400 whitespace-nowrap">IR (${rsIrs.length}):</span>
                <select onchange="rbPickRsIr(this, ${toneIdx}, ${pIdx})"
                        class="flex-1 bg-dark-800 border border-gray-800 rounded text-xs text-gray-300 px-1 py-0.5">${options}</select>
                <button onclick="rbAssignRsIr(this, ${toneIdx}, ${pIdx})"
                        class="bg-green-700 hover:bg-green-600 text-white text-xs px-2 py-0.5 rounded">Use</button>
            </div>`;
    }

    // Amp gain variant picker — clickable buttons, active level highlighted.
    let ampVariantBadge = '';
    if (hasNam && p.amp_variant && Array.isArray(p.amp_variant.available) && p.amp_variant.available.length) {
        const av = p.amp_variant;
        const activeLevel = av.current_level || av.picked;
        const manualMode = (p.assigned && p.assigned.assigned_mode === 'manual');
        const overrideActive = manualMode && av.current_level && av.current_level !== av.picked;
        const btns = (av.available || []).map(level => {
            const active = level === activeLevel;
            const cls = active
                ? 'bg-emerald-700/60 text-emerald-100 border-emerald-500/60 font-semibold'
                : 'bg-dark-800 text-gray-300 border-gray-700 hover:bg-emerald-900/40 hover:text-emerald-200 hover:border-emerald-700/40';
            return `<button onclick="rbPickVariant(${toneIdx}, ${pIdx}, '${rbEsc(level)}')"
                            title="Force this gain variant for this song"
                            class="px-3 py-1 rounded border text-xs transition ${cls}">${rbEsc(level)}</button>`;
        }).join(' ');
        const autoBtn = `<button onclick="rbPickVariant(${toneIdx}, ${pIdx}, 'auto')"
                                 title="Restore the auto-pick based on the song's Gain knob"
                                 class="px-3 py-1 rounded border text-xs transition ${overrideActive ? 'bg-dark-800 text-gray-400 border-gray-700 hover:bg-emerald-900/30' : 'bg-emerald-700/40 text-emerald-200 border-emerald-600/40'}">↺ auto</button>`;
        ampVariantBadge = `
            <div class="bg-emerald-900/15 border border-emerald-800/30 rounded p-2.5 mt-2">
                <div class="flex items-center gap-2 mb-1.5">
                    <span class="text-xs text-emerald-400">🎛 Gain variant</span>
                    <span class="text-[10px] text-gray-500">${overrideActive ? `manual override (auto would be ${rbEsc(av.picked || '?')})` : `auto from RS Gain knob = ${rbEsc(av.rs_gain)}`}</span>
                </div>
                <div class="flex items-center gap-1.5 flex-wrap">${btns} ${autoBtn}</div>
            </div>`;
    }

    // RS knob badges — read-only summary of the game's per-piece values.
    const rsKnobs = p.knobs || {};
    const knobNames = Object.keys(rsKnobs);
    let rsKnobsBlock = '';
    if (knobNames.length > 0) {
        const pairs = knobNames.map(k => {
            const v = rsKnobs[k];
            const display = typeof v === 'number' ? (v % 1 === 0 ? v.toString() : v.toFixed(1)) : v;
            return `<span class="inline-block bg-dark-900/60 border border-gray-800/50 rounded px-1.5 py-0.5 text-[10px] text-gray-300 mr-1 mb-1"><span class="text-gray-500">${rbEsc(k)}</span> <span class="text-amber-300">${rbEsc(display)}</span></span>`;
        }).join('');
        rsKnobsBlock = `
            <div class="bg-dark-900/30 border border-gray-800/40 rounded p-2.5 mt-2">
                <div class="text-[10px] text-gray-500 mb-1.5">Knob values (read-only)</div>
                <div class="flex flex-wrap">${pairs}</div>
            </div>`;
    }

    // Bypass button — same toggle as before, styled larger for the editor.
    const bypassCls = bypassed
        ? 'bg-amber-700/40 text-amber-300 border-amber-600/40'
        : 'bg-dark-700 hover:bg-dark-600 text-gray-300 border-gray-700';
    const bypassLabel = bypassed ? '⤳ Bypassed (signal passes through)' : 'Bypass this stage';

    // Big photo for the editor (same source as the chain cards).
    // Same sibling-swap pattern as rbRenderPieceCard — see comment there
    // for why we avoid the `JSON.stringify` inside an attribute approach.
    const imgUrl = `${window.RB_API}/gear_photo/${encodeURIComponent(p.type)}${_RB_GEAR_PHOTO_CB}`;
    const onerrBig = "this.style.display='none'; var n=this.nextElementSibling; if(n) n.classList.remove('hidden');";
    // Plugin-UI face for our canvas-backed VSTs (current param values).
    const pStemBig = rbCanvasStem(p);
    const pCanvasArtBig = (pStemBig && window.RBPedalCanvas && window.RBPedalCanvas.has(pStemBig))
        ? window.RBPedalCanvas.dataURL(pStemBig, rbCanvasThumbValues(p)) : null;
    const pCanvasTagBig = pCanvasArtBig
        ? `<img src="${pCanvasArtBig}" alt="" style="max-width:100%;max-height:100%;object-fit:contain"
               class="max-w-full max-h-full rounded object-contain bg-dark-900">`
        : '';

    return `
        <div class="bg-dark-800/40 border-y border-gray-800/40 p-4 space-y-3" data-tone="${toneIdx}" data-piece="${pIdx}">
            <div class="flex items-start gap-4">
                <div class="flex-shrink-0 w-32 h-32 flex items-center justify-center overflow-hidden" style="width:128px;height:128px">
                    ${pCanvasTagBig}
                    <img src="${imgUrl}" alt="" loading="lazy"
                         style="${pCanvasArtBig ? 'display:none;' : ''}max-width:100%;max-height:100%;object-fit:contain"
                         class="max-w-full max-h-full rounded object-contain bg-dark-900"
                         onerror="${onerrBig}">
                    <div class="hidden w-full h-full rounded bg-dark-900 flex items-center justify-center text-xs text-gray-600 text-center px-2">
                        ${rbEsc(p.rs_category || 'gear')}
                    </div>
                </div>
                <div class="min-w-0 flex-1">
                    <div class="flex items-baseline justify-between gap-2 mb-1">
                        <div class="min-w-0">
                            <div class="text-base text-gray-100 font-medium truncate">${rbEsc(p.real_name || p.type)}</div>
                            <div class="text-xs text-gray-500 truncate">
                                #${pIdx + 1} · ${rbEsc(p.slot)} · ${rbEsc(p.rs_category)}
                                <span class="text-gray-600">·</span>
                                <code class="text-gray-500">${rbEsc(p.type)}</code>
                            </div>
                        </div>
                        <button id="rb-bypass-${toneIdx}-${pIdx}"
                                onclick="rbToggleBypass(${toneIdx}, ${pIdx}, this)"
                                title="Bypass skips this stage in the preview (signal passes through unprocessed)"
                                class="flex-shrink-0 px-3 py-1.5 rounded border text-xs transition ${bypassCls}">
                            ${rbEsc(bypassLabel)}
                        </button>
                    </div>
                    <div class="text-xs ${stageClass} truncate" title="${rbEsc(hasVst ? effVstPath : (hasFile ? effFile : ''))}">${rbEsc(stageLabel)}
                        ${(hasFile || hasVst) && mode ? `<span class="text-[10px] text-gray-600 ml-1">(${rbEsc(mode)})</span>` : ''}
                    </div>
                </div>
            </div>

            ${ampVariantBadge}

            <div class="flex flex-wrap items-center gap-2">
                <button onclick="rbToggleGearSwap(${toneIdx}, ${pIdx})"
                        title="Swap this ${rbEsc(p.rs_category)} for a different one — just for this song"
                        class="bg-amber-900/25 hover:bg-amber-900/45 text-amber-300 border border-amber-800/40 px-3 py-1.5 rounded text-xs">
                    🔁 Swap…
                </button>
                ${hasVst ? `
                <button onclick="rbToneEditVst(${toneIdx}, ${pIdx})"
                        title="Load this VST in the engine and edit its parameters with inline sliders — just for this song"
                        class="bg-purple-900/30 hover:bg-purple-900/50 text-purple-300 border border-purple-800/40 px-3 py-1.5 rounded text-xs">
                    🎛 Edit VST
                </button>` : ''}
                <div class="flex-1"></div>
                <button onclick="rbRemovePiece(${toneIdx}, ${pIdx})"
                        title="Remove this piece from the chain"
                        class="px-2 py-1 rounded text-xs bg-red-900/30 hover:bg-red-900/50 text-red-300 border border-red-800/40 transition">✗ Remove</button>
            </div>

            <div id="rb-swap-${toneIdx}-${pIdx}" class="hidden bg-amber-900/10 border border-amber-800/30 rounded p-2"></div>
            <div id="rb-tone-vst-editor-${toneIdx}-${pIdx}" class="hidden bg-purple-900/10 border border-purple-800/30 rounded p-2 space-y-2"></div>

            ${rsKnobsBlock}
            ${rsIrControl}
        </div>`;
}

// Click handler for tone tabs at the top. Resets the piece selection to
// 0 so the user always lands on the first stage of the new tone (which
// is usually the most-recently-swapped — the amp is rarely at index 0).
function rbSelectTone(toneIdx, filename) {
    const ed = rbEnsureEditorState();
    ed.selectedToneIdx = toneIdx;
    ed.selectedPIdx = 0;
    rbReRenderSongEditor(filename);
}

function rbSelectPiece(toneIdx, pIdx) {
    const ed = rbEnsureEditorState();
    ed.selectedToneIdx = toneIdx;
    ed.selectedPIdx = pIdx;
    rbReRenderSongEditor();
}

// Full redraw of the song editor without re-fetching from the backend.
// Used after in-memory mutations (reorder, bypass toggle) — server-side
// edits use rbRefreshSongAfterEdit which does an extra /song fetch.
function rbReRenderSongEditor(filename) {
    if (!rbState.songTones) return;
    const el = document.getElementById('rb-song-tones');
    if (!el) return;
    const f = filename || rbState.currentSongFile;
    el.innerHTML = rbRenderSongEditor(rbState.songTones, f);
}

// Backwards-compat shim so the old call sites (rbAutoDownloadSong,
// rbRefreshSongAfterEdit) still trigger a redraw of the active tone.
// The arg list stays the same but we re-render the whole editor — the
// chain strip + detail panel both need to refresh after any chain
// change (variant override, gear swap, add/remove piece).
function rbReRenderToneChain(toneIdx, filename) {
    rbReRenderSongEditor(filename);
}

// rbRenderPiece kept as a thin shim — the v2 editor renders pieces via
// rbRenderPieceCard (chain strip) + rbRenderPieceEditor (detail panel)
// instead. Anyone still calling rbRenderPiece by mistake gets the
// detail-panel form so they don't render an empty box.
function rbRenderPiece(p, toneIdx, pIdx) {
    return rbRenderPieceEditor(p, toneIdx, pIdx, rbState.currentSongFile || '');
}

// Quick "🎛 Edit VST" shortcut for per-tone pieces that already have a VST
// assigned. Same UX as rbMasterEditVst — opens an inline panel with HTML
// sliders for every parameter, drives setParameter live, lets you capture
// state back into the piece. Sidesteps the 2-click path through 📚 Library
// → Plugins tab → Load & Edit so the editor is a single click away.
// VST hosts auto-expose ~128 "MIDI CC <n>" automation params (plus the odd
// bypass/program meta param). They aren't real plugin controls and flood the
// inline editor — show only the plugin's own parameters.
function rbFilterVstParams(params) {
    return (params || []).filter(p => {
        const n = String(p.name ?? p.label ?? '').trim();
        // MIDI CC / MIDI Learn assignments — never user-meaningful in our
        // chain editor.
        if (/^midi/i.test(n)) return false;
        // Generic 'Param 1..4' placeholders. Melda exposes 4 of these on
        // most of its free plugins as preset-mappable hooks; they have no
        // effect unless wired in the Melda UI to a sound param.
        if (/^param\s+\d+$/i.test(n)) return false;
        // Preset cycling triggers ('previous (Preset trigger)' / 'next
        // (Preset trigger)') — host-automation hooks, not sound params.
        if (/\(\s*preset\s+trigger\s*\)/i.test(n)) return false;
        // Bypass + Program — the chain editor has its own dedicated
        // Bypass UI; Program is an internal patch index irrelevant here.
        if (/^bypass$/i.test(n)) return false;
        if (/^program$/i.test(n)) return false;
        // Engine-injected meta params. The native host PREPENDS "Buffer Size"
        // and "Sample Rate" (and sometimes "Latency") to every plugin's param
        // list — they're not the plugin's own params. Leaving them in shifted
        // every real param's index by 2, so the canvas knob/fader at logical
        // slot 0 was reading/driving "Buffer Size" instead of the first real
        // knob. Drop them here so logical position == real-param order.
        if (/^(buffer\s*size|sample\s*rate|latency)$/i.test(n)) return false;
        return true;
    });
}

// Build the canvas's parameter model from a raw getParameters() array.
// Returns { values, idMap, logicalParams }:
//   • values     — keyed by LOGICAL index (0,1,2… into the filtered real
//                  params) AND by param name, so hand-built specs (logical
//                  ids) and the EQ (band freq names) both resolve correctly.
//   • idMap       — logical index → REAL engine paramId (for setParameter).
//   • logicalParams — the filtered params re-id'd to their logical index
//                  (so the generic fallback lays them out 0,1,2…).
// `overrideById` (optional, keyed by REAL id) overlays in-progress edits.
function rbBuildCanvasModel(rawParams, overrideById) {
    const filtered = rbFilterVstParams(rawParams || []);
    const values = {}, idMap = {};
    const logicalParams = filtered.map((p, i) => {
        const realId = p.id ?? p.paramId ?? p.index ?? i;
        idMap[i] = realId;
        let v = p.value ?? p.current;
        if (overrideById && overrideById[realId] != null) v = overrideById[realId];
        if (typeof v === 'number') { values[i] = v; if (p.name) values[p.name] = v; if (p.label) values[p.label] = v; }
        return Object.assign({}, p, { id: i });
    });
    return { values, idMap, logicalParams };
}

// Tear down the currently-loaded inline-editor VST: close its native window
// FIRST, then clear its slot. Skipping the close left an orphaned native
// editor window pointing at a slot we then cleared — re-editing (or editing a
// second piece) crashed the host. Resets the tracked slot so the next open
// starts clean.
async function rbTeardownVstEditor(api) {
    const slot = rbState._vstEditorSlot;
    const inChain = rbState._vstEditorInChain;
    rbState._vstEditorSlot = null;
    rbState._vstEditorInChain = false;
    if (!api) return;
    // Close the prior editor's native window only if there was one…
    if (slot != null) {
        try { if (api.closePluginEditor) await api.closePluginEditor(slot); } catch (_) {}
    }
    // …and clear the chain ONLY for an ISOLATED single-VST editor. The earlier
    // unconditional clear fixed "Edit VST doubles the sound" back when opening
    // the editor stacked a 2nd copy on top of the live chain via loadVST. The
    // editor now edits the pedal IN PLACE inside the live preview chain (no 2nd
    // copy), so for an in-chain edit the preview owns the chain (torn down via
    // rbStopPreview) — clearing here would kill the sound the instant you close
    // the pedal face.
    if (!inChain) {
        rbStopFinalChainNormalizer();
        try { if (api.clearChain) await api.clearChain(); } catch (_) {}
    }
}

// Map a song-tone piece to the engine slot id of its stage WITHIN the currently
// loaded preview chain, so editing tweaks the pedal in place (the whole chain
// keeps playing) instead of loading an isolated, louder single copy. Returns
// null when there's no live chain or the piece isn't found in it — callers then
// fall back to the isolated single-VST editor.
//
// How the mapping works: the backend builds `native_preset.chain` in signal
// order; each type-0 (VST) stage carries the gear `path` + `rs_gear` (= the UI
// piece's `type`). `getChainState()` returns the loaded stages index-aligned
// with that chain spec, so chain index → engine slot id. Duplicate identical
// pedals are disambiguated by skipping earlier same-(type,path) pieces.
async function rbChainSlotIdForPiece(api, payload, toneIdx, pIdx) {
    try {
        if (!api || typeof api.getChainState !== 'function') return null;
        const chain = payload && payload.native_preset && payload.native_preset.chain;
        if (!Array.isArray(chain)) return null;
        const tone = rbState.songTones && rbState.songTones.tones[toneIdx];
        const piece = tone && tone.chain[pIdx];
        if (!piece) return null;
        const effPath = rbEffVstPath(piece);
        let dupSkip = 0;
        for (let k = 0; k < pIdx; k++) {
            const q = tone.chain[k];
            if (q && q.type === piece.type && rbEffVstPath(q) === effPath) dupSkip++;
        }
        let seen = 0, idx = -1;
        for (let i = 0; i < chain.length; i++) {
            const st = chain[i];
            if (!st || Number(st.type) !== 0) continue;
            if (typeof st.slot === 'string' && st.slot.startsWith('master_')) continue;
            if (piece.type != null && st.rs_gear !== piece.type) continue;
            if (effPath && st.path && st.path !== effPath) continue;
            if (seen++ < dupSkip) continue;
            idx = i; break;
        }
        if (idx < 0) return null;
        const loaded = await api.getChainState();
        if (!Array.isArray(loaded) || idx >= loaded.length) return null;
        const slot = loaded[idx];
        if (!slot) return null;
        return slot.id != null ? slot.id : (slot.slotId != null ? slot.slotId : idx);
    } catch (_) { return null; }
}

// Close any inline VST editor's NATIVE window + clear the tracked slot, but
// WITHOUT clearing the chain (the caller's own clearChain/loadPreset handles
// that). MUST run before navigating away (tab switch, song load) and before
// any preview clearChain/loadPreset: leaving the native editor window open
// while its slot is cleared/replaced crashes the host. This is the
// "edit a master-chain VST → switch menu / load a song → crash" report.
async function rbCloseActiveVstEditor() {
    const slot = rbState._vstEditorSlot;
    if (slot == null) return;
    rbState._vstEditorSlot = null;
    rbState._vstEditorInChain = false;
    const api = window.slopsmithDesktop && window.slopsmithDesktop.audio;
    if (api && api.closePluginEditor) {
        try { await api.closePluginEditor(slot); } catch (_) {}
    }
    // Collapse any still-open inline editor panels so a later re-open is clean.
    document.querySelectorAll(
        '[id^="rb-master-pre-editor-"],[id^="rb-master-post-editor-"],[id^="rb-tone-vst-editor-"]'
    ).forEach(el => {
        if (!el.classList.contains('hidden')) { el.classList.add('hidden'); el.innerHTML = ''; }
    });
}

// Called when the user LEAVES the Rig Builder screen (tab/plugin navigation).
// Closes the open VST editor's native window + clears its engine slot BEFORE
// the next screen (e.g. the song player) loads a preset — otherwise the player's
// clearChain/loadPreset tears down the slot under the still-open editor window
// and crashes the host. This is the "edit master VST → enter a song → crash"
// report. Idempotent + safe to call when nothing is open.
let _rbLeaving = false;
async function rbOnLeaveRigBuilder() {
    rbStopFinalChainNormalizer();
    if (_rbLeaving) return;
    _rbLeaving = true;
    try {
        const hadEditor = rbState._vstEditorSlot != null;
        await rbCloseActiveVstEditor();           // close native window first
        if (rbState.listeningTone !== null || rbState._auditionId) {
            await rbStopPreview();                // also clears chain + stops audio
        } else if (hadEditor) {
            // The inline editor left its VST loaded in the engine; clear it so
            // it doesn't linger or get torn down under a half-closed window.
            const api = window.slopsmithDesktop && window.slopsmithDesktop.audio;
            if (api && api.clearChain) await api.clearChain().catch(() => {});
        }
    } finally {
        _rbLeaving = false;
    }
}

// Capture the engine's OWN opaque VST state blob — the same thing savePreset
// produces and loadPreset restores. This is the ONLY VST state the engine
// re-applies during REAL song playback; our {params} JSON dict is editor-only
// (reapplied via setParameter in the preview, but there is no hook to do that
// after nam_tone's loadPreset for an actual song). The inline editor loads
// just this one VST (the chain is cleared first), so the single type-0 stage
// in savePreset()'s chain is it. Returns a base64 string, or null.
async function rbCaptureVstOpaqueState(api, expectVstPath) {
    if (!api || typeof api.savePreset !== 'function') return null;
    try {
        const blob = await api.savePreset();
        if (!blob) return null;
        const parsed = typeof blob === 'string' ? JSON.parse(blob) : blob;
        const chain = (parsed && parsed.chain) || [];
        let stage = chain.find(s => Number(s.type) === 0
            && (!expectVstPath || !s.path || s.path === expectVstPath));
        if (!stage) stage = chain.find(s => Number(s.type) === 0);
        return (stage && typeof stage.state === 'string' && stage.state) || null;
    } catch (_) { return null; }
}

// Stamp a piece's _vst_state with BOTH the editor params and the engine's
// opaque blob, in one envelope: {params:{…}, opaque:"<b64>"}. The backend
// emits `opaque` as the stage state (so it applies in real playback); `params`
// stays for the editor sliders + the preview's setParameter fallback. Keeps
// the last-known opaque if this call didn't capture a fresh one.
function rbStampVstState(piece, opaque) {
    if (opaque) piece._vst_opaque = opaque;
    const env = { params: piece._vst_params || {} };
    if (piece._vst_opaque) env.opaque = piece._vst_opaque;
    piece._vst_state = JSON.stringify(env);
}

// Pull the opaque blob out of a saved {params, opaque} envelope (or null).
function rbParseVstStateOpaque(state) {
    if (!state) return null;
    try {
        const obj = typeof state === 'string' ? JSON.parse(state) : state;
        if (obj && typeof obj.opaque === 'string' && obj.opaque) return obj.opaque;
    } catch (_) { /* legacy / opaque-only — nothing to pull */ }
    return null;
}

async function rbToneEditVst(toneIdx, pIdx) {
    const piece = rbState.songTones && rbState.songTones.tones[toneIdx] && rbState.songTones.tones[toneIdx].chain[pIdx];
    if (!piece) return;
    const editor = document.getElementById(`rb-tone-vst-editor-${toneIdx}-${pIdx}`);
    if (!editor) return;
    const api = window.slopsmithDesktop && window.slopsmithDesktop.audio;
    // Toggle close if already open — tear the editor VST down cleanly.
    if (!editor.classList.contains('hidden')) {
        editor.classList.add('hidden');
        editor.innerHTML = '';
        await rbTeardownVstEditor(api);
        piece._vst_slot_id = null;
        return;
    }
    if (!api) return alert('Native VST hosting not available');
    const vstPath = rbEffVstPath(piece);
    if (!vstPath) return alert('This piece has no VST assigned yet.');
    if (rbState._vstEditorBusy) return;   // ignore rapid double-clicks while a load is in flight
    rbState._vstEditorBusy = true;
    editor.classList.remove('hidden');
    editor.innerHTML = `<div class="text-xs text-gray-500">loading ${rbEsc(vstPath.split('/').pop())}…</div>`;
    try {
        // Did the tone have a saved / in-session param state BEFORE we touch
        // anything? Decides whether we auto-apply the RS knob mapping below
        // (and we must read it now, before the snapshot reseed wipes it).
        const persistedParams = (piece._vst_params && Object.keys(piece._vst_params).length)
            ? piece._vst_params
            : ((piece.assigned && piece.assigned.vst_state)
                ? rbParseVstStateParams(piece.assigned.vst_state) : null);
        const hadSaved = !!(persistedParams && Object.keys(persistedParams).length);

        // Close any previously-open editor's native window cleanly first
        // (doesn't clear the chain — a live preview keeps playing).
        await rbCloseActiveVstEditor();

        // Play the WHOLE tone chain so the pedal is heard IN CONTEXT and editing
        // adjusts the chain's sound — not an isolated, louder single VST. Start
        // the full-chain preview for this tone unless it's already the live one.
        const alreadyPreviewing = (rbState.listeningTone === toneIdx
            && rbState._previewMode === 'native');
        // Start the preview only when this tone isn't already the active one —
        // rbListenTone TOGGLES, so calling it for the already-listening tone
        // would stop playback instead of starting it.
        if (rbState.listeningTone !== toneIdx && rbState.currentSongFile) {
            await rbListenTone(toneIdx, rbState.currentSongFile);
        }
        // rbListenTone / rbCloseActiveVstEditor collapse inline panels — re-open ours.
        editor.classList.remove('hidden');
        editor.innerHTML = `<div class="text-xs text-gray-500">loading ${rbEsc(vstPath.split('/').pop())}…</div>`;

        // Locate this piece's stage inside the loaded chain. setParameter on that
        // slot tweaks the pedal in place; the chain keeps playing and no 2nd copy
        // is stacked (loading a separate VST on top doubled the sound).
        let slotId = await rbChainSlotIdForPiece(api, rbState._previewPayload, toneIdx, pIdx);
        const haveChainSlot = slotId != null;
        if (haveChainSlot) {
            rbState._vstEditorSlot = slotId;
            rbState._vstEditorInChain = true;
            // The chain load already re-applied saved params; just read them back
            // so the canvas/sliders open reflecting the live values.
            try { piece._vst_param_meta = await api.getParameters(slotId); }
            catch (_) { piece._vst_param_meta = piece._vst_param_meta || []; }
        } else {
            // Fallback (no live chain / piece not found): isolated single-VST
            // edit so the editor still works. This DOES own + clear the chain.
            try { if (api.clearChain) await api.clearChain(); } catch (_) {}
            await api.startAudio().catch(() => {});
            slotId = await rbLoadVSTWhenReady(api, vstPath);
            if (slotId == null || slotId < 0) {
                editor.innerHTML = `<div class="text-xs text-red-400">${rbEsc(rbVstRefusedMsg())}</div>`;
                return;
            }
            rbState._vstEditorSlot = slotId;
            rbState._vstEditorInChain = false;
            // Re-apply previously captured params if any. Helper resolves NAME
            // keys (from apply_vst_state.py bulk-populated states) → numeric ids
            // and clamps values to [0,1].
            const saved = piece._vst_params
                || (piece.assigned && piece.assigned.vst_state
                    ? rbParseVstStateParams(piece.assigned.vst_state) : null);
            piece._vst_param_meta = await rbRestoreSavedParamsToSlot(api, slotId, saved, vstPath)
        }
        piece._vst_slot_id = slotId;
        // Keep any previously-saved opaque blob so re-saving without a fresh
        // capture (e.g. just closing) doesn't drop it.
        piece._vst_opaque = piece._vst_opaque
            || rbParseVstStateOpaque(piece._vst_state)
            || rbParseVstStateOpaque(piece.assigned && piece.assigned.vst_state);
        // Seed _vst_params with the FULL current snapshot so subsequent slider
        // drags modify a complete dict (not just the touched ids). Persisting
        // partial dicts was a data-loss bug: untouched params would silently
        // revert to plugin defaults on chain rebuild.
        piece._vst_params = {};
        for (const param of (piece._vst_param_meta || [])) {
            const id = param.id ?? param.paramId ?? param.index;
            const v  = param.value ?? param.current;
            if (id != null && typeof v === 'number') piece._vst_params[id] = v;
        }
        // Auto-apply this song's the game knob mapping when the tone has NO
        // captured/saved state yet — so the editor opens reflecting the song's
        // settings instead of plugin defaults. (The manual "Apply RS settings"
        // button still lets you re-apply or override.) Skipped when a curated
        // state already exists so we don't clobber the user's own tweaks.
        if (!hadSaved && piece.knobs && Object.keys(piece.knobs).length) {
            try {
                const vstStem2 = vstPath.split('/').pop().replace(/\.(vst3|component)$/i, '');
                const mapped = await rbComputeRsMappedParams(piece.type, piece.knobs, vstStem2, piece._vst_param_meta);
                if (mapped && Object.keys(mapped).length) {
                    for (const [id, v] of Object.entries(mapped)) {
                        const nid = Number(id);
                        try { await api.setParameter(slotId, nid, v); } catch (_) {}
                        piece._vst_params[nid] = v;
                    }
                    try { piece._vst_param_meta = await api.getParameters(slotId); } catch (_) {}
                }
            } catch (_) { /* mapping is best-effort; defaults remain on failure */ }
        }
        // Editor priority: (1) faithful in-app canvas recreation; (2) for VSTs we
        // HAVEN'T recreated, the plugin's OWN native window on top (the real UI,
        // not generic sliders); (3) generic in-app sliders only when the plugin
        // has no native window (UI-less, e.g. a headless filter).
        if (rbHasCanvasUI(piece)) {
            rbToneRenderInlineVstParams(toneIdx, pIdx);
        } else if (await rbTryOpenNativeEditor(api, slotId)) {
            const _nm = vstPath.split('/').pop().replace(/\.(vst3|component)$/i, '');
            editor.innerHTML = rbNativeEditorPanelHtml(
                _nm, `rbToneCaptureVstState(${toneIdx}, ${pIdx})`, `rbToneEditVst(${toneIdx}, ${pIdx})`);
        } else {
            rbToneRenderInlineVstParams(toneIdx, pIdx);   // UI-less fallback: generic sliders
        }
    } catch (e) {
        editor.innerHTML = `<div class="text-xs text-red-400">load failed: ${rbEsc(rbFriendlyVstLoadError(e))}</div>`;
    } finally {
        rbState._vstEditorBusy = false;
    }
}

function rbIsWindows() { return /win/i.test((navigator.platform || navigator.userAgent || '')); }
// Message for a failed VST load. The bundled effects currently ship macOS-only
// VST3 binaries, so on Windows the engine can't load them — say so clearly
// instead of the cryptic "engine refused to load this plugin".
function rbVstRefusedMsg() {
    return 'engine refused to load this plugin'
        + (rbIsWindows()
            ? ' — heads up: the bundled effects only ship a macOS build right now, so they can\'t load on Windows yet (a Windows build is on the way).'
            : ' — the plugin binary could not be opened. Check OS/CPU compatibility, macOS quarantine/blocking, and that the .vst3/.component bundle is complete.');
}

function rbFriendlyVstLoadError(e) {
    const raw = String((e && e.message) || e || '').trim();
    if (!raw) return 'VST load failed';
    if (/engine refused/i.test(raw) || /refused (to load )?(this )?plugin/i.test(raw)) {
        return rbVstRefusedMsg();
    }
    return raw;
}

// Normalize a VST path → canvas spec key (lowercased basename, no separators).
function rbCanvasStem(piece) {
    const p = rbEffVstPath(piece);
    if (!p) return '';
    return p.split('/').pop().replace(/\.(vst3|component)$/i, '').toLowerCase().replace(/[^a-z0-9]/g, '');
}
// True if we have an in-app canvas recreation of this piece's plugin UI.
function rbHasCanvasUI(piece) {
    return !!(window.RBPedalCanvas && window.RBPedalCanvas.has(rbCanvasStem(piece)));
}

// Try to open the plugin's OWN native window. Returns true if it opened, false
// if the plugin is UI-less (openPluginEditor rejects) or the API is missing.
// Used for VSTs we haven't recreated in-app: show the real plugin UI on top
// instead of generic synthesized sliders. Awaits so we can fall back cleanly.
async function rbTryOpenNativeEditor(api, slotId) {
    if (!api || typeof api.openPluginEditor !== 'function' || slotId == null) return false;
    try { await api.openPluginEditor(slotId); return true; }
    catch (e) { console.warn('[rig_builder] this plugin has no native editor window:', e); return false; }
}

// Inline panel shown when a non-recreated VST is being edited in its own native
// window: just a header + Capture (snapshot the params tweaked in that window
// into the tone/master saved state) + close. `captureCall`/`closeCall` are the
// onclick expressions for the relevant scope (tone vs master).
function rbNativeEditorPanelHtml(vstName, captureCall, closeCall) {
    return `
        <div class="flex items-center justify-between mb-1">
            <div class="text-[11px] text-purple-300 font-semibold">Editing in the plugin's own window · ${rbEsc(vstName)}</div>
            <div class="flex items-center gap-1">
                <button onclick="${captureCall}"
                        title="Snapshot the current parameter values into the saved state"
                        class="bg-amber-700/60 hover:bg-amber-600/60 text-amber-100 text-[10px] px-2 py-0.5 rounded">📸 Capture state</button>
                <button onclick="${closeCall}"
                        title="Close editor"
                        class="text-[10px] text-gray-400 hover:text-gray-200 px-1">✕</button>
            </div>
        </div>
        <div class="text-[10px] text-gray-500">This plugin's UI opened in a separate window — tweak it there, then 📸 Capture to save into this tone.</div>`;
}

// Display width for the inline canvas. Keep each VST at a readable default
// size based on its own aspect ratio; max-width:100% in the markup keeps it
// from overflowing a narrow panel.
function rbCanvasDisplayWidth(stem) {
    const sp = window.RBPedalCanvas && window.RBPedalCanvas.specs && window.RBPedalCanvas.specs[stem];
    if (!sp) return 420;
    const aspect = sp.w / sp.h;
    if (aspect <= 1.15) return Math.max(380, Math.min(460, Math.round(aspect * 620)));
    if (aspect > 3) return 1320;
    return Math.max(560, Math.min(860, Math.round(aspect * 430)));
}

// Build the {key: value} map the canvas reads, keyed BOTH by numeric paramId
// AND by param name. Source of truth is the live getParameters snapshot
// (`_vst_param_meta`); in-progress edits in `_vst_params` are overlaid on top.
// Dual keying matters for graphic-EQ plugins whose params are NAMED by band
// frequency ("50","100",…) — a value keyed by name still lands on the right
// fader, and one keyed by id still lands on the right knob.
// Full canvas model (values + logical→real idMap + logical params) for a piece.
function rbCanvasParamModel(piece) {
    return rbBuildCanvasModel((piece && piece._vst_param_meta) || [], (piece && piece._vst_params) || null);
}

// Best-known values for a NON-interactive thumbnail (the piece may never have
// been opened in the editor, so _vst_param_meta is empty). Falls back to the
// piece's saved vst_state (name-keyed), which the canvas resolves by name.
function rbCanvasThumbValues(piece) {
    const v = rbCanvasParamModel(piece).values;
    if (Object.keys(v).length) return v;
    let byName = {};
    try { byName = rbParseVstStateParams(rbEffVstState(piece)) || {}; } catch (_) { byName = {}; }
    if (!Object.keys(byName).length) return byName;
    // A saved vst_state is keyed by VST param NAME. Specs whose controls use
    // numeric logical ids (e.g. the amps) won't resolve those — so when the
    // spec declares a `names` array (logical id -> param name), project the
    // name-keyed values onto the numeric ids too. Name keys are kept as well
    // (harmless; specs that read by name still work).
    try {
        const stem = rbCanvasStem(piece);
        const spec = window.RBPedalCanvas && window.RBPedalCanvas.specs && window.RBPedalCanvas.specs[stem];
        if (spec && Array.isArray(spec.names)) {
            const out = {};
            spec.names.forEach((nm, id) => { if (nm && byName[nm] != null) out[id] = byName[nm]; });
            return Object.assign(out, byName);
        }
    } catch (_) { /* fall through to name-keyed */ }
    return byName;
}

function rbToneRenderInlineVstParams(toneIdx, pIdx) {
    const editor = document.getElementById(`rb-tone-vst-editor-${toneIdx}-${pIdx}`);
    if (!editor) return false;
    const piece = rbState.songTones.tones[toneIdx].chain[pIdx];
    const params = rbFilterVstParams((piece && piece._vst_param_meta) || []);
    const effVstPath = rbEffVstPath(piece);
    const vstName = effVstPath.split('/').pop().replace(/\.(vst3|component)$/i, '');
    // ── In-app canvas UI (no native window): faithful pedal face, draggable
    //    knobs → setParameter + persist into piece._vst_params. ───────────────
    const stem = rbCanvasStem(piece);
    if (window.RBPedalCanvas && (window.RBPedalCanvas.has(stem) || params.length > 0)) {
        editor.innerHTML = `
            <div class="flex items-center justify-between mb-1">
                <div class="text-[11px] text-purple-300 font-semibold">In-Slopsmith editor · ${rbEsc(vstName)}</div>
                <div class="flex items-center gap-1">
                    <button onclick="rbToneCaptureVstState(${toneIdx}, ${pIdx})"
                            title="Snapshot the current parameter values into this tone's saved state"
                            class="bg-amber-700/60 hover:bg-amber-600/60 text-amber-100 text-[10px] px-2 py-0.5 rounded">📸 Capture state</button>
                    <button onclick="rbToneEditVst(${toneIdx}, ${pIdx})"
                            title="Close inline editor"
                            class="text-[10px] text-gray-400 hover:text-gray-200 px-1">✕</button>
                </div>
            </div>
            <div class="flex justify-center">
                <canvas id="rb-tone-vst-canvas-${toneIdx}-${pIdx}" style="width:${rbCanvasDisplayWidth(stem)}px;max-width:100%;cursor:ns-resize;touch-action:none"></canvas>
            </div>
            <div class="text-[10px] text-gray-500 text-center mt-1">Drag a knob up/down to adjust</div>`;
        const canvas = document.getElementById(`rb-tone-vst-canvas-${toneIdx}-${pIdx}`);
        const api = window.slopsmithDesktop && window.slopsmithDesktop.audio;
        const draw = () => {
            const model = rbCanvasParamModel(piece);   // values keyed by logical idx + name; idMap logical→real
            window.RBPedalCanvas.attach(canvas, stem, {
                values: model.values,
                params: model.logicalParams,            // generic fallback lays these out 0,1,2…
                interactive: true,
                onChange: (logicalId, val) => {
                    const realId = model.idMap[logicalId] ?? logicalId;
                    if (piece._vst_slot_id != null && api) { try { api.setParameter(piece._vst_slot_id, realId, val); } catch (_) {} }
                    piece._vst_params = piece._vst_params || {};
                    piece._vst_params[realId] = val;
                },
            });
        };
        // Fonts may still be loading on first open — redraw once they're ready.
        if (window.RBPedalCanvas.ready) window.RBPedalCanvas.ready().then(draw);
        draw();
        return true;
    }
    const header = `
        <div class="flex items-center justify-between">
            <div class="text-[11px] text-purple-300 font-semibold">In-Slopsmith editor · ${rbEsc(vstName)} · ${params.length} params</div>
            <div class="flex items-center gap-1">
                <button onclick="rbToneCaptureVstState(${toneIdx}, ${pIdx})"
                        title="Snapshot the current parameter values into this tone's saved state"
                        class="bg-amber-700/60 hover:bg-amber-600/60 text-amber-100 text-[10px] px-2 py-0.5 rounded">📸 Capture state</button>
                <button onclick="rbToneEditVst(${toneIdx}, ${pIdx})"
                        title="Close inline editor"
                        class="text-[10px] text-gray-400 hover:text-gray-200 px-1">✕</button>
            </div>
        </div>`;
    if (params.length === 0) {
        editor.innerHTML = `
            ${header}
            <div class="text-xs text-gray-500 italic mt-1">
                This plugin doesn't expose any parameters to the host. Use the native window for tweaks.
            </div>`;
        return;
    }
    const rows = params.map((p, i) => {
        const id     = p.id    ?? p.paramId ?? p.index ?? i;
        const name   = p.name  ?? p.label   ?? `Param ${i}`;
        const value  = p.value ?? p.current ?? 0;
        const text   = p.text  ?? p.display ?? '';
        const labelU = p.label_units ?? p.unit ?? '';
        const step   = p.numSteps && p.numSteps > 1 ? (1 / (p.numSteps - 1)) : 0.001;
        const display = text || (typeof value === 'number' ? value.toFixed(3) : value) + (labelU ? ` ${labelU}` : '');
        return `
            <div class="flex items-center gap-2 py-0.5">
                <span class="text-[11px] text-gray-300 w-32 truncate" title="${rbEsc(name)}">${rbEsc(name)}</span>
                <input type="range" min="0" max="1" step="${step}" value="${value}"
                       oninput="rbToneSetVstParam(${toneIdx}, ${pIdx}, ${id}, this.value, this.nextElementSibling)"
                       class="flex-1 h-1 accent-purple-500">
                <span class="text-[10px] text-purple-200/70 w-20 text-right truncate" title="${rbEsc(String(display))}">${rbEsc(String(display))}</span>
            </div>`;
    }).join('');
    editor.innerHTML = `${header}
        <div class="max-h-96 overflow-y-auto mt-1">${rows}</div>`;
}

async function rbToneSetVstParam(toneIdx, pIdx, paramId, value, valueDisplayEl) {
    const api = window.slopsmithDesktop && window.slopsmithDesktop.audio;
    const piece = rbState.songTones.tones[toneIdx].chain[pIdx];
    if (!piece || piece._vst_slot_id == null) return;
    const v = parseFloat(value);
    try { await api.setParameter(piece._vst_slot_id, paramId, v); } catch (_) {}
    if (valueDisplayEl) {
        if (typeof api?.getParameters === 'function') {
            try {
                const refreshed = await api.getParameters(piece._vst_slot_id);
                if (Array.isArray(refreshed)) {
                    const entry = refreshed.find(p => (p.id ?? p.paramId ?? p.index) === paramId);
                    valueDisplayEl.textContent = (entry && (entry.text || entry.display)) || v.toFixed(3);
                    piece._vst_param_meta = refreshed;
                } else {
                    valueDisplayEl.textContent = v.toFixed(3);
                }
            } catch (_) {
                valueDisplayEl.textContent = v.toFixed(3);
            }
        } else {
            rbRecordLegacyNativeLoadBridge('chain loaded through legacy Desktop audio API fallback');
            valueDisplayEl.textContent = v.toFixed(3);
        }
    }
    // Stage the drag in _vst_params + keep _vst_state in sync so ANY
    // subsequent persist (reorder, add piece, master edit, etc.) carries
    // the latest values — not just an explicit Capture state click.
    piece._vst_params = piece._vst_params || {};
    piece._vst_params[paramId] = v;
    rbStampVstState(piece);   // refresh params (opaque is captured at save time)
    // Debounced auto-save so the user doesn't lose drags after navigating
    // away from the song. 500 ms after the last drag we hit /save_preset.
    rbDebouncedToneSave(toneIdx, pIdx);
}

// Per-piece debounce timer. Each new drag resets the timer; the actual
// save fires only when there's been a pause. The 500 ms window keeps the
// save count sane during rapid drags while still feeling instantaneous.
const _rbToneSaveTimers = new Map();
function rbDebouncedToneSave(toneIdx, pIdx) {
    const key = `${toneIdx}:${pIdx}`;
    const existing = _rbToneSaveTimers.get(key);
    if (existing) clearTimeout(existing);
    const timer = setTimeout(async () => {
        _rbToneSaveTimers.delete(key);
        // Capture the engine's opaque state right before persisting so the
        // saved chain restores this VST correctly during real song playback.
        const piece = rbState.songTones && rbState.songTones.tones[toneIdx]
            && rbState.songTones.tones[toneIdx].chain[pIdx];
        if (piece && piece._vst_slot_id != null) {
            const api = window.slopsmithDesktop && window.slopsmithDesktop.audio;
            const opaque = await rbCaptureVstOpaqueState(api,
                piece._vst_path || (piece.assigned && piece.assigned.vst_path));
            rbStampVstState(piece, opaque);
        }
        if (rbState.currentSongFile) {
            rbPersistTone(toneIdx, rbState.currentSongFile).catch(() => null);
        }
    }, 500);
    _rbToneSaveTimers.set(key, timer);
}

async function rbToneCaptureVstState(toneIdx, pIdx) {
    const api = window.slopsmithDesktop && window.slopsmithDesktop.audio;
    const piece = rbState.songTones.tones[toneIdx].chain[pIdx];
    if (!piece) return;
    const editor = document.getElementById(`rb-tone-vst-editor-${toneIdx}-${pIdx}`);
    try {
        let params = piece._vst_params || {};
        if (piece._vst_slot_id != null && typeof api?.getParameters === 'function') {
            const live = await api.getParameters(piece._vst_slot_id).catch(() => null);
            if (Array.isArray(live)) {
                params = {};
                for (let i = 0; i < live.length; i++) {
                    const id = live[i].id ?? live[i].paramId ?? live[i].index ?? i;
                    const v  = live[i].value ?? live[i].current;
                    if (typeof v === 'number') params[id] = v;
                }
            }
        }
        piece._vst_params = params;
        // Also grab the engine's opaque state blob — the only thing that
        // restores this VST's settings during real song playback.
        const opaque = await rbCaptureVstOpaqueState(api,
            piece._vst_path || (piece.assigned && piece.assigned.vst_path));
        rbStampVstState(piece, opaque);
        // Persist through the existing tone-save path.
        if (rbState.currentSongFile) {
            await rbPersistTone(toneIdx, rbState.currentSongFile).catch(() => null);
        }
        if (editor) {
            const status = document.createElement('div');
            status.className = 'text-[10px] text-emerald-300';
            status.textContent = opaque
                ? `✓ Captured ${Object.keys(params).length} params + full state`
                : `✓ Captured ${Object.keys(params).length} param values`;
            editor.appendChild(status);
            setTimeout(() => status.remove(), 2500);
        }
    } catch (e) {
        alert(`Capture failed: ${e.message || e}`);
    }
}

// ── Library label helpers (still used by piece + catalog renderers) ────
//
// The per-song "📚 Library" button was removed once the 🔁 Swap and the
// Gear-catalog 📚 Library cover the same ground without duplication.
// These two short helpers stayed because the piece/catalog renderers
// still call rbLibShortName / rbLibLabel to humanise tone3000 filenames.

// Short, readable form of a downloaded filename: drop the
// tone3000_<id>_m<model>_ prefix and the extension, leaving the
// descriptive tail. Non-tone3000 files just lose their extension.
function rbLibShortName(name) {
    const base = String(name || '').replace(/\.[^./]+$/, '');
    const m = base.match(/^tone3000_\d+_m\d+_(.+)$/);
    return m ? m[1] : base;
}

// Disambiguate library rows that share a tone3000 title (several
// captures can all be called "EQ"): show the title, and append the
// technical filename in muted text only when the title alone is
// ambiguous within the visible list.
function rbLibLabel(file, titleCounts) {
    const t = file.title;
    const short = rbLibShortName(file.name);
    if (!t) return rbEsc(short);
    if ((titleCounts[t] || 0) > 1) {
        return `${rbEsc(t)} <span class="text-gray-500">· ${rbEsc(short)}</span>`;
    }
    return rbEsc(t);
}

// ── Per-song variant override + per-song gear swap ──────────────────────
//
// Two related editor flows scoped to a single preset (one song's tone):
//
//   rbPickVariant    — force a curated gain variant (clean/crunch/dist)
//                      for an amp with multi-NAM gain_variants. Backed
//                      by POST /piece_variant_override.
//
//   rbToggleGearSwap — open a category-filtered picker showing curated
//                      gears with photos. Picking one swaps THIS song's
//                      piece to that gear's current All Gear assignment
//                      (VST when assigned, otherwise fallback NAM/IR).
//                      Backed by POST
//                      /gear/replace_with with `preset_id`.
//
// Both operations mark `assigned_mode='manual'` on the row so a Remap
// All sweep won't undo the user's choice. Both refresh the song view
// from the server so all derived state (amp_variant badge, primaries,
// stage labels) re-renders consistently.

async function rbPickVariant(toneIdx, pIdx, level) {
    const tone = rbState.songTones && rbState.songTones.tones && rbState.songTones.tones[toneIdx];
    const piece = tone && tone.chain && tone.chain[pIdx];
    if (!tone || !piece) return;
    // Save first if this tone has never been persisted (no preset_id) —
    // the override endpoint needs an existing row to UPDATE.
    let presetId = tone.preset_id;
    if (presetId == null) {
        presetId = await rbPersistTone(toneIdx);
        if (presetId == null) {
            alert('Could not persist the tone before overriding the variant.');
            return;
        }
    }
    try {
        const r = await fetch(`${window.RB_API}/piece_variant_override`, {
            method: 'POST', headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({
                preset_id: presetId,
                rs_gear: piece.type,
                variant: level || 'auto',
            }),
        });
        if (!r.ok) {
            const err = await r.json().catch(() => ({}));
            alert(`Variant override failed: ${err.error || r.status}`);
            return;
        }
        await rbRefreshSongAfterEdit(toneIdx);
    } catch (e) {
        alert(`Variant override failed: ${e.message || e}`);
    }
}

async function rbToggleGearSwap(toneIdx, pIdx) {
    const panel = document.getElementById(`rb-swap-${toneIdx}-${pIdx}`);
    if (!panel) return;
    if (!panel.classList.contains('hidden')) {
        panel.classList.add('hidden');
        return;
    }
    panel.classList.remove('hidden');
    const piece = rbState.songTones.tones[toneIdx].chain[pIdx];
    const category = piece.rs_category || 'amp';
    panel.innerHTML = `<div class="text-xs text-gray-500">Loading ${rbEsc(category)}s…</div>`;
    try {
        const gears = await rbLoadGearsInCategory(category);
        rbRenderGearSwapPanel(panel, gears, piece, toneIdx, pIdx);
    } catch (e) {
        panel.innerHTML = `<div class="text-xs text-red-400">Failed to load gears: ${rbEsc(e.message || e)}</div>`;
    }
}

// Cached fetch of /gears_in_category so opening the picker on a second
// piece in the same session is instant. Cached at the module level —
// invalidated by an explicit window.__rbGearCatCache = null if needed.
async function rbLoadGearsInCategory(category) {
    window.__rbGearCatCache = window.__rbGearCatCache || {};
    if (window.__rbGearCatCache[category]) return window.__rbGearCatCache[category];
    const r = await fetch(`${window.RB_API}/gears_in_category/${encodeURIComponent(category)}`);
    if (!r.ok) throw new Error(`HTTP ${r.status}`);
    const data = await r.json();
    window.__rbGearCatCache[category] = data.gears || [];
    return window.__rbGearCatCache[category];
}

function rbRenderGearSwapPanel(panel, gears, piece, toneIdx, pIdx) {
    const fromGear = piece.type;
    const cards = gears.map(g => {
        const dim = g.rs_gear === fromGear ? 'opacity-40 cursor-not-allowed' : 'hover:bg-amber-900/30 cursor-pointer';
        const img = g.image
            ? `<img src="${rbEsc(g.image)}" alt="" loading="lazy" style="width:36px;height:36px;object-fit:cover" class="w-9 h-9 rounded object-cover bg-dark-900 flex-shrink-0">`
            : `<div class="w-9 h-9 rounded bg-dark-900 flex items-center justify-center text-gray-700 text-[9px] flex-shrink-0">no photo</div>`;
        const variantBadge = g.variant_count > 0
            ? `<span class="text-[9px] text-emerald-400 bg-emerald-900/30 border border-emerald-800/40 rounded px-1">${g.variant_count}×</span>`
            : `<span class="text-[9px] text-gray-600">no variants</span>`;
        const onclick = g.rs_gear === fromGear ? '' :
            `onclick="rbConfirmGearSwap(${toneIdx}, ${pIdx}, '${rbEsc(g.rs_gear)}')"`;
        return `
            <div ${onclick} class="flex items-center gap-2 p-1.5 rounded ${dim}">
                ${img}
                <div class="min-w-0 flex-1">
                    <div class="text-xs text-gray-200 truncate">${rbEsc(g.name)}</div>
                    <div class="text-[10px] text-gray-500 truncate">${rbEsc(g.rs_gear)}</div>
                </div>
                ${variantBadge}
            </div>`;
    }).join('');
    const inputId = `rb-swap-search-${toneIdx}-${pIdx}`;
    panel.innerHTML = `
        <div class="flex items-center gap-2 mb-2">
            <span class="text-[11px] text-amber-300">🔁 Swap with…</span>
            <input id="${inputId}" type="text" placeholder="🔍 Filter gears…"
                   oninput="rbFilterGearSwap(${toneIdx}, ${pIdx})"
                   class="flex-1 bg-dark-800 border border-gray-800 rounded text-[11px] text-gray-200 px-2 py-0.5">
            <span class="text-[10px] text-gray-500">${gears.length} gears</span>
        </div>
        <div id="rb-swap-rows-${toneIdx}-${pIdx}" class="max-h-72 overflow-y-auto grid grid-cols-2 gap-1">${cards}</div>
        <div class="text-[10px] text-gray-500 italic mt-2">Uses the target gear's current All Gear assignment. Cabs are skipped — use the IR dropdown instead.</div>`;
    panel._rbGearList = gears;
    panel._rbToneIdx = toneIdx;
    panel._rbPIdx = pIdx;
    panel._rbFromGear = fromGear;
}

function rbFilterGearSwap(toneIdx, pIdx) {
    const panel = document.getElementById(`rb-swap-${toneIdx}-${pIdx}`);
    if (!panel || !panel._rbGearList) return;
    const input = document.getElementById(`rb-swap-search-${toneIdx}-${pIdx}`);
    const rows = document.getElementById(`rb-swap-rows-${toneIdx}-${pIdx}`);
    if (!input || !rows) return;
    const q = (input.value || '').toLowerCase().trim();
    const filtered = q
        ? panel._rbGearList.filter(g => (g.name + ' ' + g.rs_gear).toLowerCase().includes(q))
        : panel._rbGearList;
    const fromGear = panel._rbFromGear;
    rows.innerHTML = filtered.map(g => {
        const dim = g.rs_gear === fromGear ? 'opacity-40 cursor-not-allowed' : 'hover:bg-amber-900/30 cursor-pointer';
        const img = g.image
            ? `<img src="${rbEsc(g.image)}" alt="" loading="lazy" style="width:36px;height:36px;object-fit:cover" class="w-9 h-9 rounded object-cover bg-dark-900 flex-shrink-0">`
            : `<div class="w-9 h-9 rounded bg-dark-900 flex items-center justify-center text-gray-700 text-[9px] flex-shrink-0">no photo</div>`;
        const variantBadge = g.variant_count > 0
            ? `<span class="text-[9px] text-emerald-400 bg-emerald-900/30 border border-emerald-800/40 rounded px-1">${g.variant_count}×</span>`
            : `<span class="text-[9px] text-gray-600">no variants</span>`;
        const onclick = g.rs_gear === fromGear ? '' :
            `onclick="rbConfirmGearSwap(${toneIdx}, ${pIdx}, '${rbEsc(g.rs_gear)}')"`;
        return `
            <div ${onclick} class="flex items-center gap-2 p-1.5 rounded ${dim}">
                ${img}
                <div class="min-w-0 flex-1">
                    <div class="text-xs text-gray-200 truncate">${rbEsc(g.name)}</div>
                    <div class="text-[10px] text-gray-500 truncate">${rbEsc(g.rs_gear)}</div>
                </div>
                ${variantBadge}
            </div>`;
    }).join('') || '<div class="text-xs text-gray-500 italic col-span-2">no matches</div>';
}

async function rbConfirmGearSwap(toneIdx, pIdx, toRsGear) {
    const tone = rbState.songTones && rbState.songTones.tones && rbState.songTones.tones[toneIdx];
    const piece = tone && tone.chain && tone.chain[pIdx];
    if (!tone || !piece) return;
    // Save first if no preset_id — replace_with needs an existing row.
    let presetId = tone.preset_id;
    if (presetId == null) {
        presetId = await rbPersistTone(toneIdx);
        if (presetId == null) {
            alert('Could not persist the tone before swapping the gear.');
            return;
        }
    }
    try {
        const r = await fetch(`${window.RB_API}/gear/replace_with`, {
            method: 'POST', headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({
                preset_id: presetId,
                from_rs_gear: piece.type,
                to_rs_gear: toRsGear,
            }),
        });
        if (!r.ok) {
            const err = await r.json().catch(() => ({}));
            alert(`Gear swap failed: ${err.error || r.status}`);
            return;
        }
        const data = await r.json();
        if (data.pieces_updated === 0) {
            alert('Swap failed — this gear has no NAM or VST associated yet. Open the All Gear tab and assign one to this gear first, then try the swap again.');
            return;
        }
        // Collapse the swap panel and refresh.
        const panel = document.getElementById(`rb-swap-${toneIdx}-${pIdx}`);
        if (panel) panel.classList.add('hidden');
        await rbRefreshSongAfterEdit(toneIdx);
    } catch (e) {
        alert(`Gear swap failed: ${e.message || e}`);
    }
}

// Refresh the open song from the server after a server-side edit
// (variant override / gear swap / etc.) so derived state shown in the
// piece cards reflects what the next ▶ Listen will load. Falls back to
// silently no-op if no song is open (shouldn't happen from these flows).
async function rbRefreshSongAfterEdit(toneIdx) {
    const filename = rbState.songTones && rbState.songTones.filename;
    if (!filename) return;
    try {
        const r = await fetch(`${window.RB_API}/song/${encodeURIComponent(filename)}`);
        if (!r.ok) return;
        const fresh = await r.json();
        // Seed bypass on the fresh data BEFORE replacing rbState so the
        // re-rendered chain shows the right bypass state. rbSeedBypass
        // walks data.tones[*].chain[*] and copies bypassed → _bypassed.
        if (typeof rbSeedBypass === 'function') rbSeedBypass(fresh);
        rbState.songTones = fresh;
        // Re-render only the affected chain to keep scroll position.
        rbReRenderToneChain(toneIdx, filename);
    } catch (_) { /* ignore */ }
}

// ── Master chain (global pre/post FX) ──────────────────────────────────
//
// The "Master Chain" tab edits two sentinel chains kept in preset_pieces
// under reserved preset names (`__rig_builder_master_pre__` /
// `__rig_builder_master_post__`). The backend's native_preset_full
// prepends master_pre stages + appends master_post stages around every
// per-tone chain, so e.g. an input gate + output limiter stay applied
// regardless of which song / tone is loaded.

rbState.master = { pre: [], post: [] };

async function rbLoadMasterChain() {
    const statusEl = document.getElementById('rb-master-status');
    if (statusEl) statusEl.textContent = 'Loading master chain…';
    try {
        const r = await fetch(`${window.RB_API}/master_chain`);
        if (!r.ok) throw new Error(`HTTP ${r.status}`);
        const data = await r.json();
        rbState.master.pre  = Array.isArray(data.pre)  ? data.pre  : [];
        rbState.master.post = Array.isArray(data.post) ? data.post : [];
        if (statusEl) {
            const n = rbState.master.pre.length + rbState.master.post.length;
            statusEl.textContent = n > 0
                ? `${rbState.master.pre.length} pre · ${rbState.master.post.length} post`
                : 'No master pieces configured yet — every song uses just its own chain.';
        }
    } catch (e) {
        if (statusEl) statusEl.textContent = `Failed to load master chain: ${e.message || e}`;
        rbState.master.pre = [];
        rbState.master.post = [];
    }
    rbRenderMasterChain('pre');
    rbRenderMasterChain('post');
}

function rbRenderMasterChain(role) {
    const list = rbState.master[role] || [];
    const container = document.getElementById(`rb-master-${role}-chain`);
    const counter   = document.getElementById(`rb-master-${role}-count`);
    if (!container) return;
    if (counter) counter.textContent = `${list.length} piece${list.length === 1 ? '' : 's'}`;
    if (list.length === 0) {
        container.innerHTML = `
            <div class="text-xs text-gray-500 italic bg-dark-800/40 border border-dashed border-gray-800/50 rounded p-3">
                No ${role}-FX yet. Use "＋ Add ${role} piece" below to start.
            </div>`;
        return;
    }
    container.innerHTML = list.map((p, i) => rbRenderMasterPiece(role, i, p, list.length)).join('');
}

function rbRenderMasterPiece(role, idx, p, total) {
    const isFirst = idx === 0;
    const isLast  = idx === total - 1;
    const accent  = role === 'pre' ? 'emerald' : 'cyan';
    // Effective assignment label.
    const pendingKind = p._uploaded_kind || p._vst_kind;
    const assignedKind = p.assigned && p.assigned.kind;
    const effKind = pendingKind || assignedKind || 'none';
    const effVstPath = rbEffVstPath(p);
    const effFile = rbEffFile(p);
    let label, labelClass;
    if (effKind === 'vst' && effVstPath) {
        label = `✓ VST: ${effVstPath.split('/').pop()}`;
        labelClass = 'text-purple-300';
    } else if (effFile) {
        label = `✓ ${effFile}`;
        labelClass = 'text-green-400';
    } else {
        label = '(unassigned — click Assign to pick a file or VST)';
        labelClass = 'text-gray-500';
    }
    const bypassed = !!p._bypassed || !!(p.assigned && p.assigned.bypassed);
    const pickerId = `rb-master-${role}-picker-piece-${idx}`;
    return `
        <div class="bg-dark-800 border border-${accent}-900/30 rounded-lg p-3" data-role="${role}" data-idx="${idx}">
            <div class="flex items-center justify-between mb-2">
                <div class="flex items-center gap-2 min-w-0">
                    <span class="flex-shrink-0 w-6 h-6 rounded-full bg-dark-900 border border-${accent}-800/40 text-[11px] text-${accent}-300 flex items-center justify-center font-mono">
                        ${idx + 1}
                    </span>
                    <div class="min-w-0">
                        <div class="text-sm text-gray-200 truncate">${rbEsc(p.real_name || p.type)}</div>
                        <div class="text-xs text-gray-500 truncate">${rbEsc(p.rs_category || p.category || 'other')} · ${rbEsc(p.type)}</div>
                    </div>
                </div>
                <div class="flex items-center gap-1 flex-shrink-0">
                    <button onclick="rbMasterMovePiece('${role}', ${idx}, -1)" ${isFirst ? 'disabled' : ''}
                            class="px-1.5 py-1 rounded text-xs transition ${isFirst ? 'bg-dark-700/40 text-gray-700 cursor-not-allowed' : 'bg-dark-600 hover:bg-dark-500 text-gray-300'}">▲</button>
                    <button onclick="rbMasterMovePiece('${role}', ${idx}, 1)" ${isLast ? 'disabled' : ''}
                            class="px-1.5 py-1 rounded text-xs transition ${isLast ? 'bg-dark-700/40 text-gray-700 cursor-not-allowed' : 'bg-dark-600 hover:bg-dark-500 text-gray-300'}">▼</button>
                    <button onclick="rbMasterRemovePiece('${role}', ${idx})"
                            class="px-1.5 py-1 rounded text-xs bg-red-900/40 hover:bg-red-900/60 text-red-300 border border-red-800/40 transition">✗</button>
                    <button onclick="rbMasterToggleBypass('${role}', ${idx}, this)"
                            class="px-2 py-1 rounded text-xs transition ${bypassed ? 'bg-amber-700/40 text-amber-300 border border-amber-600/40' : 'bg-dark-600 hover:bg-dark-500 text-gray-300'}">
                        ${bypassed ? '⤳ Bypassed' : 'Bypass'}
                    </button>
                </div>
            </div>
            <div class="flex items-center gap-2">
                <span class="flex-1 text-xs ${labelClass} truncate" title="${rbEsc(effVstPath || effFile || '')}">${rbEsc(label)}</span>
                ${effVstPath ? `
                <button onclick="rbMasterEditVst('${role}', ${idx})"
                        title="Load this VST in the engine and edit its parameters with inline sliders"
                        class="bg-purple-900/30 hover:bg-purple-900/50 text-purple-300 border border-purple-800/40 px-2 py-1 rounded text-xs transition">
                    🎛 Edit VST
                </button>` : ''}
                <button onclick="rbMasterOpenAssignPicker('${role}', ${idx})"
                        class="bg-${accent}-900/30 hover:bg-${accent}-900/50 text-${accent}-300 border border-${accent}-800/40 px-2 py-1 rounded text-xs transition">
                    Assign…
                </button>
            </div>
            <div id="${pickerId}" class="hidden mt-2 bg-dark-900/40 border border-gray-800/40 rounded p-2 space-y-2"></div>
            <div id="rb-master-${role}-editor-${idx}" class="hidden mt-2 bg-purple-900/10 border border-purple-800/30 rounded p-2 space-y-2"></div>
        </div>`;
}

// State mutation helpers — all auto-save via rbPersistMasterChain.

function rbMasterMovePiece(role, idx, direction) {
    const arr = rbState.master[role];
    const newIdx = idx + direction;
    if (newIdx < 0 || newIdx >= arr.length) return;
    const tmp = arr[idx];
    arr[idx] = arr[newIdx];
    arr[newIdx] = tmp;
    rbAfterMasterEdit(role);
}

async function rbMasterRemovePiece(role, idx) {
    const arr = rbState.master[role];
    const piece = arr[idx];
    const name = piece?.real_name || piece?.type || 'piece';
    if (!confirm(`Remove "${name}" from master ${role} chain?`)) return;
    arr.splice(idx, 1);
    rbAfterMasterEdit(role);
}

function rbMasterToggleBypass(role, idx, btn) {
    const arr = rbState.master[role];
    const p = arr[idx];
    if (!p) return;
    p._bypassed = !p._bypassed;
    rbAfterMasterEdit(role);
}

async function rbAfterMasterEdit(role) {
    await rbPersistMasterChain(role).catch(() => null);
    rbRenderMasterChain(role);
    // If a tone preview is running, reload it so the new master wrap is heard live.
    if (rbState.listeningTone !== null && rbState._previewPayload?.id) {
        await rbReloadPreview(rbState._previewPayload.id).catch(() => {});
    }
}

async function rbPersistMasterChain(role) {
    const arr = rbState.master[role] || [];
    const pieces = arr.map(p => {
        const isVst = p._vst_kind === 'vst' || (p.assigned && p.assigned.kind === 'vst' && p.assigned.vst_path);
        if (isVst) {
            return {
                slot: p.slot || `master_${role}`,
                rs_gear_type: p.type,
                kind: 'vst',
                file: null,
                vst_path: rbEffVstPath(p),
                vst_format: rbEffVstFormat(p),
                vst_state: rbEffVstState(p),
                params: {},
                assigned_mode: 'master',
                bypassed: !!p._bypassed,
            };
        }
        const file = rbEffFile(p);
        const kindRaw = rbEffKind(p);
        const kind = kindRaw || (file ? (p.rs_category === 'cab' ? 'ir' : 'nam') : 'none');
        return {
            slot: p.slot || `master_${role}`,
            rs_gear_type: p.type,
            kind,
            file,
            params: {},
            assigned_mode: 'master',
            bypassed: !!p._bypassed,
        };
    });
    try {
        const r = await fetch(`${window.RB_API}/master_chain/save`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ role, pieces }),
        });
        if (!r.ok) {
            const err = await r.json().catch(() => ({}));
            alert(`Save master ${role} failed: ${err.error || r.status}`);
            return null;
        }
        return await r.json();
    } catch (e) {
        alert(`Save master ${role} failed: ${e.message || e}`);
        return null;
    }
}

// ── Master VST inline editor ──
//
// Lets the user load a master VST in the engine, see its parameters as
// HTML sliders (no blurry native window), tweak them in real time via
// setParameter, and capture the resulting state back into the master
// piece. Mirrors the per-tone rbLoadAndEditVst / rbRenderInlineVstParams
// flow but reads/writes against rbState.master[role][idx] instead of
// rbState.songTones.tones[toneIdx].chain[pIdx].

async function rbMasterEditVst(role, idx) {
    const piece = rbState.master[role][idx];
    if (!piece) return;
    const editor = document.getElementById(`rb-master-${role}-editor-${idx}`);
    if (!editor) return;
    const api = window.slopsmithDesktop && window.slopsmithDesktop.audio;
    // Toggle close if already open — tear the editor VST down cleanly.
    if (!editor.classList.contains('hidden')) {
        editor.classList.add('hidden');
        editor.innerHTML = '';
        await rbTeardownVstEditor(api);
        piece._vst_slot_id = null;
        return;
    }
    if (!api) {
        alert('Native VST hosting not available');
        return;
    }
    const vstPath = rbEffVstPath(piece);
    if (!vstPath) {
        alert('This piece has no VST assigned yet — use Assign… first.');
        return;
    }
    if (rbState._vstEditorBusy) return;   // ignore rapid double-clicks while a load is in flight
    rbState._vstEditorBusy = true;
    editor.classList.remove('hidden');
    editor.innerHTML = `<div class="text-xs text-gray-500">loading ${rbEsc(vstPath.split('/').pop())}…</div>`;
    try {
        // Close + clear any previously-open editor (this or another piece)
        // before loading — closing its native window first avoids the crash.
        await rbTeardownVstEditor(api);
        await api.startAudio().catch(() => {});
        const slotId = await rbSafeLoadStandaloneVst(api, vstPath);
        if (slotId == null || slotId < 0) {
            editor.innerHTML = `<div class="text-xs text-red-400">${rbEsc(rbVstRefusedMsg())}</div>`;
            return;
        }
        rbState._vstEditorSlot = slotId;
        piece._vst_slot_id = slotId;
        // Keep any previously-saved opaque blob so re-saving without a fresh
        // capture doesn't drop it.
        piece._vst_opaque = piece._vst_opaque
            || rbParseVstStateOpaque(piece._vst_state)
            || rbParseVstStateOpaque(piece.assigned && piece.assigned.vst_state);
        // Re-apply any previously-captured param state. Helper resolves
        // NAME keys → numeric ids and clamps to [0,1]; same fix as the
        // per-tone editor path.
        const saved = piece._vst_params
            || (piece.assigned && piece.assigned.vst_state
                ? rbParseVstStateParams(piece.assigned.vst_state) : null);
        const params = await rbRestoreSavedParamsToSlot(api, slotId, saved, vstPath)
        piece._vst_param_meta = params;
        // Seed _vst_params with the FULL current snapshot. Without this,
        // subsequent slider drags would write a PARTIAL dict — untouched
        // params would silently revert to plugin defaults on the next
        // chain rebuild. Now any drag modifies a complete state.
        piece._vst_params = {};
        for (const param of params) {
            const id = param.id ?? param.paramId ?? param.index;
            const v  = param.value ?? param.current;
            if (id != null && typeof v === 'number') piece._vst_params[id] = v;
        }
        // Editor priority: (1) faithful in-app canvas recreation; (2) the
        // plugin's OWN native window for VSTs we haven't recreated; (3) generic
        // in-app sliders only when the plugin is UI-less (no native window).
        if (rbHasCanvasUI(piece)) {
            rbMasterRenderInlineVstParams(role, idx);
        } else if (await rbTryOpenNativeEditor(api, slotId)) {
            const _nm = vstPath.split('/').pop().replace(/\.(vst3|component)$/i, '');
            editor.innerHTML = rbNativeEditorPanelHtml(
                _nm, `rbMasterCaptureVstState('${role}', ${idx})`, `rbMasterEditVst('${role}', ${idx})`);
        } else {
            rbMasterRenderInlineVstParams(role, idx);   // UI-less fallback: generic sliders
        }
    } catch (e) {
        editor.innerHTML = `<div class="text-xs text-red-400">load failed: ${rbEsc(rbFriendlyVstLoadError(e))}</div>`;
    } finally {
        rbState._vstEditorBusy = false;
    }
}

function rbMasterRenderInlineVstParams(role, idx) {
    const editor = document.getElementById(`rb-master-${role}-editor-${idx}`);
    if (!editor) return false;
    const piece = rbState.master[role][idx];
    const params = rbFilterVstParams((piece && piece._vst_param_meta) || []);
    const vstName = (piece._vst_path || '').split('/').pop().replace(/\.(vst3|component)$/i, '');
    // ── In-app canvas UI (no native window) ──────────────────────────────────
    const stem = rbCanvasStem(piece);
    if (window.RBPedalCanvas && (window.RBPedalCanvas.has(stem) || params.length > 0)) {
        editor.innerHTML = `
            <div class="flex items-center justify-between mb-1">
                <div class="text-[11px] text-purple-300 font-semibold">In-Slopsmith editor · ${rbEsc(vstName)}</div>
                <div class="flex items-center gap-1">
                    <button onclick="rbMasterCaptureVstState('${role}', ${idx})"
                            title="Snapshot the current parameter values into the master chain's saved state"
                            class="bg-amber-700/60 hover:bg-amber-600/60 text-amber-100 text-[10px] px-2 py-0.5 rounded">📸 Capture state</button>
                    <button onclick="rbMasterEditVst('${role}', ${idx})"
                            title="Close inline editor (the VST stays loaded in the master chain)"
                            class="text-[10px] text-gray-400 hover:text-gray-200 px-1">✕</button>
                </div>
            </div>
            <div class="flex justify-center">
                <canvas id="rb-master-${role}-canvas-${idx}" style="width:${rbCanvasDisplayWidth(stem)}px;max-width:100%;cursor:ns-resize;touch-action:none"></canvas>
            </div>
            <div class="text-[10px] text-gray-500 text-center mt-1">Drag a knob up/down to adjust</div>`;
        const canvas = document.getElementById(`rb-master-${role}-canvas-${idx}`);
        const api = window.slopsmithDesktop && window.slopsmithDesktop.audio;
        const draw = () => {
            const model = rbCanvasParamModel(piece);
            window.RBPedalCanvas.attach(canvas, stem, {
                values: model.values,
                params: model.logicalParams,
                interactive: true,
                onChange: (logicalId, val) => {
                    const realId = model.idMap[logicalId] ?? logicalId;
                    if (piece._vst_slot_id != null && api) { try { api.setParameter(piece._vst_slot_id, realId, val); } catch (_) {} }
                    piece._vst_params = piece._vst_params || {};
                    piece._vst_params[realId] = val;
                },
            });
        };
        if (window.RBPedalCanvas.ready) window.RBPedalCanvas.ready().then(draw);
        draw();
        return true;
    }
    const header = `
        <div class="flex items-center justify-between">
            <div class="text-[11px] text-purple-300 font-semibold">In-Slopsmith editor · ${vstName} · ${params.length} params</div>
            <div class="flex items-center gap-1">
                <button onclick="rbMasterCaptureVstState('${role}', ${idx})"
                        title="Snapshot the current parameter values into the master chain's saved state"
                        class="bg-amber-700/60 hover:bg-amber-600/60 text-amber-100 text-[10px] px-2 py-0.5 rounded">📸 Capture state</button>
                <button onclick="rbMasterEditVst('${role}', ${idx})"
                        title="Close inline editor (the VST stays loaded in the master chain)"
                        class="text-[10px] text-gray-400 hover:text-gray-200 px-1">✕</button>
            </div>
        </div>`;
    if (params.length === 0) {
        editor.innerHTML = `
            ${header}
            <div class="text-xs text-gray-400 mt-1 space-y-1">
                <div>This effect exposes no host-automatable parameters — that's normal
                for some plugins (denoisers, analyzers, utilities). It's not a failure,
                and your settings <b>can</b> still be saved.</div>
                <div>Edit it in the plugin's own editor window, then click
                <b>📸 Capture state</b> above — that snapshots the plugin's current
                state into the master chain so it persists and reloads on playback.</div>
            </div>`;
        return;
    }
    const rows = params.map((p, i) => {
        const id     = p.id    ?? p.paramId ?? p.index ?? i;
        const name   = p.name  ?? p.label   ?? `Param ${i}`;
        const value  = p.value ?? p.current ?? 0;
        const text   = p.text  ?? p.display ?? '';
        const labelU = p.label_units ?? p.unit ?? '';
        const step   = p.numSteps && p.numSteps > 1 ? (1 / (p.numSteps - 1)) : 0.001;
        const display = text || (typeof value === 'number' ? value.toFixed(3) : value) + (labelU ? ` ${labelU}` : '');
        return `
            <div class="flex items-center gap-2 py-0.5">
                <span class="text-[11px] text-gray-300 w-32 truncate" title="${rbEsc(name)}">${rbEsc(name)}</span>
                <input type="range" min="0" max="1" step="${step}" value="${value}"
                       oninput="rbMasterSetVstParam('${role}', ${idx}, ${id}, this.value, this.nextElementSibling)"
                       class="flex-1 h-1 accent-purple-500">
                <span class="text-[10px] text-purple-200/70 w-20 text-right truncate" title="${rbEsc(String(display))}">${rbEsc(String(display))}</span>
            </div>`;
    }).join('');
    editor.innerHTML = `${header}
        <div class="max-h-96 overflow-y-auto mt-1">${rows}</div>`;
}

async function rbMasterSetVstParam(role, idx, paramId, value, valueDisplayEl) {
    const api = window.slopsmithDesktop && window.slopsmithDesktop.audio;
    const piece = rbState.master[role][idx];
    if (!piece || piece._vst_slot_id == null) return;
    const v = parseFloat(value);
    try { await api.setParameter(piece._vst_slot_id, paramId, v); } catch (_) {}
    // Update the display next to the slider with the plugin's formatted text.
    if (valueDisplayEl) {
        if (typeof api?.getParameters === 'function') {
            try {
                const refreshed = await api.getParameters(piece._vst_slot_id);
                if (Array.isArray(refreshed)) {
                    const entry = refreshed.find(p => (p.id ?? p.paramId ?? p.index) === paramId);
                    valueDisplayEl.textContent = (entry && (entry.text || entry.display)) || v.toFixed(3);
                    piece._vst_param_meta = refreshed;
                } else {
                    valueDisplayEl.textContent = v.toFixed(3);
                }
            } catch (_) {
                valueDisplayEl.textContent = v.toFixed(3);
            }
        } else {
            valueDisplayEl.textContent = v.toFixed(3);
        }
    }
    // Stage the drag + keep _vst_state in sync so any subsequent persist
    // carries the latest values without needing an explicit Capture.
    piece._vst_params = piece._vst_params || {};
    piece._vst_params[paramId] = v;
    rbStampVstState(piece);   // refresh params (opaque is captured at save time)
    // Debounced auto-save (500 ms after last drag) so the user doesn't
    // lose drags after navigating away from the Master tab.
    rbDebouncedMasterSave(role);
}

const _rbMasterSaveTimers = new Map();
function rbDebouncedMasterSave(role) {
    const existing = _rbMasterSaveTimers.get(role);
    if (existing) clearTimeout(existing);
    const timer = setTimeout(async () => {
        _rbMasterSaveTimers.delete(role);
        // Capture the opaque state of the master piece being edited (matched
        // by the live editor slot) before persisting, so it applies in songs.
        const arr = rbState.master[role] || [];
        const piece = arr.find(p => p && p._vst_slot_id != null
            && p._vst_slot_id === rbState._vstEditorSlot);
        if (piece) {
            const api = window.slopsmithDesktop && window.slopsmithDesktop.audio;
            const opaque = await rbCaptureVstOpaqueState(api,
                piece._vst_path || (piece.assigned && piece.assigned.vst_path));
            rbStampVstState(piece, opaque);
        }
        rbPersistMasterChain(role).catch(() => null);
    }, 500);
    _rbMasterSaveTimers.set(role, timer);
}

async function rbMasterCaptureVstState(role, idx) {
    const api = window.slopsmithDesktop && window.slopsmithDesktop.audio;
    const piece = rbState.master[role][idx];
    if (!piece) return;
    const editor = document.getElementById(`rb-master-${role}-editor-${idx}`);
    try {
        // Snapshot the live values (preferred over the staged dict because
        // it survives even when the user changed something via the plugin's
        // own native editor instead of our sliders).
        let params = piece._vst_params || {};
        if (piece._vst_slot_id != null && typeof api?.getParameters === 'function') {
            const live = await api.getParameters(piece._vst_slot_id).catch(() => null);
            if (Array.isArray(live)) {
                params = {};
                for (let i = 0; i < live.length; i++) {
                    const id = live[i].id ?? live[i].paramId ?? live[i].index ?? i;
                    const v  = live[i].value ?? live[i].current;
                    if (typeof v === 'number') params[id] = v;
                }
            }
        }
        piece._vst_params = params;
        // Also grab the engine's opaque state blob — the only thing that
        // restores this VST's settings during real song playback.
        const opaque = await rbCaptureVstOpaqueState(api,
            piece._vst_path || (piece.assigned && piece.assigned.vst_path));
        rbStampVstState(piece, opaque);
        // Persist via the existing save flow so the state survives reload.
        await rbPersistMasterChain(role).catch(() => null);
        if (editor) {
            const status = document.createElement('div');
            status.className = 'text-[10px] text-emerald-300';
            status.textContent = opaque
                ? `✓ Captured ${Object.keys(params).length} params + full state`
                : `✓ Captured ${Object.keys(params).length} param values`;
            editor.appendChild(status);
            setTimeout(() => status.remove(), 2500);
        }
    } catch (e) {
        alert(`Capture failed: ${e.message || e}`);
    }
}

// ── Master Add-piece picker ──
async function rbOpenMasterAddPiecePicker(role) {
    const picker = document.getElementById(`rb-master-${role}-picker`);
    if (!picker) return;
    if (!picker.classList.contains('hidden')) {
        picker.classList.add('hidden');
        picker.innerHTML = '';
        return;
    }
    picker.classList.remove('hidden');
    picker.innerHTML = `<div class="text-xs text-gray-500">Loading gear catalog…</div>`;
    if (_rbGearsCatalog === null) {
        try {
            const r = await fetch(`${window.RB_API}/gears_catalog`);
            const data = await r.json();
            _rbGearsCatalog = (data && data.gears) || [];
        } catch (_) { _rbGearsCatalog = []; }
    }
    // Initialise per-picker state. Defaults: the game section, DAW
    // category that makes sense for each master role.
    picker._rbSection = 'rocksmith';
    picker._rbDawCat = role === 'pre' ? 'compression' : 'reverb';
    picker._rbRsFilter = '';
    picker._rbVstFilter = '';
    rbRenderMasterAddPicker(role, picker);
}

function rbRenderMasterAddPicker(role, picker) {
    const accent = role === 'pre' ? 'emerald' : 'cyan';
    const accentVst = 'purple';
    const section  = picker._rbSection  || 'rocksmith';
    const dawCat   = picker._rbDawCat   || (role === 'pre' ? 'compression' : 'reverb');
    const rsFilter = picker._rbRsFilter || '';
    const vstFilter = picker._rbVstFilter || '';
    const sectionTabs = `
        <div class="flex items-center gap-1 mb-3 border-b border-gray-800/40 pb-2">
            <button onclick="rbMasterAddPickerSetSection('${role}', 'rocksmith')"
                    class="px-3 py-1 rounded text-xs transition ${section === 'rocksmith'
                        ? `bg-${accent}-700 text-white` : 'bg-dark-700 hover:bg-dark-600 text-gray-300'}">
                🎸 Gear <span class="opacity-60 ml-1">${(_rbGearsCatalog || []).length}</span>
            </button>
            <button onclick="rbMasterAddPickerSetSection('${role}', 'vst')"
                    class="px-3 py-1 rounded text-xs transition ${section === 'vst'
                        ? `bg-${accentVst}-700 text-white` : 'bg-dark-700 hover:bg-dark-600 text-gray-300'}">
                🎛 VST / AU <span class="opacity-60 ml-1">${(rbState.knownVsts || []).length}</span>
            </button>
            <span class="flex-1"></span>
            <button onclick="rbOpenMasterAddPiecePicker('${role}')" class="text-[10px] text-gray-400 hover:text-gray-200 px-1">✕</button>
        </div>`;
    let body;
    if (section === 'rocksmith') {
        body = rbBuildGearPickerBody({
            dawCat, filter: rsFilter,
            onCategoryCall: (k) => `rbMasterAddPickerSetDawCat('${role}', '${rbEsc(k)}')`,
            onFilterCall:   `rbMasterAddPickerSetRsFilter('${role}', this.value)`,
            onAddCall:      (g) => `rbMasterAddPiece('${role}', '${rbEsc(g.rs_gear)}', '${rbEsc(g.category)}')`,
            searchId: `rb-master-${role}-rs-search`,
        });
    } else {
        body = rbBuildVstPickerBody({
            filter: vstFilter,
            onFilterCall: `rbMasterAddPickerSetVstFilter('${role}', this.value)`,
            onPickKnownCall: (v) => `rbMasterAddPieceVst('${role}', '${rbEscPath(v.path)}', '${rbEsc(v.format || 'VST3')}', '${rbEsc(v.name || '')}')`,
            onPickPathCall:  `rbMasterAddPieceVstFromPath('${role}', this.previousElementSibling.value)`,
            searchId: `rb-master-${role}-vst-search`,
        });
    }
    picker.innerHTML = `
        <div class="text-xs text-${accent}-300 font-semibold mb-2">Add ${role} piece</div>
        ${sectionTabs}
        ${body}`;
    const searchEl = document.getElementById(
        section === 'rocksmith' ? `rb-master-${role}-rs-search` : `rb-master-${role}-vst-search`);
    if (searchEl) {
        const v = section === 'rocksmith' ? rsFilter : vstFilter;
        if (v) { searchEl.focus(); searchEl.setSelectionRange(v.length, v.length); }
    }
}

function rbMasterAddPickerSetSection(role, section) {
    const picker = document.getElementById(`rb-master-${role}-picker`);
    if (!picker) return;
    picker._rbSection = section;
    rbRenderMasterAddPicker(role, picker);
}
function rbMasterAddPickerSetDawCat(role, daw) {
    const picker = document.getElementById(`rb-master-${role}-picker`);
    if (!picker) return;
    picker._rbDawCat = daw;
    rbRenderMasterAddPicker(role, picker);
}
function rbMasterAddPickerSetRsFilter(role, value) {
    const picker = document.getElementById(`rb-master-${role}-picker`);
    if (!picker) return;
    picker._rbRsFilter = value;
    rbRenderMasterAddPicker(role, picker);
}
function rbMasterAddPickerSetVstFilter(role, value) {
    const picker = document.getElementById(`rb-master-${role}-picker`);
    if (!picker) return;
    picker._rbVstFilter = value;
    rbRenderMasterAddPicker(role, picker);
}

// Master-chain VST add. Same logic as rbAddPieceVst but pushes onto
// rbState.master[role] instead of a tone.chain.
function rbMasterAddPieceVst(role, vstPath, vstFormat, displayName) {
    if (!vstPath) return;
    const dawCat = rbDawCategoryForVst({ name: displayName, manufacturer: '' });
    const synthName = displayName || vstPath.split('/').pop().replace(/\.(vst3|component)$/i, '');
    const synthGear = 'VST_' + synthName.replace(/[^A-Za-z0-9_-]/g, '_').slice(0, 60);
    rbState.master[role].push({
        type: synthGear,
        slot: `master_${role}`,
        rs_category: dawCat === 'amps' ? 'amp' : (dawCat === 'cabs' ? 'cab' : 'pedal'),
        category: dawCat,
        real_name: synthName,
        make: '', model: '',
        assigned: null,
        _bypassed: false,
        _vst_path: vstPath,
        _vst_format: vstFormat || 'VST3',
        _vst_kind: 'vst',
    });
    const picker = document.getElementById(`rb-master-${role}-picker`);
    if (picker) { picker.classList.add('hidden'); picker.innerHTML = ''; }
    rbAfterMasterEdit(role);
}

function rbMasterAddPieceVstFromPath(role, vstPath) {
    if (!vstPath || !vstPath.trim()) return;
    const path = vstPath.trim();
    const fmt = path.toLowerCase().endsWith('.component') ? 'AudioUnit' : 'VST3';
    const name = path.split('/').pop().replace(/\.(vst3|component)$/i, '');
    return rbMasterAddPieceVst(role, path, fmt, name);
}

function rbMasterAddPiece(role, rsGearType, category) {
    const catalogEntry = (_rbGearsCatalog || []).find(g => g.rs_gear === rsGearType) || {};
    rbState.master[role].push({
        type: rsGearType,
        slot: `master_${role}`,
        rs_category: category,
        category,
        real_name: catalogEntry.name || rsGearType,
        make: catalogEntry.make || '',
        model: catalogEntry.model || '',
        assigned: null,
        _bypassed: false,
    });
    // Close picker.
    const picker = document.getElementById(`rb-master-${role}-picker`);
    if (picker) { picker.classList.add('hidden'); picker.innerHTML = ''; }
    rbAfterMasterEdit(role);
}

// ── Master Assign picker (per-piece NAM library / VST file) ──
async function rbMasterOpenAssignPicker(role, idx) {
    const pickerId = `rb-master-${role}-picker-piece-${idx}`;
    const picker = document.getElementById(pickerId);
    if (!picker) return;
    if (!picker.classList.contains('hidden')) {
        picker.classList.add('hidden');
        picker.innerHTML = '';
        return;
    }
    picker.classList.remove('hidden');
    const p = rbState.master[role][idx];
    const category = p?.rs_category || p?.category || 'pedal';
    const kind = category === 'cab' ? 'ir' : 'nam';
    picker.innerHTML = `
        <div class="flex items-center gap-2 flex-wrap">
            <button onclick="rbMasterAssignFromLibrary('${role}', ${idx}, '${kind}')"
                    class="bg-indigo-900/30 hover:bg-indigo-900/50 text-indigo-300 border border-indigo-800/40 px-2 py-1 rounded text-xs">📚 Library (${kind.toUpperCase()})</button>
            <button onclick="rbMasterAssignVstPick('${role}', ${idx})"
                    class="bg-purple-900/30 hover:bg-purple-900/50 text-purple-300 border border-purple-800/40 px-2 py-1 rounded text-xs">📁 Pick VST file…</button>
            <input id="rb-master-${role}-${idx}-vstpath" type="text"
                   placeholder="Or paste VST path: /Library/Audio/Plug-Ins/VST3/..."
                   onchange="rbMasterAssignVstPath('${role}', ${idx}, this.value)"
                   class="flex-1 bg-dark-800 border border-gray-800 rounded text-[11px] text-gray-300 px-2 py-1 font-mono">
        </div>
        <div id="rb-master-${role}-${idx}-libpanel" class="hidden mt-1"></div>
        <div id="rb-master-${role}-${idx}-status" class="text-[10px] text-gray-500"></div>`;
}

async function rbMasterAssignFromLibrary(role, idx, kind) {
    const panel = document.getElementById(`rb-master-${role}-${idx}-libpanel`);
    if (!panel) return;
    panel.classList.remove('hidden');
    panel.innerHTML = `<div class="text-xs text-gray-500">Loading library…</div>`;
    try {
        const r = await fetch(`${window.RB_API}/local_files?kind=${kind}`);
        const data = await r.json();
        const files = data.files || [];
        const inputId = `rb-master-${role}-${idx}-libsearch`;
        const rowsId  = `rb-master-${role}-${idx}-librows`;
        panel.innerHTML = `
            <div class="flex items-center gap-2 mb-1">
                <input id="${inputId}" type="text" placeholder="🔍 Filter ${kind.toUpperCase()}…"
                       oninput="rbMasterLibraryFilter('${role}', ${idx}, '${kind}', this.value)"
                       class="flex-1 bg-dark-800 border border-gray-800 rounded text-[11px] text-gray-200 px-2 py-1">
                <span id="${rowsId}-count" class="text-[10px] text-gray-500">${files.length}/${files.length}</span>
            </div>
            <div id="${rowsId}" class="max-h-48 overflow-y-auto"></div>`;
        panel._rbAllFiles = files;
        rbMasterLibraryRender(role, idx, kind, files, '');
    } catch (e) {
        panel.innerHTML = `<div class="text-xs text-red-400">Failed: ${rbEsc(e.message || e)}</div>`;
    }
}

function rbMasterLibraryFilter(role, idx, kind, q) {
    const panel = document.getElementById(`rb-master-${role}-${idx}-libpanel`);
    if (!panel || !panel._rbAllFiles) return;
    rbMasterLibraryRender(role, idx, kind, panel._rbAllFiles, q);
}

function rbMasterLibraryRender(role, idx, kind, files, filter) {
    const rowsEl = document.getElementById(`rb-master-${role}-${idx}-librows`);
    const countEl = document.getElementById(`rb-master-${role}-${idx}-librows-count`);
    if (!rowsEl) return;
    const f = (filter || '').toLowerCase().trim();
    const filtered = f ? files.filter(x => x.name.toLowerCase().includes(f)) : files;
    if (countEl) countEl.textContent = `${filtered.length}/${files.length}`;
    const rows = filtered.slice(0, 30).map(file => `
        <div class="flex items-center gap-2 px-2 py-1 hover:bg-indigo-900/20 rounded cursor-pointer"
             onclick="rbMasterApplyLibrary('${role}', ${idx}, '${rbEsc(file.name).replace(/'/g, "\\'")}', '${kind}')">
            <span class="flex-1 text-[11px] text-gray-200 truncate">${rbEsc(file.name)}</span>
            <span class="text-[10px] text-amber-300/80">used ${file.use_count}×</span>
        </div>`).join('');
    rowsEl.innerHTML = rows || '<div class="text-xs text-gray-500 italic">no matches</div>';
}

function rbMasterApplyLibrary(role, idx, fileName, kind) {
    const p = rbState.master[role][idx];
    if (!p) return;
    p._uploaded_file = fileName;
    p._uploaded_kind = kind;
    p._vst_path = null;
    p._vst_kind = null;
    rbAfterMasterEdit(role);
}

async function rbMasterAssignVstPick(role, idx) {
    const host = window.slopsmithDesktop;
    if (!host || typeof host.pickFile !== 'function') {
        return alert('File picker not available — paste the path manually instead.');
    }
    try {
        const picked = await host.pickFile([
            { name: 'VST3 plugin', extensions: ['vst3'] },
            { name: 'Audio Unit',  extensions: ['component'] },
            { name: 'All Files',   extensions: ['*'] },
        ]);
        if (!picked) return;
        const path = Array.isArray(picked) ? picked[0] : picked;
        if (path) rbMasterAssignVstPath(role, idx, path);
    } catch (e) {
        alert(`Pick failed: ${e.message || e}`);
    }
}

function rbMasterAssignVstPath(role, idx, path) {
    if (!path) return;
    const p = rbState.master[role][idx];
    if (!p) return;
    const fmt = path.toLowerCase().endsWith('.component') ? 'AudioUnit' : 'VST3';
    p._vst_path = path;
    p._vst_format = fmt;
    p._vst_kind = 'vst';
    p._uploaded_file = null;
    p._uploaded_kind = null;
    rbAfterMasterEdit(role);
}

// ── Chain editor: reorder / add / remove pieces ────────────────────────
//
// All three operations mutate the in-memory `tone.chain` array, persist
// via /save_preset (rbPersistTone) so the new state survives reload,
// and re-render the chain grid in place. The backend's `get_song` will
// then return the saved chain (chain_source='edited') instead of the
// PSARC's GearList for that tone the next time the user opens the song.

function rbMovePiece(toneIdx, pIdx, direction) {
    const tone = rbState.songTones && rbState.songTones.tones[toneIdx];
    if (!tone) return;
    const newIdx = pIdx + direction;
    if (newIdx < 0 || newIdx >= tone.chain.length) return;
    // Swap in place — simple, no copy.
    const tmp = tone.chain[pIdx];
    tone.chain[pIdx] = tone.chain[newIdx];
    tone.chain[newIdx] = tmp;
    // If the user moved the SELECTED piece (the common case now that
    // ◀ / ▶ live in the chain strip next to it), keep the selection
    // glued to that piece — otherwise the detail panel would suddenly
    // be editing whatever piece ended up at the old index.
    const ed = rbEnsureEditorState();
    if (ed.selectedToneIdx === toneIdx && ed.selectedPIdx === pIdx) {
        ed.selectedPIdx = newIdx;
    } else if (ed.selectedToneIdx === toneIdx && ed.selectedPIdx === newIdx) {
        ed.selectedPIdx = pIdx;
    }
    // Persist + re-render.
    tone.chain_source = 'edited';
    rbAfterChainEdit(toneIdx);
}

async function rbRemovePiece(toneIdx, pIdx) {
    const tone = rbState.songTones && rbState.songTones.tones[toneIdx];
    if (!tone || !tone.chain[pIdx]) return;
    const piece = tone.chain[pIdx];
    const name = piece.real_name || piece.type;
    if (!confirm(`Remove "${name}" from this tone's chain?`)) return;
    tone.chain.splice(pIdx, 1);
    tone.chain_source = 'edited';
    rbAfterChainEdit(toneIdx);
}

// Auto-save the chain after a structural edit (reorder / add / remove).
// Re-renders the grid so position numbers + ▲ ▼ ✗ button states refresh.
// Also reloads the preview if this tone is currently being listened to.
async function rbAfterChainEdit(toneIdx) {
    const filename = rbState.currentSongFile;
    if (!filename) return;
    // Persist first so the new chain is on disk.
    await rbPersistTone(toneIdx, filename).catch(() => null);
    // Update the visual chain grid.
    rbReRenderToneChain(toneIdx, filename);
    // Update the "edited" badge in the tone header (the badge is part of
    // the parent tone block, not the chain grid). Easiest: just update its
    // class/text based on tone.chain_source.
    // Live preview reload if this tone is being auditioned.
    if (rbState.listeningTone === toneIdx) {
        await rbReloadPreview(rbState._previewPayload?.id).catch(() => {});
    }
}

// ── Add piece modal ──
// Opens an inline picker that lets the user choose a slot + an rs_gear_type
// from the catalog. Click "Add" → pushes a piece onto tone.chain with
// kind='none' (unassigned). The user then uses the normal upload / Library /
// VST flow to fill it in.

let _rbGearsCatalog = null;   // cached after first fetch

async function rbOpenAddPiecePicker(toneIdx, filename) {
    const modal = document.getElementById(`rb-addpiece-modal-${toneIdx}`);
    if (!modal) return;
    if (!modal.classList.contains('hidden')) {
        modal.classList.add('hidden');
        modal.innerHTML = '';
        return;
    }
    modal.classList.remove('hidden');
    modal.innerHTML = `<div class="text-xs text-gray-500">Loading gear catalog…</div>`;
    if (_rbGearsCatalog === null) {
        try {
            const r = await fetch(`${window.RB_API}/gears_catalog`);
            const data = await r.json();
            _rbGearsCatalog = (data && data.gears) || [];
        } catch (_) {
            _rbGearsCatalog = [];
        }
    }
    // Initialise the per-modal picker state. Default section and default
    // DAW category are stored on the modal element so a re-render keeps
    // the user's place.
    modal._rbSection  = 'rocksmith';
    modal._rbDawCat   = 'amps';
    modal._rbRsFilter = '';
    modal._rbVstFilter = '';
    rbRenderAddPiecePicker(modal, toneIdx, filename);
}

// DAW-style subcategories the chain picker uses. Same order/labels as the
// backend's _DAW_CATEGORIES_ORDER. Each entry has a key (used to match
// gear.daw_category from /gears_catalog) + a display label.
const RB_DAW_CATEGORIES = [
    { key: 'amps',        label: 'Amps' },
    { key: 'cabs',        label: 'Cabs' },
    { key: 'distortion',  label: 'Distortion' },
    { key: 'modulation',  label: 'Modulation' },
    { key: 'delay',       label: 'Delay' },
    { key: 'reverb',      label: 'Reverb' },
    { key: 'compression', label: 'Compression' },
    { key: 'eq',          label: 'EQ' },
    { key: 'wah',         label: 'Wah' },
    { key: 'pitch',       label: 'Pitch' },
    { key: 'filter',      label: 'Filter' },
    { key: 'utility',     label: 'Utility' },
    { key: 'other',       label: 'Other' },
];

// Heuristic DAW-category guess for an installed VST by its name +
// manufacturer. Used so the VST tab can also be filtered by Compression,
// Modulation, etc. Returns 'other' for plugins we can't classify.
function rbDawCategoryForVst(p) {
    const hay = `${p.name || ''} ${p.manufacturer || ''} ${p.category || ''}`.toLowerCase();
    if (/\b(comp|limit|maxim|punch|optcomp)\b/.test(hay))           return 'compression';
    if (/\b(chorus|flang|phas|trem|vibrato|rotar|ensemble|leslie)\b/.test(hay)) return 'modulation';
    if (/\b(delay|echo|tape|slap)\b/.test(hay))                     return 'delay';
    if (/\b(reverb|verb|spring|plate|hall|room|chamber|shimmer)\b/.test(hay))   return 'reverb';
    if (/\b(dist|fuzz|drive|overdrive|crunch|metal|amp[^a-z]?\s*sim|amplifier|preamp|cabinet|cab[^a-z])/.test(hay)) {
        if (/(cab|cabinet|ir loader|impulse)/.test(hay)) return 'cabs';
        if (/(amp[^a-z]|amplifier|preamp)/.test(hay))   return 'amps';
        return 'distortion';
    }
    if (/\beq\b|equalizer|parametric/.test(hay))                    return 'eq';
    if (/\bwah\b|envelope filter|cry baby|autowah/.test(hay))       return 'wah';
    if (/pitch|octave|harmoni|detune/.test(hay))                    return 'pitch';
    if (/filter|mu(-|tron)|moog/.test(hay))                         return 'filter';
    if (/gate|tuner|noise|hush|silencer|bitcrush|util/.test(hay))   return 'utility';
    return 'other';
}

// Per-tone Add picker — now with two top sections: the game vs VST.
//
// The the game section browses rs_to_real.json grouped by DAW-style
// subcategories so users find pieces the way they'd look in any DAW
// plugin browser. The VST section lists installed plugins from the
// engine's scan + a paste-path input, so the user can drop a "pure VST"
// (e.g. a limiter) straight into the chain without having to first
// map it to some the game pedal.
//
// State stored on the modal element so the picker survives re-renders
// (used by the filter inputs which re-render the whole picker on every
// keystroke).
function rbRenderAddPiecePicker(modal, toneIdx, filename) {
    const safeFile = filename.replace(/'/g, "\\'");
    const section  = modal._rbSection  || 'rocksmith';
    const dawCat   = modal._rbDawCat   || 'amps';
    const rsFilter = modal._rbRsFilter || '';
    const vstFilter = modal._rbVstFilter || '';
    const sectionTabs = `
        <div class="flex items-center gap-1 mb-3 border-b border-gray-800/40 pb-2">
            <button onclick="rbAddPickerSetSection(${toneIdx}, '${rbEsc(safeFile)}', 'rocksmith')"
                    class="px-3 py-1 rounded text-xs transition ${section === 'rocksmith'
                        ? 'bg-emerald-700 text-white' : 'bg-dark-700 hover:bg-dark-600 text-gray-300'}">
                🎸 Gear <span class="opacity-60 ml-1">${(_rbGearsCatalog || []).length}</span>
            </button>
            <button onclick="rbAddPickerSetSection(${toneIdx}, '${rbEsc(safeFile)}', 'vst')"
                    class="px-3 py-1 rounded text-xs transition ${section === 'vst'
                        ? 'bg-purple-700 text-white' : 'bg-dark-700 hover:bg-dark-600 text-gray-300'}">
                🎛 VST / AU <span class="opacity-60 ml-1">${(rbState.knownVsts || []).length}</span>
            </button>
            <span class="flex-1"></span>
            <button onclick="rbOpenAddPiecePicker(${toneIdx}, '${rbEsc(safeFile)}')"
                    title="Close" class="text-[10px] text-gray-400 hover:text-gray-200 px-1">✕</button>
        </div>`;
    let body;
    if (section === 'rocksmith') {
        body = rbBuildGearPickerBody({
            dawCat, filter: rsFilter,
            onCategoryCall: (k) => `rbAddPickerSetDawCat(${toneIdx}, '${rbEsc(safeFile)}', '${rbEsc(k)}')`,
            onFilterCall:   `rbAddPickerSetRsFilter(${toneIdx}, '${rbEsc(safeFile)}', this.value)`,
            onAddCall:      (g) => `rbAddPiece(${toneIdx}, '${rbEsc(safeFile)}', '${rbEsc(g.rs_gear)}', '${rbEsc(g.category)}')`,
            searchId: `rb-addpiece-rs-search-${toneIdx}`,
        });
    } else {
        body = rbBuildVstPickerBody({
            filter: vstFilter,
            onFilterCall: `rbAddPickerSetVstFilter(${toneIdx}, '${rbEsc(safeFile)}', this.value)`,
            onPickKnownCall: (v) => `rbAddPieceVst(${toneIdx}, '${rbEsc(safeFile)}', '${rbEscPath(v.path)}', '${rbEsc(v.format || 'VST3')}', '${rbEsc(v.name || '')}')`,
            onPickPathCall:  `rbAddPieceVstFromPath(${toneIdx}, '${rbEsc(safeFile)}', this.previousElementSibling.value)`,
            searchId: `rb-addpiece-vst-search-${toneIdx}`,
        });
    }
    modal.innerHTML = `
        <div class="text-xs text-gray-400 mb-1">Add piece to <span class="text-gray-200">"${rbEsc(rbState.songTones.tones[toneIdx].name)}"</span></div>
        ${sectionTabs}
        ${body}`;
    // Restore focus on whichever input was active.
    const searchEl = document.getElementById(
        section === 'rocksmith' ? `rb-addpiece-rs-search-${toneIdx}` : `rb-addpiece-vst-search-${toneIdx}`);
    if (searchEl && (rsFilter || vstFilter)) {
        const v = section === 'rocksmith' ? rsFilter : vstFilter;
        searchEl.focus();
        searchEl.setSelectionRange(v.length, v.length);
    }
}

// Escape a path so it can be embedded inline in an onclick="" attribute.
// Single-quotes and backslashes need escaping; we already escape HTML
// via rbEsc, but onclick strings need extra care for JS literal syntax.
function rbEscPath(s) {
    return String(s ?? '').replace(/\\/g, '\\\\').replace(/'/g, "\\'");
}

// Section / filter / category mutators — store on the modal element so
// the picker remembers them across re-renders.
function rbAddPickerSetSection(toneIdx, filename, section) {
    const modal = document.getElementById(`rb-addpiece-modal-${toneIdx}`);
    if (!modal) return;
    modal._rbSection = section;
    rbRenderAddPiecePicker(modal, toneIdx, filename);
}
function rbAddPickerSetDawCat(toneIdx, filename, daw) {
    const modal = document.getElementById(`rb-addpiece-modal-${toneIdx}`);
    if (!modal) return;
    modal._rbDawCat = daw;
    rbRenderAddPiecePicker(modal, toneIdx, filename);
}
function rbAddPickerSetRsFilter(toneIdx, filename, value) {
    const modal = document.getElementById(`rb-addpiece-modal-${toneIdx}`);
    if (!modal) return;
    modal._rbRsFilter = value;
    rbRenderAddPiecePicker(modal, toneIdx, filename);
}
function rbAddPickerSetVstFilter(toneIdx, filename, value) {
    const modal = document.getElementById(`rb-addpiece-modal-${toneIdx}`);
    if (!modal) return;
    modal._rbVstFilter = value;
    rbRenderAddPiecePicker(modal, toneIdx, filename);
}

// ── Shared section bodies (the game + VST) ──
// Both bodies receive the rendering context as inline onclick strings
// so they work in the per-tone picker AND the master-chain picker
// without needing closures over the caller.

function rbBuildGearPickerBody({ dawCat, filter, onCategoryCall, onFilterCall, onAddCall, searchId }) {
    const f = rbNorm(filter || '').trim();
    const matches = (_rbGearsCatalog || []).filter(g => {
        if ((g.daw_category || 'other') !== dawCat) return false;
        if (!f) return true;
        const hay = rbNorm((g.name || '') + ' ' + (g.rs_gear || '') + ' ' + (g.make || '')) + rbGearTypeTags(g);
        return hay.includes(f);
    });
    const catButtons = RB_DAW_CATEGORIES.map(c => `
        <button onclick="${onCategoryCall(c.key)}"
                class="px-2 py-0.5 rounded text-[11px] transition ${c.key === dawCat
                    ? 'bg-emerald-700 text-white'
                    : 'bg-dark-700 hover:bg-dark-600 text-gray-300'}">${rbEsc(c.label)}</button>`).join('');
    const rows = matches.slice(0, 40).map(g => `
        <div class="flex items-center gap-2 px-2 py-1 hover:bg-emerald-900/20 rounded">
            <span class="flex-1 text-[11px] text-gray-200 truncate" title="${rbEsc(g.rs_gear)}">
                ${rbEsc(g.name)} <span class="text-gray-600">(${rbEsc(g.rs_gear)})</span>
            </span>
            <button onclick="${onAddCall(g)}"
                    class="bg-emerald-700 hover:bg-emerald-600 text-white text-[10px] px-2 py-0.5 rounded">＋ Add</button>
        </div>`).join('');
    const moreNote = matches.length > 40
        ? `<div class="text-[10px] text-gray-500 italic mt-1">…and ${matches.length - 40} more (refine search)</div>`
        : '';
    return `
        <div class="flex flex-wrap items-center gap-1 mb-2">${catButtons}</div>
        <div class="flex items-center gap-2 mb-2">
            <input id="${rbEsc(searchId)}" type="text"
                   placeholder="🔍 Filter ${rbEsc(dawCat)} by name / make / code…"
                   value="${rbEsc(filter || '')}"
                   oninput="${onFilterCall}"
                   class="flex-1 bg-dark-800 border border-gray-800 rounded text-[11px] text-gray-200 px-2 py-1">
            <span class="text-[10px] text-gray-500">${matches.length}</span>
        </div>
        <div class="max-h-64 overflow-y-auto">${rows || '<div class="text-xs text-gray-500 italic">no matches in this category</div>'}</div>
        ${moreNote}`;
}

function rbBuildVstPickerBody({ filter, onFilterCall, onPickKnownCall, onPickPathCall, searchId }) {
    const known = rbState.knownVsts || [];
    const f = (filter || '').toLowerCase().trim();
    const matches = known.filter(p => {
        if (p.isInstrument) return false;   // chain pieces are FX, not synths
        if (!f) return true;
        return (p.name || '').toLowerCase().includes(f)
            || (p.manufacturer || '').toLowerCase().includes(f)
            || (p.category || '').toLowerCase().includes(f)
            || (p.path || '').toLowerCase().includes(f)
            || rbDawCategoryForVst(p).includes(f);
    });
    const rows = matches.slice(0, 40).map(v => {
        const tag = rbDawCategoryForVst(v);
        return `
            <div class="flex items-center gap-2 px-2 py-1 hover:bg-purple-900/20 rounded">
                <span class="text-[9px] text-purple-300/80 uppercase tracking-wide px-1 rounded bg-purple-900/30 flex-shrink-0">${rbEsc(tag)}</span>
                <span class="flex-1 text-[11px] text-gray-200 truncate" title="${rbEsc(v.path || '')}">
                    ${rbEsc(v.name || v.path)} <span class="text-gray-600">${rbEsc(v.manufacturer || '')} · ${rbEsc(v.format || 'VST3')}</span>
                </span>
                <button onclick="${onPickKnownCall(v)}"
                        class="bg-purple-700 hover:bg-purple-600 text-white text-[10px] px-2 py-0.5 rounded">＋ Add</button>
            </div>`;
    }).join('');
    const moreNote = matches.length > 40
        ? `<div class="text-[10px] text-gray-500 italic mt-1">…and ${matches.length - 40} more (refine search)</div>`
        : '';
    const emptyState = known.length === 0
        ? `<div class="text-[11px] text-amber-200/80 bg-amber-900/10 border border-amber-800/30 rounded p-2 mb-2">
              No scanned VSTs yet. Either scan from any gear row's ⚙ VST… panel, or paste a path below to bypass scanning entirely.
           </div>`
        : '';
    return `
        ${emptyState}
        <div class="flex items-center gap-2 mb-2">
            <input id="${rbEsc(searchId)}" type="text"
                   placeholder="🔍 Filter by name, manufacturer, category (limiter, comp, chorus…)"
                   value="${rbEsc(filter || '')}"
                   oninput="${onFilterCall}"
                   class="flex-1 bg-dark-800 border border-gray-800 rounded text-[11px] text-gray-200 px-2 py-1">
            <span class="text-[10px] text-gray-500">${matches.length}/${known.length}</span>
        </div>
        <div class="max-h-64 overflow-y-auto">${rows || '<div class="text-xs text-gray-500 italic">no matches</div>'}</div>
        ${moreNote}
        <div class="mt-3 pt-2 border-t border-gray-800/40">
            <div class="text-[10px] text-gray-500 mb-1">Or paste a path (works without a scan):</div>
            <div class="flex items-center gap-2">
                <input type="text"
                       placeholder="/Library/Audio/Plug-Ins/VST3/MyLimiter.vst3"
                       class="flex-1 bg-dark-800 border border-gray-800 rounded text-[11px] text-gray-300 px-2 py-1 font-mono">
                <button onclick="${onPickPathCall}"
                        class="bg-purple-700 hover:bg-purple-600 text-white text-[10px] px-2 py-0.5 rounded">Use this VST</button>
            </div>
        </div>`;
}

function _rbSlotForCategory(category) {
    // Heuristic default slot for a freshly-added piece. The user can re-order
    // afterwards if they want a different signal-flow placement.
    if (category === 'amp')   return 'amp';
    if (category === 'cab')   return 'cabinet';
    if (category === 'rack')  return 'rack';
    if (category === 'pedal') return 'pre_pedal';
    return 'pre_pedal';
}

async function rbAddPiece(toneIdx, filename, rsGearType, category) {
    const tone = rbState.songTones && rbState.songTones.tones[toneIdx];
    if (!tone) return;
    // Pull the display name + make from the cached catalog so the freshly
    // added row doesn't show the raw rs_gear_type as its title. Once the
    // user reloads the song, the backend's _enrich_chain_piece replaces
    // these with the canonical values anyway, but starting from the right
    // string avoids a flicker.
    const catalogEntry = (_rbGearsCatalog || []).find(g => g.rs_gear === rsGearType) || {};
    const newPiece = {
        type: rsGearType,
        slot: _rbSlotForCategory(category),
        rs_category: category,
        category: category,
        real_name: catalogEntry.name || rsGearType,
        make: catalogEntry.make || '',
        model: catalogEntry.model || '',
        knobs: {},
        assigned: null,
        bypassed: false,
        rs_irs: [],
    };
    tone.chain.push(newPiece);
    tone.chain_source = 'edited';
    // Close the modal so the user sees the freshly-added piece.
    const modal = document.getElementById(`rb-addpiece-modal-${toneIdx}`);
    if (modal) { modal.classList.add('hidden'); modal.innerHTML = ''; }
    rbAfterChainEdit(toneIdx);
}

// Add a "pure VST" piece — no the game mapping required. Generates a
// synthetic rs_gear_type from the plugin name so the row still has a
// unique identifier downstream (preset_pieces.rs_gear_type is NOT NULL),
// and pre-fills the VST assignment so the user doesn't need to click
// through the ⚙ VST… panel afterwards. The piece category becomes the
// heuristic DAW classification ('compression', 'modulation', etc.) so
// the UI labels it sensibly.
async function rbAddPieceVst(toneIdx, filename, vstPath, vstFormat, displayName) {
    const tone = rbState.songTones && rbState.songTones.tones[toneIdx];
    if (!tone || !vstPath) return;
    const dawCat = rbDawCategoryForVst({ name: displayName, manufacturer: '' });
    const synthName = displayName || vstPath.split('/').pop().replace(/\.(vst3|component)$/i, '');
    // Synthetic rs_gear_type: stable for a given VST so re-adding the
    // same plugin twice produces the same key (and dedup would land
    // sensibly if we ever want N:1 mapping later).
    const synthGear = 'VST_' + synthName.replace(/[^A-Za-z0-9_-]/g, '_').slice(0, 60);
    const newPiece = {
        type: synthGear,
        slot: dawCat === 'amps' ? 'amp' : (dawCat === 'cabs' ? 'cabinet' : 'post_pedal'),
        rs_category: dawCat === 'amps' ? 'amp' : (dawCat === 'cabs' ? 'cab' : 'pedal'),
        category: dawCat,
        real_name: synthName,
        make: '',
        model: '',
        knobs: {},
        assigned: null,
        bypassed: false,
        rs_irs: [],
        // Pre-fill the VST assignment so the row shows up already loaded
        // — no second click needed in the ⚙ VST… panel.
        _vst_path: vstPath,
        _vst_format: vstFormat || 'VST3',
        _vst_kind: 'vst',
    };
    tone.chain.push(newPiece);
    tone.chain_source = 'edited';
    const modal = document.getElementById(`rb-addpiece-modal-${toneIdx}`);
    if (modal) { modal.classList.add('hidden'); modal.innerHTML = ''; }
    rbAfterChainEdit(toneIdx);
}

// Paste-path variant — accepts a raw filesystem path the user typed in.
// Detects format from the extension (.vst3 → VST3, .component → AudioUnit).
function rbAddPieceVstFromPath(toneIdx, filename, vstPath) {
    if (!vstPath || !vstPath.trim()) return;
    const path = vstPath.trim();
    const fmt = path.toLowerCase().endsWith('.component') ? 'AudioUnit' : 'VST3';
    const name = path.split('/').pop().replace(/\.(vst3|component)$/i, '');
    return rbAddPieceVst(toneIdx, filename, path, fmt, name);
}

// ── VST panel rendering + handlers ────────────────────────────────────

// ── Shared VST picker helpers (search + category groups + hide instruments) ──
// Derive a short, human group label from the VST3/AU category. VST3 reports
// pipe-delimited categories like "Fx|Reverb" or "Fx|Dynamics|Compressor";
// we drop the leading "Fx" and group by the first meaningful segment.
function rbVstCategoryLabel(p) {
    if (p.isInstrument) return 'Instruments';
    const parts = (p.category || '').split('|')
        .map(s => s.trim()).filter(s => s && s.toLowerCase() !== 'fx');
    return parts[0] || 'Other';
}

// Build <optgroup>-grouped <option>s from rbState.knownVsts, applying a text
// filter (name / manufacturer / category) and the hide-instruments flag.
// Shared by both pickers so the Songs and Gear panels stay in sync.
function rbBuildVstOptions(stagedPath, filter, hideInstruments) {
    const known = rbState.knownVsts || [];
    const q = (filter || '').trim().toLowerCase();
    const matches = known.filter(p => {
        if (hideInstruments && p.isInstrument) return false;
        if (!q) return true;
        return ((p.name || '') + ' ' + (p.manufacturer || '') + ' ' + (p.category || ''))
            .toLowerCase().includes(q);
    });
    if (matches.length === 0) return '<option value="" disabled>(no plugins match)</option>';
    const groups = {};
    for (const p of matches) {
        const cat = rbVstCategoryLabel(p);
        (groups[cat] = groups[cat] || []).push(p);
    }
    return Object.keys(groups).sort().map(cat => {
        const opts = groups[cat]
            .slice().sort((a, b) => (a.name || '').localeCompare(b.name || ''))
            .map(p => {
                const sel = (p.path === stagedPath) ? ' selected' : '';
                const tag = p.format ? ` [${rbEsc(p.format)}]` : '';
                return `<option value="${rbEsc(p.path)}"${sel}>${rbEsc(p.name || p.path.split('/').pop())}${tag}</option>`;
            }).join('');
        return `<optgroup label="${rbEsc(cat)}">${opts}</optgroup>`;
    }).join('');
}

// Re-render a picker's <select> options live from its search box + checkbox.
// `selectId` is the <select> id; the search/toggle share it as a prefix.
function rbFilterVstSelect(selectId) {
    const sel = document.getElementById(selectId);
    if (!sel) return;
    const input = document.getElementById(selectId + '-search');
    const cb = document.getElementById(selectId + '-hideinst');
    const staged = sel.value || sel.getAttribute('data-staged') || '';
    sel.innerHTML = rbBuildVstOptions(staged, input ? input.value : '', cb ? cb.checked : true);
}

function rbRenderVstPanelBody(toneIdx, pIdx, currentVstPath, currentFormat) {
    const known = rbState.knownVsts || [];
    // The current selection lives on the piece object (so closing/opening
    // the panel doesn't lose it before the user clicks Assign).
    const piece = rbState.songTones && rbState.songTones.tones[toneIdx] && rbState.songTones.tones[toneIdx].chain[pIdx];
    const stagedPath = (piece && piece._vst_staged_path) || currentVstPath || '';
    const stagedName = stagedPath ? stagedPath.split('/').pop() : '(none selected)';
    // Dropdown only renders if a previous scan populated the list. If not,
    // we fall back to file-picker only (no scan required, never crashes).
    let pluginSelector;
    if (known.length === 0) {
        pluginSelector = `
            <div class="text-xs text-gray-400">
                No plugins scanned yet — scan in <span class="text-gray-300">Settings → VST / Audio Unit plugins</span>, or use 📁 Pick file below.
            </div>`;
    } else {
        const selId = `rb-vst-select-${toneIdx}-${pIdx}`;
        const opts = rbBuildVstOptions(stagedPath, '', true);
        pluginSelector = `
            <div class="flex items-center gap-2 mb-1">
                <input id="${selId}-search" type="text" placeholder="🔍 filter by name / brand / category"
                       oninput="rbFilterVstSelect('${selId}')"
                       class="flex-1 bg-dark-900 border border-gray-800 rounded text-xs text-gray-200 px-2 py-1">
                <label class="text-[10px] text-gray-400 flex items-center gap-1 whitespace-nowrap">
                    <input id="${selId}-hideinst" type="checkbox" checked
                           onchange="rbFilterVstSelect('${selId}')"> hide instruments
                </label>
            </div>
            <select id="${selId}" data-staged="${rbEsc(stagedPath)}"
                    onchange="rbStagePath(${toneIdx}, ${pIdx}, this.value)"
                    class="w-full bg-dark-800 border border-gray-800 rounded text-xs text-gray-200 px-2 py-1">${opts}</select>`;
    }
    return `
        <div class="text-xs text-purple-300 font-semibold">VST3 / Audio Unit</div>
        ${pluginSelector}
        <div class="flex items-center gap-2 flex-wrap">
            <button onclick="rbPickVstFile(${toneIdx}, ${pIdx})"
                    title="Open a file picker — bypass scan entirely. Pick a .vst3 or .component bundle."
                    class="bg-emerald-700 hover:bg-emerald-600 text-white text-xs px-2 py-1 rounded">
                📁 Pick file
            </button>
        </div>
        <div class="flex items-center gap-2">
            <input id="rb-vst-path-${toneIdx}-${pIdx}" type="text"
                   placeholder="Or paste path: /Library/Audio/Plug-Ins/VST3/TAL-Chorus-LX.vst3"
                   value="${rbEsc(stagedPath)}"
                   onchange="rbUpdatePathFromInput(${toneIdx}, ${pIdx}, this.value)"
                   class="flex-1 bg-dark-800 border border-gray-800 rounded text-[11px] text-gray-300 px-2 py-1 font-mono">
        </div>
        <div id="rb-vst-selected-${toneIdx}-${pIdx}" class="text-[10px] text-purple-200/80 break-all">Selected: ${rbEsc(stagedName)}</div>
        <div class="text-[10px] text-gray-500 leading-snug">
            Path also supports <code>.component</code> (Audio Units). Pasting a full
            path auto-assigns; using the file picker requires clicking <strong class="text-purple-200">✓ Use this VST</strong> below.
        </div>
        <div class="flex items-center gap-2 flex-wrap">
            <button onclick="rbLoadAndEditVst(${toneIdx}, ${pIdx})"
                    class="bg-blue-700 hover:bg-blue-600 text-white text-xs px-2 py-1 rounded">
                ▶ Load &amp; Edit
            </button>
            <button onclick="rbApplyRsSettingsToVst(${toneIdx}, ${pIdx})"
                    title="Apply this song's knob values to the VST params (requires a curated mapping in rs_knob_to_vst_param.json)"
                    class="bg-cyan-700/70 hover:bg-cyan-600/70 text-cyan-100 text-xs px-2 py-1 rounded">
                ⇶ Apply song settings
            </button>
            <button onclick="rbCaptureVstState(${toneIdx}, ${pIdx})"
                    title="Capture the current parameter state of the VST in the engine"
                    class="bg-amber-700/60 hover:bg-amber-600/60 text-amber-100 text-xs px-2 py-1 rounded">
                📸 Capture state
            </button>
            <button onclick="rbAssignVst(${toneIdx}, ${pIdx})"
                    class="bg-purple-700 hover:bg-purple-600 text-white text-xs px-2 py-1 rounded">
                ✓ Use this VST
            </button>
        </div>
        <div id="rb-vst-status-${toneIdx}-${pIdx}" class="text-[10px] text-gray-500"></div>`;
}

// Pure compute of the RS-knob→VST-param values for a piece (no engine calls).
// Fetches the curated mapping for (rs_gear, vst) and translates this piece's
// the game knob values + any `_static` pins into a {paramId: value} dict.
// Returns null when there's no curated mapping. Shared by the manual "Apply
// RS settings" button and the auto-apply on editor open.
async function rbComputeRsMappedParams(rsGearType, rsKnobs, vstStem, paramsList) {
    let mapping;
    try {
        const r = await fetch(`${window.RB_API}/vst/knob_mapping?rs_gear_type=${encodeURIComponent(rsGearType)}&vst_name=${encodeURIComponent(vstStem)}`);
        const data = await r.json();
        mapping = data && data.mapping;
    } catch (_) { return null; }
    if (!mapping) return null;
    const nameToId = {};
    (paramsList || []).forEach((p, i) => {
        const id = p.id ?? p.paramId ?? p.index ?? i;
        nameToId[(p.name || '').toLowerCase()] = id;
    });
    const out = {};
    const staticBlock = mapping._static;
    if (staticBlock && typeof staticBlock === 'object') {
        for (const [pname, pval] of Object.entries(staticBlock)) {
            const tid = nameToId[String(pname).toLowerCase()];
            if (tid == null) continue;
            out[tid] = Math.max(0, Math.min(1, parseFloat(pval)));
        }
    }
    for (const [rsKnobName, rule] of Object.entries(mapping)) {
        if (rsKnobName === '_static') continue;
        if (!rsKnobs || !(rsKnobName in rsKnobs)) continue;
        const rsValue = parseFloat(rsKnobs[rsKnobName]);
        if (isNaN(rsValue)) continue;
        let targetId;
        if (typeof rule.param === 'number') targetId = rule.param;
        else if (typeof rule.param === 'string') {
            targetId = nameToId[rule.param.toLowerCase()];
            if (targetId == null) { const asInt = parseInt(rule.param, 10);
                if (!isNaN(asInt) && String(asInt) === rule.param.trim()) targetId = asInt; }
        }
        if (targetId == null) continue;
        const scale = (rule.scale != null) ? parseFloat(rule.scale) : 0.01;
        const offset = (rule.offset != null) ? parseFloat(rule.offset) : 0;
        let v = rsValue * scale + offset;
        if (rule.invert) v = 1 - v;
        out[targetId] = Math.max(0, Math.min(1, v));
    }
    return out;
}

// Apply the RS knob values for THIS piece to the loaded VST's params,
// using the rs_knob_to_vst_param.json translation table. Surfaces a clear
// message when no mapping exists for the (rs_gear, vst) pair so the user
// knows whether to curate the table or replicate manually.
async function rbApplyRsSettingsToVst(toneIdx, pIdx) {
    const api = window.slopsmithDesktop && window.slopsmithDesktop.audio;
    const piece = rbState.songTones.tones[toneIdx].chain[pIdx];
    const statusEl = document.getElementById(`rb-vst-status-${toneIdx}-${pIdx}`);
    const setStatus = (m) => { if (statusEl) statusEl.textContent = m; };
    if (piece._vst_slot_id == null) {
        return setStatus('Load the plugin first with "▶ Load & Edit".');
    }
    const vstPath = rbResolveStagedPath(toneIdx, pIdx);
    if (!vstPath) return setStatus('No VST selected.');
    const vstStem = vstPath.split('/').pop().replace(/\.(vst3|component)$/i, '');
    const rsKnobs = piece.knobs || {};
    if (Object.keys(rsKnobs).length === 0) {
        return setStatus('This RS gear exposes no knobs to map from.');
    }
    setStatus('looking up mapping…');
    let mapping;
    try {
        const r = await fetch(`${window.RB_API}/vst/knob_mapping?rs_gear_type=${encodeURIComponent(piece.type)}&vst_name=${encodeURIComponent(vstStem)}`);
        const data = await r.json();
        mapping = data && data.mapping;
    } catch (e) {
        return setStatus(`mapping lookup failed: ${e.message || e}`);
    }
    if (!mapping) {
        return setStatus(`No curated mapping for ${piece.type} × ${vstStem}. Replicate manually using the knob-values panel above, then curate rs_knob_to_vst_param.json so future songs auto-apply.`);
    }
    // Resolve VST param IDs once (faster + tolerates name vs index in the table).
    let paramsList = piece._vst_param_meta || [];
    if (paramsList.length === 0 && typeof api?.getParameters === 'function') {
        try { paramsList = await api.getParameters(piece._vst_slot_id) || []; } catch (_) {}
    }
    const nameToId = {};
    paramsList.forEach((p, i) => {
        const id = p.id ?? p.paramId ?? p.index ?? i;
        nameToId[(p.name || '').toLowerCase()] = id;
    });

    let applied = 0, skipped = [];
    // Static defaults first — curator-pinned params applied regardless of
    // RS knobs (e.g. kHs Distortion Mode + Dynamics so fuzz pedals sound
    // like fuzz, etc.). Values already normalized [0,1].
    const staticBlock = mapping._static;
    if (staticBlock && typeof staticBlock === 'object') {
        for (const [pname, pval] of Object.entries(staticBlock)) {
            const tid = nameToId[String(pname).toLowerCase()];
            if (tid == null) { skipped.push(`_static.${pname} (param not on VST)`); continue; }
            const v = Math.max(0, Math.min(1, parseFloat(pval)));
            try {
                await api.setParameter(piece._vst_slot_id, tid, v);
                piece._vst_params = piece._vst_params || {};
                piece._vst_params[tid] = v;
                applied++;
            } catch (e) {
                skipped.push(`_static.${pname} (setParameter threw: ${e.message || e})`);
            }
        }
    }
    for (const [rsKnobName, rule] of Object.entries(mapping)) {
        if (rsKnobName === '_static') continue;   // handled above
        if (!(rsKnobName in rsKnobs)) { skipped.push(`${rsKnobName} (not on this gear)`); continue; }
        const rsValue = parseFloat(rsKnobs[rsKnobName]);
        if (isNaN(rsValue)) { skipped.push(`${rsKnobName} (NaN)`); continue; }
        // Resolve the target VST param id. `rule.param` can be an int index
        // (most reliable) or a case-insensitive name lookup.
        let targetId;
        if (typeof rule.param === 'number') {
            targetId = rule.param;
        } else if (typeof rule.param === 'string') {
            // NAME first (graphic-EQ params are named by band frequency, e.g.
            // "50"); fall back to a numeric index only if no name matches.
            targetId = nameToId[rule.param.toLowerCase()];
            if (targetId == null) {
                const asInt = parseInt(rule.param, 10);
                if (!isNaN(asInt) && String(asInt) === rule.param.trim()) targetId = asInt;
            }
        }
        if (targetId == null) { skipped.push(`${rsKnobName} → ${rule.param} (param not found on VST)`); continue; }
        const scale = (rule.scale != null) ? parseFloat(rule.scale) : 0.01;
        const offset = (rule.offset != null) ? parseFloat(rule.offset) : 0;
        let v = rsValue * scale + offset;
        if (rule.invert) v = 1 - v;
        v = Math.max(0, Math.min(1, v));    // clamp into VST normalised range
        try {
            await api.setParameter(piece._vst_slot_id, targetId, v);
            piece._vst_params = piece._vst_params || {};
            piece._vst_params[targetId] = v;
            applied++;
        } catch (e) {
            skipped.push(`${rsKnobName} (setParameter threw: ${e.message || e})`);
        }
    }
    // Refresh the slider display so the new values show up.
    if (typeof api?.getParameters === 'function') {
        try {
            piece._vst_param_meta = await api.getParameters(piece._vst_slot_id);
            rbRenderInlineVstParams(toneIdx, pIdx);
        } catch (_) {}
    }
    const skipMsg = skipped.length > 0 ? ` · skipped: ${skipped.slice(0, 3).join(', ')}${skipped.length > 3 ? '…' : ''}` : '';
    setStatus(`Applied ${applied} RS knobs to VST params${skipMsg}`);
}

// Stage a path on the piece (for Load & Edit / Assign to read), without
// persisting yet. Lets the user pick from dropdown OR file picker OR
// pasted text input and have all flows converge on the same Assign action.
function rbStagePath(toneIdx, pIdx, path) {
    const piece = rbState.songTones.tones[toneIdx].chain[pIdx];
    piece._vst_staged_path = path;
}

// Stage from the manual path input AND update the "Selected" display
// without re-rendering the whole panel (keeps the text cursor in the input).
// Also auto-assigns the VST when the user finished typing a real path
// (.vst3 or .component) — saves the explicit "✓ Use this VST" click
// in the common case where you already know the path you want. Picking
// from the dropdown or file picker still requires the Assign button so
// accidental clicks don't override your current pick.
function rbUpdatePathFromInput(toneIdx, pIdx, path) {
    rbStagePath(toneIdx, pIdx, path);
    const sel = document.getElementById(`rb-vst-selected-${toneIdx}-${pIdx}`);
    if (sel) {
        const name = (path || '').split('/').pop() || '(none selected)';
        sel.textContent = `Selected: ${name}`;
    }
    // Auto-assign when the user pasted/typed a real plugin path. Heuristic:
    // ends with .vst3 or .component AND looks like an absolute filesystem
    // path (starts with /). Anything else is half-typed and we leave it
    // as a stage for now.
    const looksReady = /^\/.+\.(vst3|component)$/i.test((path || '').trim());
    if (looksReady) {
        rbAssignVst(toneIdx, pIdx).catch((e) =>
            console.warn('[rig_builder] auto-assign VST from path input failed:', e));
    }
}

// Use Slopsmith's host file picker to select a .vst3 or .component bundle
// by path. Sidesteps scanPlugins entirely — engine just loads what we
// hand it, no introspection of the install dirs required.
async function rbPickVstFile(toneIdx, pIdx) {
    const host = window.slopsmithDesktop;
    const statusEl = document.getElementById(`rb-vst-status-${toneIdx}-${pIdx}`);
    const setStatus = (m) => { if (statusEl) statusEl.textContent = m; };
    if (!host || typeof host.pickFile !== 'function') {
        return alert('File picker not available on this Slopsmith build.');
    }
    try {
        const picked = await host.pickFile([
            { name: 'VST3 plugin',  extensions: ['vst3'] },
            { name: 'Audio Unit',   extensions: ['component'] },
            { name: 'All Files',    extensions: ['*'] },
        ]);
        if (!picked) return;
        // Normalize: pickFile may return a single string or an array.
        const path = Array.isArray(picked) ? picked[0] : picked;
        if (!path) return;
        const piece = rbState.songTones.tones[toneIdx].chain[pIdx];
        piece._vst_staged_path = path;
        // Re-render so the "Selected" line updates.
        const panel = document.getElementById(`rb-vst-panel-${toneIdx}-${pIdx}`);
        if (panel) {
            const curFmt = piece._vst_format || (piece.assigned && piece.assigned.vst_format) || (path.toLowerCase().endsWith('.component') ? 'AudioUnit' : 'VST3');
            panel.innerHTML = rbRenderVstPanelBody(toneIdx, pIdx, path, curFmt);
        }
        setStatus(`picked ${path.split('/').pop()}`);
    } catch (e) {
        setStatus(`pick failed: ${e.message || e}`);
    }
}

async function rbLoadKnownVsts() {
    // Three sources, in order of preference:
    //   1. Engine's own cache via loadPluginList() — fastest, no scan.
    //      This is what audio_engine populates after its own scans, so a
    //      user who already scanned there gets plugins for free.
    //   2. Our backend filesystem cache /vst/known — persisted on our side.
    //   3. Empty list, user must click Scan.
    const api = window.slopsmithDesktop && window.slopsmithDesktop.audio;
    const mergeByPath = (a, b) => {
        const byPath = new Map();
        for (const p of (a || [])) if (p && p.path) byPath.set(p.path, p);
        for (const p of (b || [])) if (p && p.path) byPath.set(p.path, p);
        return Array.from(byPath.values()).sort((x, y) => String(x.name || '').localeCompare(String(y.name || '')));
    };
    const loadBackend = async () => {
        const r = await fetch(`${window.RB_API}/vst/known`);
        if (!r.ok) return [];
        const data = await r.json();
        return Array.isArray(data.plugins) ? data.plugins : [];
    };
    if (api && typeof api.loadPluginList === 'function' && typeof api.getKnownPlugins === 'function') {
        try {
            // loadPluginList loads the engine's cached list (no scan). Safe
            // to call even with no cache — it just no-ops.
            await api.loadPluginList();
            const plugins = await api.getKnownPlugins();
            if (Array.isArray(plugins) && plugins.length > 0) {
                const backendPlugins = await loadBackend().catch(() => []);
                rbState.knownVsts = mergeByPath(backendPlugins, plugins);
                // Sync to our backend cache so future loads work even if
                // the engine cache gets wiped.
                fetch(`${window.RB_API}/vst/sync_known`, {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({plugins: rbState.knownVsts}),
                }).catch(() => {});
                return;
            }
        } catch (_) { /* fall through to backend cache */ }
    }
    try {
        rbState.knownVsts = await loadBackend();
    } catch (_) { /* best-effort */ }
}

// Shared scan implementation used by both the Songs-tab per-piece panel
// (rbScanForVsts) and the Gear-tab catalog panel (rbCatalogScanVsts).
// Returns the plugin list (may be empty); throws on hard engine failure.
//
// CRASH SAFETY NOTE: Slopsmith's native engine validates each plugin by
// instantiating it briefly, and a single malformed VST3 / AU can crash the
// host process. When that happens:
//   - The engine writes the offending path to /tmp/slopsmith-vst-trace-*.log
//     before instantiation, so on relaunch you can identify the culprit.
//   - The engine's internal `lastPluginScanPath_` lets it skip a known
//     crashing plugin on subsequent scans, BUT only if the user re-clicks
//     Scan after the relaunch. The first scan crash is unavoidable from JS.
//
// To minimise risk, we (a) call savePluginList() after scan so partial
// progress survives a future crash, and (b) suggest scanning Audio Units
// separately from VST3 if available — AU scanning is the more crash-prone
// of the two formats on macOS.
async function rbDoVstScan(statusSetter) {
    const api = window.slopsmithDesktop && window.slopsmithDesktop.audio;
    if (!api || typeof api.scanPlugins !== 'function' || typeof api.getKnownPlugins !== 'function') {
        throw new Error('Native VST hosting not available (running in WASM-only mode?)');
    }
    if (rbState._vstScanInProgress) {
        throw new Error('scan already in progress');
    }
    rbState._vstScanInProgress = true;
    // The native scan instantiates every installed VST3/AU to validate it,
    // which on a machine with many plugins is slow and can HANG outright on
    // a malformed plugin — the engine has no internal timeout. Race it
    // against a wall-clock limit so the UI never gets stuck on "scanning…"
    // forever. We can't truly cancel the native scan, so it may keep running
    // in the background after a timeout; we just stop waiting and report it.
    const SCAN_TIMEOUT_MS = 120000;
    let scanTimer;
    const scanTimeout = new Promise((_, reject) => {
        scanTimer = setTimeout(
            () => reject(new Error(
                `timed out after ${SCAN_TIMEOUT_MS / 1000}s — a slow or incompatible `
                + `plugin is likely hanging the engine. Check the console for the last `
                + `scanned path, remove that plugin, then relaunch Slopsmith and retry.`)),
            SCAN_TIMEOUT_MS);
    });
    try {
        statusSetter && statusSetter('scanning… (up to a minute · don\'t click anything)');
        // scanPlugins returns the list directly per the audio_engine plugin
        // (see bundle/audio_engine/screen.js:744). Older signatures returned
        // void — handle both.
        let plugins;
        const ret = await Promise.race([api.scanPlugins(), scanTimeout]);
        if (Array.isArray(ret)) {
            plugins = ret;
        } else {
            // Older return-void signature → fetch list separately.
            plugins = await Promise.race([api.getKnownPlugins(), scanTimeout]);
        }
        rbState.knownVsts = Array.isArray(plugins) ? plugins : [];
        // Persist to BOTH caches: engine-side (so a future loadPluginList
        // gets it without a re-scan) and our filesystem JSON (so /vst/known
        // can serve the dropdown without going through JS).
        if (typeof api.savePluginList === 'function') {
            await api.savePluginList().catch((e) =>
                console.warn('[rig_builder] savePluginList failed:', e));
        }
        await fetch(`${window.RB_API}/vst/sync_known`, {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({plugins: rbState.knownVsts}),
        }).catch(() => {});
        // The backend merges scan results with previously-seeded entries
        // (seed_known_vsts.py + any prior successful scans), so the merged
        // total can be > what scan returned this run. Re-fetch /vst/known
        // to pick up the merged list instead of showing only what scan
        // got before crashing.
        try {
            const merged = await (await fetch(`${window.RB_API}/vst/known`)).json();
            if (Array.isArray(merged.plugins) &&
                merged.plugins.length >= rbState.knownVsts.length) {
                rbState.knownVsts = merged.plugins;
            }
        } catch (_) { /* fall back to local scan result */ }
        statusSetter && statusSetter(`found ${rbState.knownVsts.length} plugins`);
        return rbState.knownVsts;
    } finally {
        clearTimeout(scanTimer);
        rbState._vstScanInProgress = false;
    }
}

async function rbScanForVsts(toneIdx, pIdx) {
    const statusEl = document.getElementById(`rb-vst-status-${toneIdx}-${pIdx}`);
    const setStatus = (msg) => { if (statusEl) statusEl.textContent = msg; };
    try {
        await rbDoVstScan(setStatus);
        const panel = document.getElementById(`rb-vst-panel-${toneIdx}-${pIdx}`);
        if (panel) {
            const piece = rbState.songTones.tones[toneIdx].chain[pIdx];
            const cur = rbEffVstPath(piece);
            const fmt = rbEffVstFormat(piece);
            panel.innerHTML = rbRenderVstPanelBody(toneIdx, pIdx, cur, fmt);
        }
    } catch (e) {
        setStatus(`scan failed: ${e.message || e}`);
    }
}

// Scan triggered from the Settings tab — the single place to (re)scan
// installed VST3/AU plugins. Populates rbState.knownVsts, which feeds the
// 📚 Library "Plugins" tab and the per-piece VST dropdowns everywhere.
async function rbScanFromSettings() {
    const btn = document.getElementById('rb-settings-scan-btn');
    const setStatus = (m) => {
        const s = document.getElementById('rb-settings-scan-status');
        if (s) s.textContent = m;
    };
    if (btn) btn.disabled = true;
    try {
        await rbDoVstScan(setStatus);
    } catch (e) {
        setStatus(`scan failed: ${e.message || e}`);
    } finally {
        if (btn) btn.disabled = false;
    }
}

// Show the current known-plugin count in the Settings scan status line.
function rbUpdateScanStatus() {
    const s = document.getElementById('rb-settings-scan-status');
    if (!s) return;
    const n = (rbState.knownVsts || []).length;
    s.textContent = n > 0 ? `${n} plugins known` : 'no plugins scanned yet';
}

// Resolve the current "pending" VST path for a piece — prefers an explicit
// stage (dropdown change OR file picker pick), falls back to the persisted
// assignment when the user only opened the panel without picking again.
function rbResolveStagedPath(toneIdx, pIdx) {
    const piece = rbState.songTones.tones[toneIdx].chain[pIdx];
    if (piece._vst_staged_path) return piece._vst_staged_path;
    // Live dropdown value, if the panel has one.
    const select = document.getElementById(`rb-vst-select-${toneIdx}-${pIdx}`);
    if (select && select.value) return select.value;
    return (piece.assigned && piece.assigned.vst_path) || '';
}

// Restore a saved {paramId|paramName → value} dict into a freshly-loaded
// VST slot. Returns the live `getParameters()` snapshot AFTER restoration
// (or `[]` if the engine lacks `getParameters`). Used by all 3 editor-open
// paths (per-tone Load & Edit, per-tone 🎛 Edit VST, master 🎛 Edit VST)
// to avoid duplicating name→id resolution + value clamping logic.
//
// Why we need both name→id and clamp: `apply_vst_state.py` writes param
// NAMES (durable across plugin versions, e.g. {"Threshold": 0.2}), while
// a 📸 Capture writes numeric IDs ({"5": 0.2}). The engine's setParameter
// takes ID + normalized [0,1]. Without name resolution, the bulk-populated
// states silently no-op (parseInt("Threshold") = NaN → editor opens at
// plugin defaults). Without clamping, an out-of-range value gets pinned
// to the param's min or behaves erratically (see `Gain -2328 dB` bug).

async function rbGetVstParamsEventually(api, slotId, attempts = 18, delayMs = 75) {
    if (!api || typeof api.getParameters !== 'function') return [];

    let last = [];
    for (let i = 0; i < attempts; i++) {
        try {
            const raw = await api.getParameters(slotId);
            if (Array.isArray(raw)) {
                last = raw;
                if (raw.length > 0) return raw;
            }
        } catch (e) {
            if (i === 0) console.warn('[rig_builder restore] getParameters threw:', e);
        }
        await new Promise(r => setTimeout(r, delayMs));
    }
    return last;
}

function rbAddCanvasParamNameFallback(vstPath, nameToId, idToName) {
    if (!vstPath || !window.RBPedalCanvas || !window.RBPedalCanvas.specs) return false;

    const stem = vstPath.split('/').pop()
        .replace(/\.(vst3|component)$/i, '')
        .toLowerCase()
        .replace(/[^a-z0-9]/g, '');

    const spec = window.RBPedalCanvas.specs[stem];
    if (!spec || !Array.isArray(spec.names)) return false;

    let added = 0;
    spec.names.forEach((name, idx) => {
        if (!name) return;
        const key = String(name).trim().toLowerCase();
        if (nameToId[key] == null) {
            nameToId[key] = idx;
            idToName[idx] = name;
            added++;
        }
    });

    if (added) {
        console.log(`[rig_builder restore] using canvas spec fallback for ${stem}: ${added} param names`);
    }

    return added > 0;
}

async function rbRestoreSavedParamsToSlot(api, slotId, savedParams, vstPath = '') {
    // Small grace period after loadVST. Some JUCE-hosted VST3 plugins (esp.
    // larger ones like MCompressor with 150 params) finish parameter setup
    // a tick or two after loadVST resolves; calling setParameter inside that
    // window can silently no-op even though it returns without throwing.
    // Empirically 50 ms is enough on M1 with kHs / Melda free.
    await new Promise(r => setTimeout(r, 50));
    let params = await rbGetVstParamsEventually(api, slotId);
    const savedKeys = savedParams ? Object.keys(savedParams) : [];
    console.log(`[rig_builder restore] slot=${slotId} · ${params.length} live params · ${savedKeys.length} saved keys: ${savedKeys.slice(0, 6).join(', ')}${savedKeys.length > 6 ? '…' : ''}`);
    if (!savedParams || typeof api.setParameter !== 'function') {
        console.warn(`[rig_builder restore] slot=${slotId} — no saved params or no setParameter API, skipping`);
        return params;
    }
    const nameToId = {};
    const idToName = {};
    params.forEach((p, idx) => {
        const pid = p.id ?? p.paramId ?? p.index ?? idx;
        const pname = (p.name ?? p.label ?? '').toLowerCase();
        if (pname) {
            nameToId[pname] = pid;
            idToName[pid] = p.name || p.label;
        }
    });
    const unresolvedNameKeys = savedKeys.filter(k => {
        const key = String(k).trim().toLowerCase();
        if (nameToId[key] != null) return false;

        const asNum = parseInt(k, 10);
        return !(Number.isFinite(asNum) && String(asNum) === String(k).trim());
    });

    if (unresolvedNameKeys.length) {
        rbAddCanvasParamNameFallback(vstPath, nameToId, idToName);
    }
    const sampleParam = params[0] || {};
    console.log(`[rig_builder restore] slot=${slotId} · live param shape keys: ${Object.keys(sampleParam).join(', ')} · first 5 names: ${params.slice(0, 5).map(p => p.name || p.label || '<no-name>').join(' | ')}`);
    let applied = 0;
    const failed = [];
    const appliedDetail = [];
    for (const [pid, v] of Object.entries(savedParams)) {
        // Resolve by NAME first — graphic-EQ params are NAMED by band
        // frequency ("50","100",…), which would otherwise be misread as a
        // numeric paramId (50) that doesn't exist → silent no-op. Fall back
        // to numeric paramId only when no param name matches.
        let targetId = nameToId[String(pid).toLowerCase()];
        let resolvedBy = (targetId != null) ? 'name' : null;
        if (targetId == null) {
            const asNum = parseInt(pid, 10);
            if (!isNaN(asNum) && String(asNum) === String(pid).trim()) {
                targetId = asNum;
                resolvedBy = 'numeric';
            } else {
                resolvedBy = 'unresolved';
            }
        }
        if (targetId == null || isNaN(targetId)) {
            failed.push(`${pid}(${resolvedBy})`);
            continue;
        }
        const clamped = Math.max(0, Math.min(1, parseFloat(v)));
        try {
            await api.setParameter(slotId, targetId, clamped);
            applied++;
            appliedDetail.push(`${pid}→[${targetId}]${idToName[targetId] ? '=' + idToName[targetId] : ''}=${clamped.toFixed(3)}`);
        } catch (e) {
            failed.push(`${pid}→${targetId}(setParam threw: ${e.message || e})`);
        }
    }
    if (failed.length) {
        console.warn(`[rig_builder restore] slot=${slotId}: applied ${applied}, FAILED: ${failed.join(', ')}`);
    } else {
        console.log(`[rig_builder restore] slot=${slotId}: applied ${applied}/${savedKeys.length} ✓ ${appliedDetail.slice(0, 4).join(' | ')}${appliedDetail.length > 4 ? '…' : ''}`);
    }
    // Refresh so the caller sees the actual post-restore values, and log a
    // verification line confirming the engine accepted the writes (compares
    // requested vs actual for up to 4 touched params).
    if (typeof api.getParameters === 'function') {
        try {
            const refreshed = await rbGetVstParamsEventually(api, slotId, 4, 50);
            if (Array.isArray(refreshed)) {
                params = refreshed;
                const verify = [];
                for (const detail of appliedDetail.slice(0, 4)) {
                    const m = detail.match(/^.+→\[(\d+)\].*=([\d.]+)$/);
                    if (!m) continue;
                    const tid = parseInt(m[1], 10);
                    const want = parseFloat(m[2]);
                    const actual = refreshed.find(p => (p.id ?? p.paramId ?? p.index) === tid);
                    const actualVal = actual ? (actual.value ?? actual.current) : null;
                    verify.push(`[${tid}] want=${want.toFixed(3)} got=${typeof actualVal === 'number' ? actualVal.toFixed(3) : 'n/a'}`);
                }
                if (verify.length) console.log(`[rig_builder restore] slot=${slotId} verify: ${verify.join(' | ')}`);
            }
        } catch (_) {}
    }
    return params;
}

async function rbLoadAndEditVst(toneIdx, pIdx) {
    const api = window.slopsmithDesktop && window.slopsmithDesktop.audio;
    if (!api) return alert('Native VST hosting not available');
    const path = rbResolveStagedPath(toneIdx, pIdx);
    if (!path) return alert('Pick a plugin first (Pick file or dropdown)');
    const statusEl = document.getElementById(`rb-vst-status-${toneIdx}-${pIdx}`);
    if (statusEl) statusEl.textContent = `loading ${path.split('/').pop()}…`;
    try {
        // Clear any previous experimental load so the editor doesn't accumulate.
        await rbTeardownVstEditor(api);
        await api.startAudio().catch(() => {});
        const slotId = await rbSafeLoadStandaloneVst(api, path);
        if (slotId == null || slotId < 0) throw new Error(rbVstRefusedMsg());
        rbState._vstEditorSlot = slotId;
        // Render the inline params editor (HTML sliders driving setParameter
        // in real time). This is THE workaround for the blurry-native-editor
        // bug — our UI renders crisp at any Retina scale because it's HTML.
        const piece = rbState.songTones.tones[toneIdx].chain[pIdx];
        piece._vst_slot_id = slotId;
        // If we have previously-captured param values, re-apply them so the
        // editor opens with the user's saved tweaks instead of plugin defaults.
        // Helper resolves NAME keys (from apply_vst_state.py) → numeric ids
        // and clamps values to [0,1] (engine's normalized range).
        const savedParams = piece._vst_params || (piece.assigned && piece.assigned.vst_state
            ? rbParseVstStateParams(piece.assigned.vst_state) : null);
        const params = await rbRestoreSavedParamsToSlot(api, slotId, savedParams, path);
        piece._vst_param_meta = params;
        rbRenderInlineVstParams(toneIdx, pIdx);
        if (statusEl) {
            statusEl.textContent = `loaded slot ${slotId} · ${params.length} params · tweak below, then "Capture state"`;
        }
    } catch (e) {
        if (statusEl) statusEl.textContent = `load failed: ${rbFriendlyVstLoadError(e)}`;
    }
}

// Walk the chain that was just loaded into the engine and re-apply any
// persisted VST params (kind=vst stages whose `state` carries our
// {"params": {paramId: value, ...}} JSON shape). Matches stages to chain
// JSON by index — assumes loadPreset preserves order, which is what the
// audio_engine plugin code path also relies on (see bundle screen.js).
async function rbReapplyVstParamsToChain(api, chainSpec) {
    if (typeof api.getChainState !== 'function' || typeof api.setParameter !== 'function') {
        console.warn('[rig_builder reapply] api.getChainState or setParameter missing — walker skipped');
        return;
    }
    let loaded;
    try { loaded = await api.getChainState(); } catch (e) {
        console.warn('[rig_builder reapply] getChainState failed:', e);
        return;
    }
    if (!Array.isArray(loaded)) {
        console.warn('[rig_builder reapply] getChainState returned non-array:', loaded);
        return;
    }
    console.log(`[rig_builder reapply] chain has ${loaded.length} loaded stage(s); spec has ${chainSpec.length}`);
    // Walk the SPEC and the LOADED state together. We rely on
    // index alignment — both lists are in signal-flow order.
    for (let i = 0; i < chainSpec.length && i < loaded.length; i++) {
        const spec = chainSpec[i];
        const slot = loaded[i];
        if (!spec || !slot || spec.type !== 0 || slot.type !== 0) continue;
        // Decode the state b64 to find our params dict.
        let stateObj = null;
        try {
            const decoded = atob(spec.state || '');
            stateObj = JSON.parse(decoded);
        } catch (_) {}
        // The chain JSON wraps our payload as {"pluginPath": ..., "format": ..., "pluginState": <opaque or JSON-string>}.
        // pluginState is what we actually saved into vst_state — try to parse it.
        const inner = stateObj && stateObj.pluginState;
        let params = null;
        if (inner) {
            try {
                const parsed = typeof inner === 'string' ? JSON.parse(inner) : inner;
                if (parsed && parsed.params) params = parsed.params;
            } catch (_) {}
        }
        if (!params || Object.keys(params).length === 0) continue;
        const slotId = slot.id ?? slot.slotId ?? i;
        console.log(`[rig_builder reapply] stage ${i} (slot ${slotId}): ${Object.keys(params).length} params to apply — keys: ${Object.keys(params).slice(0, 5).join(', ')}${Object.keys(params).length > 5 ? '…' : ''}`);

        // Resolve param NAMES (string keys) to IDs via getParameters(),
        // same pattern as the manual ⇶ Apply RS settings flow. Keys that
        // are already numeric strings (or numbers) skip the lookup.
        // This makes bulk-populated vst_states (apply_vst_state.py writes
        // {paramName: value}) restore correctly on real song playback.
        let nameToId = null;
        // ALWAYS build the name→id map: some plugins NAME their params with
        // numeric strings (the bundled graphic EQs name each band by its
        // frequency, e.g. "50","100","6400"). Those keys must resolve by
        // NAME — reading "50" as numeric paramId 50 targets a nonexistent id
        // and silently no-ops, leaving the band at its 0.5 default (the
        // "EQ8/Bass EQ8 didn't map" bug).
        if (typeof api.getParameters === 'function') {
            try {
                const paramList = await api.getParameters(slotId);
                if (Array.isArray(paramList)) {
                    nameToId = {};
                    paramList.forEach((p, idx) => {
                        const pid = p.id ?? p.paramId ?? p.index ?? idx;
                        const pname = (p.name ?? p.label ?? '').toLowerCase();
                        if (pname) nameToId[pname] = pid;
                    });
                    console.log(`[rig_builder reapply] slot ${slotId}: getParameters returned ${paramList.length} params; first 5 names: ${paramList.slice(0, 5).map(p => p.name || p.label).join(' | ')}`);
                } else {
                    console.warn(`[rig_builder reapply] slot ${slotId}: getParameters returned non-array:`, paramList);
                }
            } catch (e) {
                console.warn(`[rig_builder reapply] slot ${slotId}: getParameters threw:`, e);
            }
        }

        let appliedCount = 0;
        const failed = [];
        for (const [pid, v] of Object.entries(params)) {
            // Resolve by NAME first (handles numeric-named params like the
            // graphic-EQ bands); fall back to a numeric paramId only when no
            // param name matches the key.
            let targetId = nameToId ? nameToId[String(pid).toLowerCase()] : undefined;
            if (targetId == null) {
                const asNum = parseInt(pid, 10);
                if (!isNaN(asNum) && String(asNum) === String(pid).trim()) targetId = asNum;
            }
            if (targetId == null || isNaN(targetId)) {
                failed.push(pid);
                continue;
            }
            // Engine takes normalized [0,1]. Old states may still carry raw
            // dB/Hz values from before the apply_vst_state.py normalization
            // fix — clamp defensively so they don't pin params to the wrong
            // extreme (the symptom that produced "Gain -2328 dB" in the
            // editor). New states are already normalized so clamp is a no-op
            // for them.
            const clamped = Math.max(0, Math.min(1, parseFloat(v)));
            try {
                await api.setParameter(slotId, targetId, clamped);
                appliedCount++;
            } catch (e) {
                console.warn(`[rig_builder reapply] setParameter slot=${slotId} param=${pid}(${targetId}):`, e);
                failed.push(`${pid}(setParam threw)`);
            }
        }
        if (failed.length) {
            console.warn(`[rig_builder reapply] slot ${slotId}: applied ${appliedCount} params, failed: ${failed.join(', ')}`);
        } else {
            console.log(`[rig_builder reapply] slot ${slotId}: applied ${appliedCount} params ✓`);
        }
    }
}

// Schedule a VST param re-apply after a loadPreset call. Used by the
// fetch interceptor (song playback via bundle, where we don't control
// the loadPreset call directly) and by other paths that have a chain
// spec on hand. Waits `delayMs` before reapplying so the engine has
// time to finish instantiating each VST — calling setParameter before
// the plug-in is fully loaded crashes some hosts.
//
// Why this matters: the engine's loadPreset restores VSTs from an
// opaque state blob, but in practice the parameter restore is flaky —
// users report that VSTs in a chain stay at plug-in defaults until
// they open the editor (which forces a setParameter walk). Calling
// this helper after every loadPreset shortcuts that — the user no
// longer has to open each VST editor for the saved params to apply.
function rbReapplyVstParamsAfterLoad(chainSpec, delayMs) {
    if (!chainSpec || !Array.isArray(chainSpec)) return;
    const hasVst = chainSpec.some(s => s && s.type === 0);
    if (!hasVst) return;        // no point scheduling work
    setTimeout(() => {
        const api = window.slopsmithDesktop && window.slopsmithDesktop.audio;
        if (!api) return;
        rbReapplyVstParamsToChain(api, chainSpec).catch((e) =>
            console.warn('[rig_builder] re-apply VST params (deferred):', e));
    }, typeof delayMs === 'number' ? delayMs : 200);
}

// Try to parse the vst_state column into a {paramId: value} dict. The column
// may hold either our JSON-shape ({"params":{...}}) or the legacy opaque
// savePreset() blob. Returns null if it isn't our shape.
function rbParseVstStateParams(state) {
    if (!state) return null;
    try {
        const obj = typeof state === 'string' ? JSON.parse(state) : state;
        if (obj && obj.params && typeof obj.params === 'object') return obj.params;
    } catch (_) { /* not our shape — treat as opaque */ }
    return null;
}

// Render HTML sliders for the loaded VST's parameters. Each input fires
// setParameter live so the audio reflects the change in real time. We
// store the current values on the piece so Capture/Assign can read them.
function rbRenderInlineVstParams(toneIdx, pIdx) {
    const piece = rbState.songTones.tones[toneIdx].chain[pIdx];
    const params = rbFilterVstParams(piece._vst_param_meta || []);
    const containerId = `rb-vst-params-${toneIdx}-${pIdx}`;
    let host = document.getElementById(containerId);
    if (!host) {
        // Create the container if it doesn't exist yet (first render after Load).
        const panel = document.getElementById(`rb-vst-panel-${toneIdx}-${pIdx}`);
        if (!panel) return;
        host = document.createElement('div');
        host.id = containerId;
        host.className = 'mt-2 pt-2 border-t border-purple-800/30 max-h-96 overflow-y-auto';
        panel.appendChild(host);
    }
    if (params.length === 0) {
        host.innerHTML = `
            <div class="text-xs text-gray-500 italic">
                This plugin doesn't expose any parameters to the host
                (or getParameters() failed). Use the native editor window for tweaks.
            </div>`;
        return;
    }
    const rows = params.map((p, i) => {
        // Try common field names — different engines / JUCE versions expose
        // slightly different shapes. Be permissive.
        const id     = p.id    ?? p.paramId ?? p.index ?? i;
        const name   = p.name  ?? p.label   ?? `Param ${i}`;
        const value  = p.value ?? p.current ?? 0;
        const text   = p.text  ?? p.display ?? '';
        const label  = p.label_units ?? p.unit ?? '';
        const numSteps = p.numSteps ?? 0;
        // Normalised slider — JUCE's convention is [0, 1].
        const step = numSteps > 1 ? (1 / (numSteps - 1)) : 0.001;
        const valDisplay = text || (typeof value === 'number' ? value.toFixed(3) : value) + (label ? ` ${label}` : '');
        return `
            <div class="flex items-center gap-2 py-1">
                <span class="text-[11px] text-gray-300 w-32 truncate" title="${rbEsc(name)}">${rbEsc(name)}</span>
                <input type="range" min="0" max="1" step="${step}" value="${value}"
                       oninput="rbSetVstParam(${toneIdx}, ${pIdx}, ${id}, this.value, this.nextElementSibling)"
                       class="flex-1 h-1 accent-purple-500">
                <span class="text-[10px] text-purple-200/70 w-20 text-right truncate" title="${rbEsc(String(valDisplay))}">${rbEsc(String(valDisplay))}</span>
            </div>`;
    }).join('');
    host.innerHTML = `
        <div class="text-[11px] text-purple-300 font-semibold mb-1">In-Slopsmith editor · ${params.length} params</div>
        ${rows}`;
}

// Slider onInput handler — sets the param live in the engine + updates the
// "current value" display next to the slider. Also stages the new value in
// the piece's pending params dict so Capture/Assign read it.
async function rbSetVstParam(toneIdx, pIdx, paramId, value, valueDisplayEl) {
    const api = window.slopsmithDesktop && window.slopsmithDesktop.audio;
    const piece = rbState.songTones.tones[toneIdx].chain[pIdx];
    if (piece._vst_slot_id == null) return;
    const v = parseFloat(value);
    try {
        await api.setParameter(piece._vst_slot_id, paramId, v);
    } catch (e) {
        console.warn('[rig_builder] setParameter failed:', e);
    }
    // Re-query just this param's display text if the engine exposes a way.
    // Cheap fallback: show the normalised value.
    if (valueDisplayEl) {
        // Best-effort: ask getParameters and find our entry by id.
        if (typeof api.getParameters === 'function') {
            try {
                const refreshed = await api.getParameters(piece._vst_slot_id);
                if (Array.isArray(refreshed)) {
                    const entry = refreshed.find(p => (p.id ?? p.paramId ?? p.index) === paramId);
                    if (entry && (entry.text || entry.display)) {
                        valueDisplayEl.textContent = entry.text || entry.display;
                    } else {
                        valueDisplayEl.textContent = v.toFixed(3);
                    }
                    piece._vst_param_meta = refreshed;
                } else {
                    valueDisplayEl.textContent = v.toFixed(3);
                }
            } catch (_) {
                valueDisplayEl.textContent = v.toFixed(3);
            }
        } else {
            valueDisplayEl.textContent = v.toFixed(3);
        }
    }
    // Stage the value so Capture state / Use this VST persists it.
    piece._vst_params = piece._vst_params || {};
    piece._vst_params[paramId] = v;
}

async function rbCaptureVstState(toneIdx, pIdx) {
    const api = window.slopsmithDesktop && window.slopsmithDesktop.audio;
    const piece = rbState.songTones.tones[toneIdx].chain[pIdx];
    const statusEl = document.getElementById(`rb-vst-status-${toneIdx}-${pIdx}`);
    if (statusEl) statusEl.textContent = 'capturing…';
    try {
        // Preferred path: snapshot the param values directly from the
        // engine. This is portable (just {paramId: value} JSON), survives
        // chain rebuilds, and reapplies cleanly via setParameter on Listen.
        let params = piece._vst_params || {};
        if (piece._vst_slot_id != null && typeof api?.getParameters === 'function') {
            const live = await api.getParameters(piece._vst_slot_id).catch(() => null);
            if (Array.isArray(live)) {
                params = {};
                for (let i = 0; i < live.length; i++) {
                    const id = live[i].id ?? live[i].paramId ?? live[i].index ?? i;
                    const v  = live[i].value ?? live[i].current;
                    if (typeof v === 'number') params[id] = v;
                }
            }
        }
        piece._vst_params = params;
        // Capture the engine's per-stage opaque state blob (what loadPreset
        // restores in real playback) and stamp it alongside the params.
        const opaque = await rbCaptureVstOpaqueState(api,
            piece._vst_path || (piece.assigned && piece.assigned.vst_path));
        rbStampVstState(piece, opaque);
        if (statusEl) {
            const n = Object.keys(params).length;
            statusEl.textContent = opaque
                ? `captured ${n} params + full state. Click "Use this VST".`
                : `captured ${n} param values. Click "Use this VST".`;
        }
    } catch (e) {
        if (statusEl) statusEl.textContent = `capture failed: ${e.message || e}`;
    }
}

async function rbAssignVst(toneIdx, pIdx) {
    const path = rbResolveStagedPath(toneIdx, pIdx);
    if (!path) return alert('Pick a plugin first (Pick file or dropdown)');
    const piece = rbState.songTones.tones[toneIdx].chain[pIdx];
    // Detect format from path extension if not explicit.
    const fmt = path.toLowerCase().endsWith('.component') ? 'AudioUnit' : 'VST3';
    // In-memory pending state — gets persisted by Save preset / Listen
    // (which call rbPersistTone → /save_preset with vst_* fields).
    piece._vst_path = path;
    piece._vst_format = fmt;
    piece._vst_kind = 'vst';
    // Capture state (if any) was set by rbCaptureVstState; leave it.
    // Trigger the standard "gear changed" flow so the row re-renders and
    // any live preview reloads. (Per-song — global propagation was removed.)
    rbAfterGearChange(toneIdx);
    const statusEl = document.getElementById(`rb-vst-status-${toneIdx}-${pIdx}`);
    if (statusEl) statusEl.textContent = `assigned. Click "Save preset" or "Listen" to persist.`;
}

function rbPickRsIr(select, toneIdx, pIdx) {
    // Cache the selection on the piece so "Use" reads what's currently
    // shown rather than re-querying the DOM later.
    const piece = rbState.songTones.tones[toneIdx].chain[pIdx];
    piece._selected_rs_ir = select.value;
}

function rbAssignRsIr(btn, toneIdx, pIdx) {
    const piece = rbState.songTones.tones[toneIdx].chain[pIdx];
    const wrapper = btn.closest('[data-piece]');
    const select = wrapper.querySelector('select');
    const file = piece._selected_rs_ir || select.value;
    if (!file) return;
    piece._uploaded_file = file;
    piece._uploaded_kind = 'rs_ir';
    const label = wrapper.querySelector('.rb-piece-file');
    label.textContent = `✓ ${file}`;
    label.classList.add('text-green-400');
    rbAfterGearChange(toneIdx);   // reflect + re-audition immediately
}

// Click handler for the cab mic-position buttons. The mic_variants
// payload (per piece) already came with the resolved ir_file for each
// suffix — we just pin that file as the assigned IR, mark the piece
// kind as rs_ir, and let the chain-edit flow persist + reload.
function rbPickCabMic(toneIdx, pIdx, irFile) {
    const piece = rbState.songTones && rbState.songTones.tones
        && rbState.songTones.tones[toneIdx]
        && rbState.songTones.tones[toneIdx].chain[pIdx];
    if (!piece || !irFile) return;
    piece._uploaded_file = irFile;
    piece._uploaded_kind = 'rs_ir';
    rbAfterGearChange(toneIdx);
}

async function rbUploadFile(input, toneIdx, pIdx) {
    const file = input.files[0];
    if (!file) return;
    const piece = rbState.songTones.tones[toneIdx].chain[pIdx];
    const targetUrl = piece.rs_category === 'cab' ? `${window.NAM_API}/irs` : `${window.NAM_API}/models`;

    const fd = new FormData();
    fd.append('file', file);
    const wrapper = input.closest('[data-piece]');
    const label = wrapper.querySelector('.rb-piece-file');
    label.textContent = `uploading ${file.name}…`;
    try {
        const r = await fetch(targetUrl, { method: 'POST', body: fd });
        if (!r.ok) throw new Error(await r.text());
        const data = await r.json();
        // Store on the piece so save_preset picks it up.
        piece._uploaded_file = data.name;
        piece._uploaded_kind = piece.rs_category === 'cab' ? 'ir' : 'nam';
        label.textContent = `✓ ${data.name}`;
        label.classList.add('text-green-400');
        rbAfterGearChange(toneIdx);   // reflect + re-audition immediately
    } catch (e) {
        label.textContent = `error: ${e.message}`;
        label.classList.add('text-red-400');
    }
}

// Persist the tone's current chain selection. Returns the preset_id on
// success, or null on failure (after alerting). Shared by the explicit
// "Save preset" button and the "Listen" preview — the NAM engine can
// only load a *saved* preset id, so previewing has to persist first.
async function rbPersistTone(toneIdx, filename) {
    const tone = rbState.songTones.tones[toneIdx];
    filename = filename || rbState.currentSongFile;
    const pieces = tone.chain.map(p => {
        // VST takes priority over NAM/IR when the user has explicitly
        // picked one (either pending via _vst_path or persisted via assigned).
        const pendingVst = p._vst_kind === 'vst' && p._vst_path;
        const assignedVst = (p.assigned && p.assigned.kind === 'vst' && p.assigned.vst_path);
        const isVst = pendingVst || assignedVst;
        if (isVst) {
            return {
                slot: p.slot,
                rs_gear_type: p.type,
                kind: 'vst',
                file: null,
                vst_path: rbEffVstPath(p),
                vst_format: rbEffVstFormat(p),
                vst_state: rbEffVstState(p),
                params: p.knobs || {},
                assigned_mode: p._vst_kind ? 'manual_vst' : (p.assigned && p.assigned.assigned_mode) || 'manual_vst',
                bypassed: !!p._bypassed,
            };
        }
        const file = rbEffFile(p);
        const kindRaw = rbEffKind(p);
        const kind = kindRaw || (file ? (p.rs_category === 'cab' ? 'ir' : 'nam') : 'none');
        return {
            slot: p.slot,
            rs_gear_type: p.type,
            kind,
            file,
            params: p.knobs || {},
            assigned_mode: 'manual',
            bypassed: !!p._bypassed,   // persist the per-piece bypass
        };
    });
    const payload = {
        filename,
        tone_key: tone.key || tone.name,
        name: `${filename}::${tone.key || tone.name}`,
        pieces,
    };
    try {
        const r = await fetch(`${window.RB_API}/save_preset`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload),
        });
        if (!r.ok) {
            const err = await r.json().catch(() => ({}));
            alert(`Save failed: ${err.error || r.status}`);
            return null;
        }
        const body = await r.json().catch(() => ({}));
        rbRecordLegacyToneDbBridge('save_preset persisted provider-private legacy tone database rows');
        const presetId = body.preset_id ?? null;
        if (presetId !== null) {
            const toneKey = tone.key || tone.name;
            await rbUpsertAudioEffectsMapping({
                filename,
                toneKey,
                presetId,
                label: tone.name || toneKey,
                source: 'manual',
                active: true,
            }).catch(e => console.warn('[rig_builder] audio-effects mapping save failed:', e.message));
            const mirroredRows = Array.isArray(body.mirrored_presets) && body.mirrored_presets.length
                ? body.mirrored_presets
                : (Array.isArray(body.mirrored) ? body.mirrored.map(name => ({ filename: name, preset_id: presetId })) : []);
            for (const mirrored of mirroredRows) {
                const mirroredFilename = mirrored && mirrored.filename;
                const mirroredPresetId = mirrored && (mirrored.preset_id ?? mirrored.presetId) || presetId;
                if (!mirroredFilename) continue;
                rbUpsertAudioEffectsMapping({
                    filename: mirroredFilename,
                    toneKey,
                    presetId: mirroredPresetId,
                    label: tone.name || toneKey,
                    source: 'manual-mirror',
                    active: true,
                }).catch(e => console.warn('[rig_builder] mirrored audio-effects mapping save failed:', e.message));
            }
        }
        return presetId;
    } catch (e) {
        alert(`Save failed: ${e.message}`);
        return null;
    }
}

async function rbSaveTonePreset(toneIdx, filename) {
    const tone = rbState.songTones.tones[toneIdx];
    const presetId = await rbPersistTone(toneIdx, filename);
    if (presetId !== null) {
        alert(`Preset saved for "${tone.name}". The NAM engine will load it when this song plays.`);
    }
}

// Native desktop audio engine, or null (e.g. browser/WASM-only mode).
function rbNativeAudio() {
    const a = window.slopsmithDesktop && window.slopsmithDesktop.audio;
    return (a && typeof a.loadPreset === 'function' && typeof a.startAudio === 'function') ? a : null;
}

function rbNativeAudioDeviceReason(device) {
    if (!device || typeof device !== 'object') return 'Native audio device state is unavailable';
    const sampleRate = Number(device.sampleRate);
    const blockSize = Number(device.blockSize || device.inputBlockSize || 0);
    if (!Number.isFinite(sampleRate) || sampleRate <= 0) return 'Native audio input is not ready: sample rate is unavailable';
    if (!Number.isFinite(blockSize) || blockSize <= 0) return 'Native audio input is not ready: block size is unavailable';
    return '';
}

async function rbEnsureNativeAudioReady(api, options) {
    if (!api) return { ok: false, reason: 'Native audio engine is not available' };

    const opts = options || {};
    const mayStart = opts.startAudio !== false && typeof api.startAudio === 'function';
    let wasRunning = true;
    if (typeof api.isAudioRunning === 'function') {
        try { wasRunning = await api.isAudioRunning(); }
        catch (_) { wasRunning = true; }
    }
    if (!wasRunning && mayStart) {
        try { await api.startAudio(); }
        catch (e) {
            return { ok: false, reason: e && e.message ? e.message : 'Native audio input could not be started' };
        }
    }

    if (typeof api.getCurrentDevice !== 'function') return { ok: true };
    let device = null;
    try { device = await api.getCurrentDevice(); }
    catch (_) { return { ok: true }; }
    const reason = rbNativeAudioDeviceReason(device);
    return reason ? { ok: false, reason } : { ok: true };
}

async function rbLoadVSTWhenReady(api, vstPath) {
    const ready = await rbEnsureNativeAudioReady(api);
    if (!ready.ok) throw new Error(ready.reason || 'Native audio input is not ready');
    return api.loadVST(vstPath);
}

// Stop whatever preview is active (native full-chain or nam_tone fallback).
async function rbStopPreview() {
    rbStopFinalChainNormalizer();
    // Tear down any open VST editor window BEFORE clearChain below — clearing
    // a slot an editor window still points at crashes the host.
    await rbCloseActiveVstEditor();
    const mode = rbState._previewMode;
    const wasListening = rbState.listeningTone;
    const wasAudition = rbState._auditionId;
    rbState._previewMode = null;
    rbState.listeningTone = null;
    rbState._auditionId = null;
    try {
        if (mode === 'nam' && typeof window.namStopPresetTest === 'function') {
            await window.namStopPresetTest();
        } else {
            const api = rbNativeAudio();
            if (api) {
                if (api.setMonitorMute) await api.setMonitorMute(true).catch(() => {});
                if (api.clearChain) await api.clearChain().catch(() => {});
                if (rbState._previewStartedAudio && api.stopAudio) await api.stopAudio().catch(() => {});
            }
        }
    } catch (_) { /* best-effort */ }
    rbState._previewStartedAudio = false;
    rbState._previewPayload = null;
    // Restore whichever button label was showing "⏸ Stop".
    if (wasListening !== null) {
        const b = document.getElementById(`rb-listen-${wasListening}`);
        if (b) b.textContent = '▶ Listen';
    }
    if (wasAudition) {
        const b = document.getElementById(wasAudition);
        if (b) {
            b.disabled = false;
            // Restore the button's original label ("▶ clean", "▶ crunch",
            // "▶ Listen", "▶ Dynamic Cone", …) instead of a bare "▶" —
            // variant audition buttons were losing their level label
            // every time the user stopped/switched.
            b.textContent = b.dataset.origLabel || '▶';
        }
    }
}

// ── Per-stage bypass (audition each piece in/out of the chain) ─────────
function rbUpdateBypassBtn(btn, on) {
    if (!btn) return;
    // The new song editor uses long descriptive labels on the bypass
    // button; the legacy compact label is kept as a fallback for any
    // surviving short-form usage (e.g. master chain).
    const wantsLong = (btn.textContent || '').includes('signal');
    btn.textContent = on
        ? (wantsLong ? '⤳ Bypassed (signal passes through)' : '⤳ Bypassed')
        : (wantsLong ? 'Bypass this stage' : 'Bypass');
    btn.className = 'px-3 py-1.5 rounded border text-xs transition ' + (on
        ? 'bg-amber-700/40 text-amber-300 border-amber-600/40'
        : 'bg-dark-700 hover:bg-dark-600 text-gray-300 border-gray-700');
}

function rbToggleBypass(toneIdx, pIdx, btn) {
    const piece = rbState.songTones.tones[toneIdx].chain[pIdx];
    piece._bypassed = !piece._bypassed;
    rbUpdateBypassBtn(btn, piece._bypassed);
    // Re-render the editor so the chain-strip card's photo (grayscale
    // when bypassed) and its status dot update immediately — without
    // this, the visual state only refreshed when the user clicked a
    // different piece. Cheap enough to do on every toggle.
    rbReRenderSongEditor();
    // Persist the bypass for this song right away so it survives reload /
    // restart (it used to live only in memory until "Save preset").
    if (rbState.currentSongFile) rbPersistTone(toneIdx, rbState.currentSongFile);
    const candidates = rbCandidateDomainsApi();
    if (candidates && typeof candidates.dispatch === 'function') {
        void candidates.dispatch({
            domain: 'audio-effects',
            command: piece._bypassed ? 'bypass' : 'restore',
            requester: RB_PLUGIN_ID,
            payload: { routeKey: RB_EFFECTS_ROUTE_KEY, authorization: 'user-action' },
        });
    }
    // If this tone is previewing, reload now. "bypassed" makes the engine
    // pass the signal THROUGH the stage (not silence it), so the rest of
    // the chain keeps working — exactly the requested behaviour.
    if (rbState.listeningTone === toneIdx) rbReloadPreview();
}

// Stamp each chain stage's `bypassed` from its matching UI piece.
//
// Master pre/post stages (slot starts with 'master_') keep whatever
// bypass they came in with from the backend — they aren't in
// tone.chain, so a `.find` lookup would always miss, force the stage
// to bypassed=false, and silently clobber the master tab's bypass
// state for global FX every time a song was loaded.
function rbApplyBypassToChain(payload, toneIdx) {
    const tone = rbState.songTones && rbState.songTones.tones[toneIdx];
    const chain = (payload && payload.native_preset && payload.native_preset.chain) || [];
    if (!tone) return;
    for (const stage of chain) {
        if (stage.slot && typeof stage.slot === 'string' && stage.slot.startsWith('master_')) {
            continue;   // belongs to the master chain; backend already set bypass
        }
        const piece = tone.chain.find(p => p.type === stage.rs_gear);
        stage.bypassed = !!(piece && piece._bypassed);
    }
}

// Walk the chain just loaded into the engine and force each slot's
// bypass to match what the chain spec said. The engine's loadPreset
// has been unreliable at re-applying bypass on every reload — once a
// slot has been bypassed, subsequent loadPreset calls sometimes leave
// it bypassed even when the new spec says bypassed:false. This
// explicit setBypass walk makes the reload deterministic (was the
// "bypass stuck once activated" Discord report).
async function rbReapplyBypassToChain(api, chainSpec) {
    if (typeof api.getChainState !== 'function' || typeof api.setBypass !== 'function') return;
    let loaded;
    try { loaded = await api.getChainState(); } catch (_) { return; }
    if (!Array.isArray(loaded)) return;
    for (let i = 0; i < chainSpec.length && i < loaded.length; i++) {
        const spec = chainSpec[i];
        const slot = loaded[i];
        if (!spec || !slot) continue;
        const slotId = slot.id ?? slot.slotId ?? i;
        const wantBypass = !!spec.bypassed;
        try { await api.setBypass(slotId, wantBypass); } catch (_) {}
    }
}

let _rbFinalLevelerStageCache = null;

async function rbFetchFinalLevelerStage() {
    if (_rbFinalLevelerStageCache) return _rbFinalLevelerStageCache;
    try {
        const r = await fetch(`${window.RB_API}/final_leveler_stage`);
        const d = await r.json();
        const stage = d && d.enabled && d.stage ? d.stage : null;
        _rbFinalLevelerStageCache = stage;
        return stage;
    } catch (_) {
        return null;
    }
}

async function rbAppendFinalLevelerToStandaloneVstChain(api, baseStage) {
    const chainSpec = [baseStage];
    const leveler = await rbFetchFinalLevelerStage();

    if (!api || !leveler || !leveler.path || typeof api.loadVST !== 'function') {
        return chainSpec;
    }

    try {
        const levelerSlotId = await api.loadVST(leveler.path);
        if (levelerSlotId == null || levelerSlotId < 0) {
            console.warn('[rig_builder final-leveler] engine refused final leveler VST');
            return chainSpec;
        }

        chainSpec.push(leveler);

        // Aplica los params del leveler si el engine no restaura state solo.
        await rbReapplyVstParamsToChain(api, chainSpec).catch((e) =>
            console.warn('[rig_builder final-leveler] param reapply failed:', e)
        );

        return chainSpec;
    } catch (e) {
        console.warn('[rig_builder final-leveler] append failed:', e);
        return chainSpec;
    }
}

// Reload the current native preview chain. Pass a presetId to refetch the
// chain (after a gear change); omit it to just re-apply bypass flags to the
// already-fetched chain (after a bypass toggle). Audio keeps running.
async function rbReloadPreview(refetchPresetId) {
    if (rbState.listeningTone === null || rbState._previewMode !== 'native') return;
    const api = rbNativeAudio();
    if (!api) return;
    if (refetchPresetId != null) {
        try {
            rbState._previewPayload = await (await fetch(`${window.RB_API}/native_preset_full/${refetchPresetId}`)).json();
        } catch (e) { console.warn('[rig_builder] refetch preview failed', e); return; }
    }
    const payload = rbState._previewPayload;
    if (!payload) return;
    rbApplyBypassToChain(payload, rbState.listeningTone);
    const chainArr = payload.native_preset.chain || [];
    const chainLen = chainArr.length || 1;
    try {
        // AWAIT the pre-load mute so chain gain is genuinely at 0 before
        // clearChain+loadPreset run. Previously fire-and-forget, racing
        // the loadPreset and letting the attack transient leak through.
        // rbPreLoadMute returns once mute is applied; the un-mute happens
        // on its own internal timer with a fade-in so we don't pop.
        // Target gain is computed from the chain itself (amp+cab → ×2.0,
        // amp only → ×0.5) so the output is normalised across configs.
        await rbPreLoadMute(chainLen, rbChainGainTargetFor(chainArr)).catch(() => {});
        await rbLoadNativePresetPayload(api, payload, {
            mode: 'preview',
            ref: refetchPresetId || (payload && payload.id) || 'current',
            authorization: 'user-action',
        });
        await rbSyncAudioEffectsCapability('preview-reloaded', { chain: chainArr, mode: 'preview' });
        // Engine sometimes leaves a slot bypassed across reloads — force each
        // slot's bypass to match the spec so toggling un-bypass actually un-bypasses.
        await rbReapplyBypassToChain(api, chainArr);
        // VST params: the opaque state in the chain JSON doesn't reliably
        // restore plug-in params; walk the chain and call setParameter
        // explicitly so VSTs come up at their saved values, not defaults.
        await rbReapplyVstParamsToChain(api, chainArr).catch((e) =>
            console.warn('[rig_builder] reload re-apply VST params:', e));
        await rbApplyChainInputDrive({ chain: chainArr });
        await rbStartFinalChainNormalizer(chainArr);
        // Don't manually un-mute here — rbPreLoadMute does it with a fade
        // on its own timer. Forcing un-mute now would defeat the fade.
    } catch (e) { console.warn('[rig_builder] reload preview failed', e); }
}

// Re-render the open song's editor from current in-memory state (keeps
// _uploaded_file + _bypassed + the selected tone/piece in rbState.editor),
// restoring the active preview button label.
function rbRerenderSong() {
    const el = document.getElementById('rb-song-tones');
    if (!el || !rbState.songTones || !rbState.currentSongFile) return;
    el.innerHTML = rbRenderSongEditor(rbState.songTones, rbState.currentSongFile);
    if (rbState.listeningTone !== null) {
        const b = document.getElementById(`rb-listen-${rbState.listeningTone}`);
        if (b) b.textContent = '⏸ Stop';
    }
}

// Call after any gear change (upload / RS-IR assign / download-and-assign):
// reflect it in the UI immediately, and if the affected tone is previewing,
// re-save + reload the chain so the new gear is audible at once.
async function rbAfterGearChange(toneIdx) {
    rbRerenderSong();
    // Auto-persist so per-song gear changes survive reload: an upload /
    // RS-IR assign used to live only in memory until "Save preset". For the
    // download path (toneIdx == null) the global _assign_file_to_gear already
    // wrote the DB, but we still persist the listening tone to capture any
    // in-memory edits and to reload its preview.
    const idx = (toneIdx != null) ? toneIdx : rbState.listeningTone;
    if (idx != null && rbState.currentSongFile) {
        const pid = await rbPersistTone(idx, rbState.currentSongFile);
        if (pid !== null && rbState.listeningTone === idx) await rbReloadPreview(pid);
    }
}

// ── Single-stage audition (catalog ▶ and search-candidate ▶) ──────────
// Loads ONE NAM/IR stage into the engine so you hear that gear in
// isolation. `btnId` is the toggling button; calling again stops it.
// Perceptual-loudness trim for amp gain-variant auditioning. LUFS
// normalization in the backend matches integrated loudness across NAMs,
// but distortion captures still SOUND louder than equally-LUFS-matched
// clean captures because of sustained harmonic density. Compensate by
// attenuating progressively for higher-gain variants. Tuned by ear
// against typical curated 3-tier amps (Marshall JCM800, Twin, etc.).
// Returns 1.0 (no extra trim) for unknown levels so non-variant amps
// audition exactly as before.
function rbAuditionGainForVariantLevel(level) {
    const TRIM_DB = {
        clean:    0,
        crunch:  -3,
        dist:    -6,
        // Common alternate level names — keep parity if curators use them.
        lead:    -6,
        od:      -3,
        ultra:   -6,
        ultraod1:-6,
    };
    const dB = TRIM_DB[(level || '').toLowerCase()];
    if (dB == null) return 1.0;
    return Math.pow(10, dB / 20);
}

async function rbAuditionFile(file, kind, btnId, gain, rsGear) {
    const btn = btnId ? document.getElementById(btnId) : null;
    if (rbState._auditionId === btnId) { await rbStopPreview(); return; }
    await rbStopPreview();   // stop any other preview/audition first
    const api = rbNativeAudio();
    if (!api) { alert('Audio engine unavailable. Open the “NAM” plugin once to initialize it.'); return; }
    // Stash the button's original label (e.g. "▶ clean", "▶ Listen")
    // so we can restore it after the user stops or switches buttons.
    // The previous implementation hard-coded "▶" on restore, which
    // wiped the level/mic label off variant audition buttons.
    if (btn && !btn.dataset.origLabel) btn.dataset.origLabel = btn.textContent;
    if (btn) { btn.disabled = true; btn.textContent = '⏳'; }
    try {
        const gainQs = (typeof gain === 'number' && isFinite(gain))
            ? `&gain=${encodeURIComponent(gain.toFixed(4))}` : '';
        const gearQs = (typeof rsGear === 'string' && rsGear)
            ? `&rs_gear=${encodeURIComponent(rsGear)}` : '';
        const url = `${window.RB_API}/native_preset_one?file=${encodeURIComponent(file)}&kind=${encodeURIComponent(kind || 'nam')}${gainQs}${gearQs}`;
        const payload = await (await fetch(url)).json();
        const chain = payload.native_preset && payload.native_preset.chain;
        if (!Array.isArray(chain) || !chain.length) throw new Error('file not found');
        await rbLoadNativePresetPayload(api, payload, {
            mode: 'audition',
            ref: kind || 'single',
            authorization: 'user-action',
        });
        await rbSyncAudioEffectsCapability('audition-file', { chain, mode: 'audition', userAction: true });
        if (api.setGain) {
            // Chain-level drive matched to the audition target. Bass
            // amps (rs_gear starts with 'Bass_') use unity to avoid
            // over-saturating the tone3000 clean-gain capture; the
            // catalog always knows g.rs_gear so this is reliable.
            const isBass = typeof rsGear === 'string' && rsGear.startsWith('Bass_');
            await rbApplyChainInputDrive({ isBass, chain });
        }

        await rbStartFinalChainNormalizer(chain);
        if (api.setMonitorMute) await api.setMonitorMute(false).catch(() => {});
        const wasRunning = api.isAudioRunning ? await api.isAudioRunning().catch(() => true) : true;
        await api.startAudio();
        rbState._previewStartedAudio = !wasRunning;
        rbState._previewMode = 'native';
        rbState._auditionId = btnId;
        // "⏸ <label>" lets the user see what they're listening to AND
        // know how to pause. Falls back to a bare ⏸ when no original
        // label was captured (legacy button, no dataset.origLabel).
        if (btn) {
            btn.disabled = false;
            const orig = btn.dataset.origLabel || '';
            const labelTail = orig.replace(/^\s*▶\s*/, '');
            btn.textContent = labelTail ? `⏸ ${labelTail}` : '⏸';
        }
    } catch (e) {
        if (btn) {
            btn.disabled = false;
            btn.textContent = btn.dataset.origLabel || '▶';
        }
        alert(`Could not play: ${e && e.message ? e.message : e}`);
    }
}

// Search-candidate ▶: download the capture (no assign) then audition it.
async function rbAuditionCandidate(btn, rsGear, toneId) {
    if (!btn.id) btn.id = `rb-cand-${toneId}`;
    const btnId = btn.id;
    if (rbState._auditionId === btnId) { await rbStopPreview(); return; }
    await rbStopPreview();
    const old = btn.textContent;
    // Stash the original label so rbStopPreview can restore it later.
    // Set BEFORE changing textContent so rbAuditionFile (which only
    // assigns origLabel if missing) doesn't pick up the ⏳ marker.
    if (!btn.dataset.origLabel) btn.dataset.origLabel = old;
    btn.disabled = true; btn.textContent = '⏳';
    let jobId = null;
    try {
        jobId = await rbStartCapabilityJob('rig-builder.download-capture', 'Download candidate for audition', {
            logicalJobKey: `rig-builder.audition-candidate-${Date.now()}`,
            targetKind: 'tone3000-candidate',
            targetRef: 'candidate-audition',
        });
        const r = await fetch(`${RB_API}/audition_candidate`, {
            method: 'POST', headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ rs_gear: rsGear, tone3000_id: toneId }),
        });
        const data = await r.json();
        if (!r.ok) { rbFinishCapabilityJob(jobId, false, data.error || `HTTP ${r.status}`, 'external-dependency'); alert(data.error || `HTTP ${r.status}`); btn.disabled = false; btn.textContent = old; return; }
        rbFinishCapabilityJob(jobId, true, 'Candidate downloaded for audition');
        btn.disabled = false;
        await rbAuditionFile(data.file, data.kind, btnId);
    } catch (e) {
        rbFinishCapabilityJob(jobId, false, e.message || e, 'external-dependency');
        btn.disabled = false; btn.textContent = old;
        alert(`Could not download/listen: ${e && e.message ? e.message : e}`);
    }
}

// ── Gear catalog grouped by type ─────────────────────────────
let _rbCatalogSeq = 0;
// Gear-tab nav state. Lives on rbState so toggling filters mid-session
// doesn't lose the user's setup, and so tab switches re-apply the same
// filters when they come back. The Sets are rebuilt fresh per session.
if (!rbState.gearCollapsedCats) rbState.gearCollapsedCats = new Set();
if (!rbState.gearExpanded) rbState.gearExpanded = new Set();

const RB_GEAR_LABEL = {
    amp:   'Amplifiers',
    pedal: 'Pedals',
    cab:   'Cabinets',
    rack:  'Racks',
    other: 'Other',
};

const RB_GEAR_BROWSER_CATS = [
    { key: 'amp', label: 'Amps' },
    { key: 'pedal', label: 'Pedals' },
    { key: 'rack', label: 'Racks' },
    { key: 'cab', label: 'Cabs' },
];

if (!rbState.gearBrowserCategory) rbState.gearBrowserCategory = 'amp';
if (!rbState.gearInstrumentFilter) rbState.gearInstrumentFilter = 'all';

async function rbLoadCatalog() {
    const el = document.getElementById('rb-catalog');
    if (!el) return;
    if (rbState._auditionId) await rbStopPreview();   // stop stale audition before re-render
    el.innerHTML = '<p class="text-gray-500">Loading…</p>';
    let data;
    try { data = await (await fetch(`${window.RB_API}/gear_catalog`)).json(); }
    catch (e) { el.innerHTML = `<p class="text-red-400">Error: ${rbEsc(e.message)}</p>`; return; }
    rbState.gearCatalog = (data && data.categories) || {};
    if (!Object.keys(rbState.gearCatalog).length) {
        el.innerHTML = '<p class="text-gray-500">No gear yet. Map a song first.</p>';
        return;
    }
    rbApplyGearFilters();
}

// 150 ms debounce on search input so we don't re-render the whole
// catalog on every keystroke. Re-renders are cheap (~150 cards) but
// the network of nested template literals adds up if you spam it.
let _rbGearSearchTimer = null;
function rbDebouncedGearFilter() {
    if (_rbGearSearchTimer) clearTimeout(_rbGearSearchTimer);
    _rbGearSearchTimer = setTimeout(() => rbApplyGearFilters(), 150);
}

// Lowercase + strip accents so "distorsión" == "distorsion".
function rbNorm(s) {
    return (s || '').toString().toLowerCase().normalize('NFD').replace(/[̀-ͯ]/g, '');
}

// Maps a gear's RS codename / category to extra searchable TYPE keywords
// (Spanish + English) so typing a pedal type — "distorsion", "coro", "eco" —
// surfaces every gear of that type even when the display name (a model number
// like CB-3) doesn't contain the word. Matched against the English rs_gear
// codename, so the synonyms expand to both languages.
const RB_TYPE_SYNONYMS = [
    [/distortion/, 'distortion distorsion'],
    [/overdrive/, 'overdrive drive sobresaturacion saturacion'],
    [/fuzz|buzz|muff/, 'fuzz'],
    [/chorus/, 'chorus coro'],
    [/flanger|flange/, 'flanger flange'],
    [/phaser|phase|vibe/, 'phaser fase faser vibe'],
    [/delay|echo|clone/, 'delay echo eco retardo'],
    [/reverb|verb|chamber|plate|spring|room|hall/, 'reverb reverberacion verb'],
    [/tremolo|trem/, 'tremolo tremol'],
    [/vibrato/, 'vibrato'],
    [/wah/, 'wah wahwah'],
    [/comp/, 'compressor compresor comp'],
    [/\beq\b|equal|graphic/, 'eq equalizer ecualizador'],
    [/octave|octav|pitch|sub/, 'octave octava pitch octaver'],
    [/boost/, 'boost booster realce'],
    [/filter|filt|wah/, 'filter filtro'],
    [/gate/, 'gate noise compuerta ruido'],
    [/ring|mod/, 'ringmod modulador'],
    [/acoustic|simulator/, 'acoustic acustico simulator'],
];
function rbGearTypeTags(g) {
    // Curated, authoritative type tags from the backend (pedal_type_tags.json)
    // take priority; the codename synonym guess stays as a fallback for gears
    // not yet curated.
    let tags = ' ' + rbNorm((g && g.type_tags) || '');
    const key = rbNorm((g && g.rs_gear || '') + ' ' + (g && g.category || ''));
    for (const [re, syn] of RB_TYPE_SYNONYMS) if (re.test(key)) tags += ' ' + syn;
    return tags;
}

function rbGearInstrument(g) {
    const category = (g && g.category || '').toLowerCase();
    if (category === 'rack') return 'all';
    const rs = String(g && g.rs_gear || '');
    return /^Bass_/i.test(rs) || /(^|_)Bass(_|$)/i.test(rs) ? 'bass' : 'guitar';
}

function rbGearHasVst(g) {
    return !!(g && (
        g.vst_path ||
        g.assigned?.vst_path ||
        g.assigned_kind === 'vst' ||
        g.kind === 'vst'
    ));
}

function rbGearVstPath(g) {
    return (g && (
        g.vst_path ||
        g.assigned?.vst_path ||
        ''
    )) || '';
}

function rbGearCanvasStem(g) {
    return rbGearHasVst(g)
        ? g.vst_path.split('/').pop().replace(/\.(vst3|component)$/i, '').toLowerCase().replace(/[^a-z0-9]/g, '')
        : '';
}

function rbGearUsesVstOnlyVisual(g) {
    return ['amp', 'pedal', 'rack'].includes(String(g && g.category || '').toLowerCase());
}

function rbGearIsAssigned(g) {
    return rbGearHasVst(g) || !!(g && (g.assigned || g.file));
}

function rbGearSearchHaystack(g) {
    return rbNorm(
        (g.real_name || '') + ' ' +
        (g.make || '') + ' ' +
        (g.model || '') + ' ' +
        (g.rs_gear || '') + ' ' +
        (g.tone3000_title || '')
    ) + rbGearTypeTags(g);
}

function rbGearMatchesFilters(g, search, onlyUnassigned, instrument) {
    if (onlyUnassigned && rbGearIsAssigned(g)) return false;
    if (instrument && instrument !== 'all') {
        const gi = rbGearInstrument(g);
        if (gi !== 'all' && gi !== instrument) return false;
    }
    return !search || rbGearSearchHaystack(g).includes(search);
}

function rbFilteredGearByCategory(search, onlyUnassigned, instrument) {
    const out = {};
    for (const cat of RB_GEAR_BROWSER_CATS.map(c => c.key)) {
        const rows = rbState.gearCatalog && rbState.gearCatalog[cat] || [];
        out[cat] = rows.filter(g => rbGearMatchesFilters(g, search, onlyUnassigned, instrument));
    }
    return out;
}

function rbFindGear(rsGear) {
    if (!rbState.gearCatalog) return null;
    for (const cat of Object.keys(rbState.gearCatalog)) {
        const found = (rbState.gearCatalog[cat] || []).find(g => g.rs_gear === rsGear);
        if (found) return found;
    }
    return null;
}

function rbSelectGearCategory(cat) {
    rbState.gearBrowserCategory = cat;
    const list = rbFilteredGearForActiveCategory();
    rbState.gearSelected = list[0] ? list[0].rs_gear : null;
    rbApplyGearFilters();
}

function rbSelectGearInstrument(value) {
    rbState.gearInstrumentFilter = value || 'all';
    rbState.gearSelected = null;
    rbApplyGearFilters();
}

function rbSelectGear(rsGear) {
    rbState.gearSelected = rsGear;
    rbApplyGearFilters();
}

function rbFilteredGearForActiveCategory() {
    if (!rbState.gearCatalog) return [];
    const search = rbNorm(((document.getElementById('rb-gear-search') || {}).value || '')).trim();
    const onlyUnassigned = !!((document.getElementById('rb-gear-only-unassigned') || {}).checked);
    const instrument = rbState.gearInstrumentFilter || 'all';
    const cat = rbState.gearBrowserCategory || 'amp';
    return (rbState.gearCatalog[cat] || [])
        .filter(g => rbGearMatchesFilters(g, search, onlyUnassigned, instrument));
}

function rbRenderGearCategoryMenu(filtered) {
    const el = document.getElementById('rb-gear-category-menu');
    if (!el) return;
    const active = rbState.gearBrowserCategory || 'amp';
    el.innerHTML = RB_GEAR_BROWSER_CATS.map(cat => {
        const on = cat.key === active;
        const count = (filtered[cat.key] || []).length;
        return `<button onclick="rbSelectGearCategory('${cat.key}')"
                        class="px-3 py-2 rounded-lg text-left border transition ${on
                            ? 'bg-accent/30 border-accent/40 text-white'
                            : 'bg-dark-800/50 border-gray-800/50 text-gray-400 hover:text-gray-200 hover:bg-dark-700/50'}">
                    <span class="block text-sm font-medium">${cat.label}</span>
                    <span class="block text-[10px] text-gray-500">${count} shown</span>
                </button>`;
    }).join('');
}

function rbRenderGearListItem(g) {
    const selected = rbState.gearSelected === g.rs_gear;
    const assigned = rbGearIsAssigned(g);
    const instrument = rbGearInstrument(g);
    const rsArt = `${window.RB_API}/gear_photo/${encodeURIComponent(g.rs_gear)}${_RB_GEAR_PHOTO_CB}`;
    const stem = rbGearCanvasStem(g);
    const vstArt = (stem && window.RBPedalCanvas && window.RBPedalCanvas.has(stem))
        ? window.RBPedalCanvas.dataURL(stem, {}) : null;
    const thumbW = vstArt ? 84 : 56;
    const sub = [
        instrument === 'all' ? 'all instruments' : instrument,
        assigned ? 'assigned' : 'unassigned',
    ].join(' · ');
    return `<button onclick="rbSelectGear(${rbEsc(JSON.stringify(g.rs_gear))})"
                    class="w-full text-left border rounded-lg p-2 transition flex items-center gap-2 ${selected
                        ? 'bg-dark-600 border-accent/40'
                        : 'bg-dark-800/40 border-gray-800/40 hover:bg-dark-700/50 hover:border-gray-700'}">
                <span class="h-14 flex-shrink-0 rounded bg-dark-900 border border-gray-800/50 overflow-hidden flex items-center justify-center" style="width:${thumbW}px;height:56px">
                    ${vstArt ? `<img src="${vstArt}" alt="" loading="lazy"
                         style="width:${thumbW}px;height:56px;object-fit:contain"
                         onerror="this.style.display='none'; var n=this.nextElementSibling; if(n)n.style.display='';">` : ''}
                    <img src="${rsArt}" alt="" loading="lazy"
                         style="${vstArt ? 'display:none;' : ''}width:56px;height:56px;object-fit:contain"
                         onerror="this.style.display='none'; var n=this.nextElementSibling; if(n)n.classList.remove('hidden');">
                    <span class="hidden text-[9px] uppercase text-gray-700">${rbEsc(g.category || 'gear')}</span>
                </span>
                <span class="min-w-0 flex-1">
                    <span class="block text-sm text-gray-100 leading-tight break-words">${rbEsc(g.real_name || g.rs_gear)}</span>
                    <span class="block text-[10px] text-gray-500 mt-1">${rbEsc(sub)}</span>
                </span>
            </button>`;
}

function rbCatalogVisualForGear(g, size) {
    const isVst = rbGearHasVst(g);
    const gStem = isVst ? g.vst_path.split('/').pop().replace(/\.(vst3|component)$/i, '').toLowerCase().replace(/[^a-z0-9]/g, '') : '';
    const canvasArt = (gStem && window.RBPedalCanvas && window.RBPedalCanvas.has(gStem))
        ? window.RBPedalCanvas.dataURL(gStem, {}) : null;
    const minHeight = size === 'large' ? '360px' : '96px';
    const height = size === 'large' ? '52vh' : '96px';
    if (rbGearUsesVstOnlyVisual(g)) {
        if (isVst) {
            if (canvasArt) {
                return `<div class="bg-dark-900 border border-purple-800/30 rounded-xl overflow-hidden flex items-center justify-center"
                            style="min-height:${minHeight};height:${height}">
                            <img src="${canvasArt}" alt="" style="max-width:100%;max-height:100%;object-fit:contain">
                        </div>`;
            }

            const vstName = g.vst_path.split('/').pop();
            return `<div class="bg-purple-900/10 border border-purple-800/30 rounded-xl flex items-center justify-center text-center px-6"
                        style="min-height:${minHeight};height:${height}">
                        <div>
                            <div class="text-purple-300 text-sm font-semibold mb-1">VST assigned</div>
                            <div class="text-gray-200 text-xs break-all">${rbEsc(vstName)}</div>
                        </div>
                    </div>`;
        }
        return `<div class="bg-dark-900/60 border border-gray-800/50 rounded-xl flex items-center justify-center text-center px-6"
                     style="min-height:${minHeight};height:${height}">
                <div>
                    <div class="text-gray-500 text-sm">No VST assigned</div>
                </div>
            </div>`;
    }
    const rsArt = `${window.RB_API}/gear_photo/${encodeURIComponent(g.rs_gear)}${_RB_GEAR_PHOTO_CB}`;
    const fallback = g.image
        ? `<img src="${rbEsc(g.image)}" alt="" loading="lazy"
                 style="display:none;max-width:100%;max-height:100%;object-fit:contain"
                 class="max-w-full max-h-full rounded object-contain bg-dark-900"
                 onerror="this.style.display='none'; var n=this.nextElementSibling; if(n)n.classList.remove('hidden');">`
        : '';
    const onerr = "this.style.display='none'; var n=this.nextElementSibling; if(n){ if(n.tagName==='IMG'){n.style.display=''} else {n.classList.remove('hidden')} }";
    const empty = `<div class="hidden w-full h-full flex items-center justify-center text-gray-700 text-xs uppercase tracking-wide">${rbEsc(g.category || 'gear')}</div>`;
    return `<div class="bg-dark-900 border border-gray-800/50 rounded-xl overflow-hidden flex items-center justify-center"
                 style="min-height:${minHeight};height:${height}">
                ${canvasArt
                    ? `<img src="${canvasArt}" alt="" style="max-width:100%;max-height:100%;object-fit:contain" class="max-w-full max-h-full object-contain">`
                    : `<img src="${rsArt}" alt="" loading="lazy" style="max-width:100%;max-height:100%;object-fit:contain" class="max-w-full max-h-full object-contain" onerror="${onerr}">${fallback}${empty}`}
            </div>`;
}

function rbOpenSelectedGearVst(g) {
    if (!g || !rbGearHasVst(g)) return;

    const safeId = g.rs_gear.replace(/[^a-zA-Z0-9_-]/g, '_');
    const panel = document.getElementById(`rb-cat-edit-${safeId}`);
    if (!panel) return;

    const path = rbGearVstPath ? rbGearVstPath(g) : g.vst_path;
    if (!path) return;

    const stem = path.split('/').pop()
        .replace(/\.(vst3|component)$/i, '')
        .toLowerCase()
        .replace(/[^a-z0-9]/g, '');

    rbCatalogEditInline(
        safeId,
        path,
        g.vst_format || 'VST3',
        g.rs_gear,
        stem
    );
}

function rbRenderGearDetail(g) {
    const el = document.getElementById('rb-gear-detail');
    if (!el) return;
    if (!g) {
        el.innerHTML = `<div class="bg-dark-700/40 border border-gray-800/50 rounded-xl p-6 text-gray-500 text-sm">
            No gear matches the current filters.
        </div>`;
        return;
    }
    const safeId = g.rs_gear.replace(/[^a-zA-Z0-9_-]/g, '_');
    const isVst = rbGearHasVst(g);
    const assignedLine = isVst
        ? `<span class="text-purple-300 break-all">VST: ${rbEsc(g.vst_path.split('/').pop())}</span>`
        : rbGearIsAssigned(g)
            ? `<span class="text-emerald-300 break-all">${rbEsc(g.tone3000_title || rbLibShortName(g.file) || 'assigned')}</span>`
            : `<span class="text-gray-500">unassigned</span>`;
    const instrument = rbGearInstrument(g);
    const t3kHeaderLink = g.tone3000_url
        ? `<a href="${rbEsc(g.tone3000_url)}" target="_blank" title="View on tone3000"
              class="text-gray-500 hover:text-accent text-sm">tone3000 ↗</a>` : '';
    const hasInlineAudition = (
        (Array.isArray(g.variants) && g.variants.length > 0)
        || (Array.isArray(g.mic_variants) && g.mic_variants.length > 0)
    );
    const variantsBtn = g.category === 'amp' ? `
        <button onclick="rbToggleAmpVariants('${rbEsc(g.rs_gear)}')"
                class="bg-emerald-900/30 hover:bg-emerald-900/50 text-emerald-300 border border-emerald-800/40 px-3 py-1.5 rounded text-xs">🎚 Variants</button>` : '';
    const libraryBtn = `<button onclick="rbToggleCatalogLibrary('${rbEsc(g.rs_gear)}','${rbEsc(g.category || '')}','${rbEsc(g.vst_path || '')}','${rbEsc(g.vst_format || 'VST3')}')"
                                class="bg-indigo-900/30 hover:bg-indigo-900/50 text-indigo-300 border border-indigo-800/40 px-3 py-1.5 rounded text-xs">📚 Library</button>`;
    const searchBtn = `<button onclick="rbOpenSuggest('${rbEsc(g.rs_gear)}')"
                                class="text-gray-400 hover:text-gray-200 text-xs px-2 py-1.5">🔍 Search tone3000</button>`;
    const editVstBtn = ''
    const variantAuditions = Array.isArray(g.variants) && g.variants.length
        ? `<div class="flex items-center gap-1 flex-wrap">${g.variants.map(v => {
            const vId = `rb-aud-${_rbCatalogSeq++}`;
            if (!v.available || !v.file) return `<button disabled class="text-[10px] px-2 py-0.5 rounded bg-dark-800/50 text-gray-600 cursor-not-allowed">▶ ${rbEsc(v.level)}</button>`;
            const trim = rbAuditionGainForVariantLevel(v.level);
            return `<button id="${vId}" onclick="rbAuditionFile('${rbEsc(v.file).replace(/'/g,"\\'")}','nam','${vId}',${trim},'${rbEsc(g.rs_gear || '')}')"
                            class="text-[10px] px-2 py-0.5 rounded bg-emerald-900/30 hover:bg-emerald-900/60 text-emerald-300 border border-emerald-800/40">▶ ${rbEsc(v.level)}</button>`;
        }).join('')}</div>` : '';
    const micAuditions = Array.isArray(g.mic_variants) && g.mic_variants.length
        ? `<div class="flex items-center gap-1 flex-wrap">${g.mic_variants.map(v => {
            const vId = `rb-aud-${_rbCatalogSeq++}`;
            if (!v.available || !v.ir_file) return `<button disabled class="text-[10px] px-2 py-0.5 rounded bg-dark-800/50 text-gray-600 cursor-not-allowed">▶ ${rbEsc(v.label || v.suffix)}</button>`;
            return `<button id="${vId}" onclick="rbAuditionFile('${rbEsc(v.ir_file).replace(/'/g,"\\'")}','ir','${vId}')"
                            class="text-[10px] px-2 py-0.5 rounded bg-sky-900/30 hover:bg-sky-900/60 text-sky-300 border border-sky-800/40">▶ ${rbEsc(v.label || v.suffix)}</button>`;
        }).join('')}</div>` : '';
    const visualBlock = rbCatalogVisualForGear(g, 'large');

    el.innerHTML = `<div class="bg-dark-700/40 border border-gray-800/50 rounded-xl p-4 space-y-4">
        <div class="flex items-start justify-between gap-3">
            <div class="min-w-0">
                <h3 class="text-white text-xl font-semibold leading-tight break-words">${rbEsc(g.real_name || g.rs_gear)}</h3>
                <div class="text-xs text-gray-500 mt-1">
                    ${rbEsc(g.rs_gear)} · ${rbEsc(RB_GEAR_LABEL[g.category] || g.category || 'gear')} · ${rbEsc(instrument === 'all' ? 'all instruments' : instrument)}
                </div>
            </div>
            ${t3kHeaderLink}
        </div>
        <div id="rb-cat-edit-${safeId}" class="hidden bg-purple-900/10 border border-purple-800/30 rounded p-2"></div>
        ${isVst ? '' : visualBlock}
        <div class="bg-dark-800/50 border border-gray-800/40 rounded-lg p-3 space-y-2">
            <div class="text-xs text-gray-400">Current assignment: ${assignedLine}</div>
            ${variantAuditions}
            ${micAuditions}
            <div class="flex flex-wrap items-center gap-1.5">
                ${editVstBtn}
                ${variantsBtn}
                ${libraryBtn}
                <div class="flex-1"></div>
                ${searchBtn}
            </div>
            <div id="rb-cat-lib-${safeId}" class="hidden bg-indigo-900/10 border border-indigo-800/30 rounded p-2"></div>
            <div id="rb-cat-variants-${safeId}" class="hidden bg-emerald-900/10 border border-emerald-800/30 rounded p-2"></div>
        </div>
    </div>`;

    if (isVst) {
    setTimeout(() => {
        rbOpenSelectedGearVst(g);
    }, 0);
}
}

function rbApplyGearFilters() {
    const el = document.getElementById('rb-catalog');
    if (!el || !rbState.gearCatalog) return;
    const search = rbNorm(((document.getElementById('rb-gear-search') || {}).value || '')).trim();
    const onlyUnassigned = !!((document.getElementById('rb-gear-only-unassigned') || {}).checked);
    const instrument = rbState.gearInstrumentFilter || 'all';
    const instEl = document.getElementById('rb-gear-instrument-filter');
    if (instEl && instEl.value !== instrument) instEl.value = instrument;

    const filtered = rbFilteredGearByCategory(search, onlyUnassigned, instrument);
    rbRenderGearCategoryMenu(filtered);

    const activeCat = rbState.gearBrowserCategory || 'amp';
    const activeList = filtered[activeCat] || [];
    const selectedStillVisible = rbState.gearSelected &&
        activeList.some(g => g.rs_gear === rbState.gearSelected);

    if (!selectedStillVisible)
        rbState.gearSelected = null;

    const selected = rbState.gearSelected ? rbFindGear(rbState.gearSelected) : null;

    const summary = document.getElementById('rb-gear-list-summary');
    if (summary) {
        const label = (RB_GEAR_BROWSER_CATS.find(c => c.key === activeCat) || {}).label || activeCat;
        summary.textContent = `${activeList.length} ${label.toLowerCase()}${search ? ' matching search' : ''}`;
    }

    if (!activeList.length) {
        el.innerHTML = `<div class="text-center text-gray-500 py-10">
            No matches.${search ? ` Try clearing the search.` : ''}</div>`;
        rbRenderGearDetail(null);
        return;
    }
    try { el.innerHTML = activeList.map(rbRenderGearListItem).join(''); }
    catch (e) {
        console.error('[rig_builder] catalog render failed', e);
        el.innerHTML = `<p class="text-red-400">Error rendering: ${rbEsc(e.message)}</p>`;
    }
    rbRenderGearDetail(selected);
    // Pedal-canvas thumbnails are rendered with dataURL() at build time; if the
    // embedded fonts weren't loaded yet they'd use a fallback face. Repaint the
    // catalog ONCE when fonts finish loading so the thumbnails come out right.
    if (!rbState._gearFontsRepaint && window.RBPedalCanvas && window.RBPedalCanvas.ready) {
        rbState._gearFontsRepaint = true;
        window.RBPedalCanvas.ready().then(() => { try { rbApplyGearFilters(); } catch (_) {} });
    }
}

function rbScrollToCategory(cat) {
    const target = document.getElementById(`rb-cat-${cat}`);
    if (!target) return;
    // Expand if collapsed so the user actually sees the cards.
    if (rbState.gearCollapsedCats.has(cat)) {
        rbState.gearCollapsedCats.delete(cat);
        rbApplyGearFilters();
        // Wait for the re-render to land before scrolling.
        setTimeout(() => {
            document.getElementById(`rb-cat-${cat}`)?.scrollIntoView({behavior: 'smooth', block: 'start'});
        }, 30);
    } else {
        target.scrollIntoView({behavior: 'smooth', block: 'start'});
    }
}

function rbToggleCategoryCollapse(cat) {
    if (rbState.gearCollapsedCats.has(cat)) rbState.gearCollapsedCats.delete(cat);
    else rbState.gearCollapsedCats.add(cat);
    rbApplyGearFilters();
}

function rbClearGearFilters() {
    const s = document.getElementById('rb-gear-search');
    const u = document.getElementById('rb-gear-only-unassigned');
    const i = document.getElementById('rb-gear-instrument-filter');
    if (s) s.value = '';
    if (u) u.checked = false;
    if (i) i.value = 'all';
    rbState.gearInstrumentFilter = 'all';
    rbState.gearBrowserCategory = 'amp';
    rbState.gearSelected = null;
    rbState.gearCollapsedCats.clear();
    rbApplyGearFilters();
}

// One-line card used in compact mode. Drops the photo to a thumbnail
// and skips the controls panel — click ▶ still works, and the rs_gear
// name is shown small for quick scanning at scale (100+ gears).
function rbRenderCatalogCardCompact(g) {
    const btnId = `rb-aud-${_rbCatalogSeq++}`;
    const photo = g.image
        ? `<img src="${rbEsc(g.image)}" alt="" loading="lazy"
               style="width:32px;height:32px;object-fit:cover"
               class="w-8 h-8 rounded object-cover bg-dark-900 flex-shrink-0"
               onerror="this.replaceWith(Object.assign(document.createElement('div'),{className:'w-8 h-8 rounded bg-dark-900 flex-shrink-0'}))">`
        : `<div class="w-8 h-8 rounded bg-dark-900 flex-shrink-0"></div>`;
    const status = g.assigned
        ? `<span class="text-emerald-400 text-xs" title="Assigned">●</span>`
        : `<span class="text-amber-400 text-xs" title="Pending">●</span>`;
    // Compact rows still let you audition. Suggest / library picker
    // are one step away: clicking anywhere else on the row toggles
    // back to a full card for that single gear (planned).
    const file = g.file
        ? `<span class="text-xs text-gray-500 truncate font-mono" title="${rbEsc(g.file)}">${rbEsc(g.file.split('/').pop())}</span>`
        : (g.vst_path
            ? `<span class="text-xs text-purple-400 truncate" title="${rbEsc(g.vst_path)}">VST: ${rbEsc(g.vst_path.split('/').pop())}</span>`
            : `<span class="text-xs text-gray-600 italic">unassigned</span>`);
    return `<div class="flex items-center gap-2 px-3 py-2 hover:bg-dark-700/30">
        ${photo}
        ${status}
        <div class="min-w-0 flex-1">
            <div class="text-gray-200 truncate text-xs"><strong>${rbEsc(g.real_name)}</strong></div>
            ${file}
        </div>
        ${g.file ? `<button id="${btnId}" onclick="rbAuditionFile(${JSON.stringify(g.file)},${JSON.stringify(g.kind || 'nam')},'${btnId}',undefined,${JSON.stringify(g.rs_gear || '')})"
                            class="text-gray-400 hover:text-emerald-300 px-1.5 py-0.5 text-xs">▶</button>` : ''}
    </div>`;
}

function rbRenderCatalogCard(g) {
    // v2 catalog card — minimal collapsed state, click row to expand
    // ─────────────────────────────────────────────────────────────
    // Header (always visible): photo · full name · rs_gear · status pill
    //   ▸ The full name no longer truncates — it wraps over 2 lines so
    //     "Marshall JCM800 2203" et al. stay readable.
    //   ▸ the game gear photo (/gear_photo/{rs_gear}) first; if the
    //     RS extraction hasn't been run, fall back to the tone3000
    //     capture image when the curator has assigned one.
    //
    // Action panel (revealed on click): ▶ Listen · 🎚 Variants ·
    //   📚 Library · 🔍 Search · ↗ tone3000 · the variant audition row
    //   and the existing Library / Variants panels.

    const safeId = g.rs_gear.replace(/[^a-zA-Z0-9_-]/g, '_');
    const expanded = rbState.gearExpanded && rbState.gearExpanded.has(g.rs_gear);
    const isVst = g.kind === 'vst' && g.vst_path;
    // Status is communicated entirely through the photo now: when
    // nothing is assigned (no NAM, no IR, no VST) the photo goes
    // grayscale + dimmed, matching the "off" feel of a bypassed piece
    // in the song editor. No colored dot needed — the visual state is
    // the indicator.
    const isAssigned = isVst || g.assigned;
    const photoOff = isAssigned ? '' : 'grayscale opacity-40';

    // Assignment label — only rendered inside the expanded action panel
    // now (the collapsed row used to show it under the rs_gear codename,
    // but that line repeated 100+ times made the catalog feel noisy).
    let assignedLine;
    if (isVst) {
        const vstName = g.vst_path.split('/').pop();
        assignedLine = `<div class="text-xs text-purple-300/90 break-all" title="${rbEsc(g.vst_path)}">✓ VST: ${rbEsc(vstName)}</div>`;
    } else if (g.assigned) {
        const label = g.tone3000_title || rbLibShortName(g.file) || 'assigned';
        assignedLine = `<div class="text-xs text-green-400/90 break-all" title="${rbEsc(g.file || '')}">✓ ${rbEsc(label)}</div>`;
    } else {
        assignedLine = `<div class="text-xs text-gray-500">(unassigned)</div>`;
    }

    // the game art with tone3000 image as a fallback. The sibling-swap
    // trick avoids the HTML-in-attribute escaping issue we hit in the
    // song editor — onerror just hides this img and reveals the next
    // sibling, which is the next photo source down the chain.
    const rsArt = `${window.RB_API}/gear_photo/${encodeURIComponent(g.rs_gear)}${_RB_GEAR_PHOTO_CB}`;
    const onerrChain = "this.style.display='none'; var n=this.nextElementSibling; if(n){ if(n.tagName==='IMG'){n.style.display=''} else {n.classList.remove('hidden')} }";
    // For gears we've built a VST canvas UI for, show the recreated plugin
    // face as the thumbnail (instead of the game art). dataURL renders
    // off-screen at default knob values; if fonts haven't loaded yet the one
    // re-render kicked off by RBPedalCanvas.ready() (see rbApplyGearFilters)
    // repaints it correctly.
    const gStem = isVst ? g.vst_path.split('/').pop().replace(/\.(vst3|component)$/i, '').toLowerCase().replace(/[^a-z0-9]/g, '') : '';
    const canvasArt = (gStem && window.RBPedalCanvas && window.RBPedalCanvas.has(gStem))
        ? window.RBPedalCanvas.dataURL(gStem, {}) : null;
    const canvasImgTag = canvasArt
        ? `<img src="${canvasArt}" alt="" style="max-width:100%;max-height:100%;object-fit:contain"
               class="max-w-full max-h-full rounded object-contain" onerror="${onerrChain}">`
        : '';
    // Inline width/height/object-fit (not just Tailwind w-16 etc.) so the
    // thumbnail stays small even where the host's purged CSS build drops the
    // plugin-only sizing utilities — without it the raw ~512px art renders
    // full size (the "gear photos huge on Windows" bug).
    const t3kImgTag = g.image
        ? `<img src="${rbEsc(g.image)}" alt="" loading="lazy"
               style="display:none;max-width:100%;max-height:100%;object-fit:cover"
               class="max-w-full max-h-full rounded object-cover bg-dark-900"
               onerror="${onerrChain}">`
        : '';
    const photoBlock = `
        <div class="flex-shrink-0 w-16 h-16 flex items-center justify-center rounded bg-dark-900 overflow-hidden transition ${photoOff}"
             style="width:64px;height:64px"
             title="${isAssigned ? '' : 'Unassigned — no NAM/IR/VST mapped yet'}">
            ${canvasImgTag}
            <img src="${rsArt}" alt="" loading="lazy"
                 style="${canvasArt ? 'display:none;' : ''}max-width:100%;max-height:100%;object-fit:contain"
                 class="max-w-full max-h-full rounded object-contain"
                 onerror="${onerrChain}">
            ${t3kImgTag}
            <div class="hidden w-full h-full flex items-center justify-center text-gray-700 text-[10px] uppercase tracking-wide">${rbEsc(g.category || 'gear')}</div>
        </div>`;

    // Action buttons (only rendered when expanded).
    const btnId = `rb-aud-${_rbCatalogSeq++}`;
    // ▶ Listen visibility:
    //   - VST → always (audition has no inline equivalent)
    //   - Amp with curated gain_variants → no (the ▶ clean/crunch/dist
    //     row covers it with better labels)
    //   - Cab with mic_variants → no (the ▶ Dynamic Cone / Condenser
    //     Edge / … row covers it)
    //   - Pedal / rack / "other" / amp w/o variants → YES (those have
    //     no inline audition row, so Listen is the only way to hear
    //     the assigned gear in isolation from the catalog).
    const hasInlineAudition = (
        (Array.isArray(g.variants) && g.variants.length > 0)
        || (Array.isArray(g.mic_variants) && g.mic_variants.length > 0)
    );
    let listenBtn = '';
    let editBtn = '';
    if (isVst) {
        listenBtn = `<button id="${btnId}" onclick="event.stopPropagation(); rbAuditionVst('${rbEsc(g.vst_path).replace(/'/g,"\\'")}','${rbEsc(g.vst_format || 'VST3')}','${btnId}','${rbEsc(g.rs_gear)}')"
                            title="Listen to this VST in isolation"
                            class="bg-purple-700/50 hover:bg-purple-600/60 text-purple-100 px-3 py-1.5 rounded text-xs">▶ Listen</button>`;
        // Direct "edit this VST" — loads the plugin and opens the native
        // editor window. Saves a click vs the 📚 Library re-pick flow when
        // the gear already has a VST assigned (the common case after the
        // bulk-assign step). Passes rs_gear so rbCatalogEditVst can apply
        // the (gear, vst) `_static` defaults (e.g. kHs Distortion Type for
        // fuzz/od/dist pedals, MEqualizer band-enable flags).
        editBtn = ''
    } else if (g.assigned && !hasInlineAudition) {
        listenBtn = `<button id="${btnId}" onclick="event.stopPropagation(); rbAuditionFile('${rbEsc(g.file).replace(/'/g,"\\'")}', '${rbEsc(g.kind || 'nam')}', '${btnId}', undefined, '${rbEsc(g.rs_gear || '')}')"
                            title="Listen to this gear in isolation"
                            class="bg-dark-600 hover:bg-dark-500 text-gray-200 px-3 py-1.5 rounded text-xs">▶ Listen</button>`;
    }
    // tone3000 link → small icon in the card header, not a competing
    // button. Reduces the action-row noise.
    const t3kHeaderLink = g.tone3000_url
        ? `<a href="${rbEsc(g.tone3000_url)}" target="_blank" onclick="event.stopPropagation()"
              title="View on tone3000" aria-label="View on tone3000"
              class="text-gray-500 hover:text-accent text-base px-1 leading-none">↗</a>` : '';
    const variantsBtn = g.category === 'amp' ? `
        <button onclick="event.stopPropagation(); rbToggleAmpVariants('${rbEsc(g.rs_gear)}')"
                title="Map clean / crunch / dist captures so the song's Gain knob picks the right one"
                class="bg-emerald-900/30 hover:bg-emerald-900/50 text-emerald-300 border border-emerald-800/40 px-3 py-1.5 rounded text-xs">🎚 Variants</button>` : '';
    const libraryBtn = `<button onclick="event.stopPropagation(); rbToggleCatalogLibrary('${rbEsc(g.rs_gear)}','${rbEsc(g.category || '')}','${rbEsc(g.vst_path || '')}','${rbEsc(g.vst_format || 'VST3')}')"
                                title="Pick a downloaded NAM/IR or an installed VST/AU and bulk-assign to every preset using this gear"
                                class="bg-indigo-900/30 hover:bg-indigo-900/50 text-indigo-300 border border-indigo-800/40 px-3 py-1.5 rounded text-xs">📚 Library</button>`;
    const searchBtn = `<button onclick="event.stopPropagation(); rbOpenSuggest('${rbEsc(g.rs_gear)}')"
                                title="Search tone3000 for more candidate captures for this gear"
                                class="text-gray-400 hover:text-gray-200 text-xs px-2 py-1.5">🔍 Search tone3000</button>`;

    // Audition row for curated multi-NAM amps — one mini ▶ per variant
    // (clean/crunch/dist). A/B the captures without leaving the catalog.
    let variantAuditionRow = '';
    if (Array.isArray(g.variants) && g.variants.length) {
        const btns = g.variants.map(v => {
            const vId = `rb-aud-${_rbCatalogSeq++}`;
            if (!v.available || !v.file) {
                return `<button disabled title="NAM not downloaded — Setup → Download all curated variants"
                                class="text-[10px] px-2 py-0.5 rounded bg-dark-800/50 text-gray-600 cursor-not-allowed">▶ ${rbEsc(v.level)}</button>`;
            }
            // Per-level perceptual trim: clean=1.0, crunch=0.71 (-3 dB),
            // dist=0.50 (-6 dB). Layers on top of the backend's LUFS
            // normalization to compensate for the harmonic-density boost
            // distortion captures get beyond integrated loudness.
            const trim = rbAuditionGainForVariantLevel(v.level);
            return `<button id="${vId}" onclick="event.stopPropagation(); rbAuditionFile('${rbEsc(v.file).replace(/'/g,"\\'")}','nam','${vId}',${trim},'${rbEsc(g.rs_gear || '')}')"
                            title="${rbEsc(v.notes || v.level)} — A/B level-matched (${(20 * Math.log10(trim)).toFixed(0)} dB trim)"
                            class="text-[10px] px-2 py-0.5 rounded bg-emerald-900/30 hover:bg-emerald-900/60 text-emerald-300 border border-emerald-800/40">▶ ${rbEsc(v.level)}</button>`;
        }).join(' ');
        variantAuditionRow = `<div class="flex items-center gap-1 flex-wrap">
            <span class="text-[10px] text-gray-500">A/B variants:</span>${btns}
        </div>`;
    }

    // Audition row for cabs: one ▶ per mic position resolved from the
    // Wwise HIRC. Sky-blue tone to distinguish from amp variants. The
    // labels come from the gear manifest's Category field (e.g.
    // "Dynamic Cone") so the user reads "Dynamic close" / "Condenser
    // edge" instead of generic "IR 0/1/2/…". Each variant is a
    // standalone IR — auditioning loads only that .wav, no chain.
    let micVariantAuditionRow = '';
    if (Array.isArray(g.mic_variants) && g.mic_variants.length) {
        const btns = g.mic_variants.map(v => {
            const vId = `rb-aud-${_rbCatalogSeq++}`;
            if (!v.available || !v.ir_file) {
                return `<button disabled title="IR not extracted — re-run Setup → Extract everything"
                                class="text-[10px] px-2 py-0.5 rounded bg-dark-800/50 text-gray-600 cursor-not-allowed">▶ ${rbEsc(v.label || v.suffix)}</button>`;
            }
            return `<button id="${vId}" onclick="event.stopPropagation(); rbAuditionFile('${rbEsc(v.ir_file).replace(/'/g,"\\'")}','ir','${vId}')"
                            title="${rbEsc(v.mic_type || '')} · ${rbEsc(v.position || '')} (suffix ${rbEsc(v.suffix)})"
                            class="text-[10px] px-2 py-0.5 rounded bg-sky-900/30 hover:bg-sky-900/60 text-sky-300 border border-sky-800/40">▶ ${rbEsc(v.label || v.suffix)}</button>`;
        }).join(' ');
        micVariantAuditionRow = `<div class="flex items-center gap-1 flex-wrap">
            <span class="text-[10px] text-gray-500">Mic positions:</span>${btns}
        </div>`;
    }

    // Layout (expanded):
    //   1. Current assignment line (what's loaded now)
    //   2. A/B variant audition row (the most useful interactive bit on
    //      amp cards — promoted to the TOP so the user can sample
    //      without scrolling past 5 buttons)
    //   3. Mic-position row (cabs)
    //   4. Primary actions: ▶ Listen · 🎛 Edit (VSTs) · 🎚 Variants (amps)
    //      · 📚 Library
    //   5. Secondary: 🔍 Search (small, low-contrast)
    //   6. Sub-panels — stopPropagation on the wrapper so any click
    //      inside (input, list item, dropdown) doesn't bubble up to
    //      the card's collapse handler. That was the bug where opening
    //      Library/Variants and then touching the panel collapsed it.
    const actionsPanel = expanded ? `
        <div class="border-t border-gray-800/50 mt-2 pt-2 space-y-2"
             onclick="event.stopPropagation()">
            ${assignedLine}
            ${variantAuditionRow}
            ${micVariantAuditionRow}
            <div class="flex flex-wrap items-center gap-1.5">
                ${listenBtn}
                ${editBtn}
                ${variantsBtn}
                ${libraryBtn}
                <div class="flex-1"></div>
                ${searchBtn}
            </div>
            <div id="rb-cat-edit-${safeId}" class="hidden bg-purple-900/10 border border-purple-800/30 rounded p-2"></div>
            <div id="rb-cat-lib-${safeId}" class="hidden bg-indigo-900/10 border border-indigo-800/30 rounded p-2"></div>
            <div id="rb-cat-variants-${safeId}" class="hidden bg-emerald-900/10 border border-emerald-800/30 rounded p-2"></div>
        </div>` : '';

    const chevron = expanded ? '▼' : '▶';
    const cardHighlight = expanded
        ? 'border-accent/40 bg-dark-700/70'
        : 'border-gray-800/50 bg-dark-700/40 hover:border-gray-700 hover:bg-dark-700/60';

    return `
        <div onclick="rbToggleGearCard('${rbEsc(g.rs_gear)}')"
             class="cursor-pointer border rounded-lg p-3 transition ${cardHighlight}">
            <div class="flex items-start gap-3">
                ${photoBlock}
                <div class="min-w-0 flex-1">
                    <div class="text-gray-100 font-medium leading-tight break-words" title="${rbEsc(g.real_name)}">${rbEsc(g.real_name)}</div>
                </div>
                <div class="flex items-center gap-1 flex-shrink-0 mt-0.5">
                    ${t3kHeaderLink}
                    <span class="text-gray-500 text-xs select-none" aria-hidden="true">${chevron}</span>
                </div>
            </div>
            ${actionsPanel}
        </div>`;
}

// Toggle the expanded state for one gear. Re-renders the catalog so
// the action panel materializes (or collapses). The expanded set is
// persisted on rbState so a tab switch and back keeps the panel open.
function rbToggleGearCard(rsGear) {
    if (!rbState.gearExpanded) rbState.gearExpanded = new Set();
    if (rbState.gearExpanded.has(rsGear)) rbState.gearExpanded.delete(rsGear);
    else rbState.gearExpanded.add(rsGear);
    rbApplyGearFilters();
}

// Toggle + render the Gain-variants panel for an amp in the Gear catalog.
// Fetches GET /amp_variants/{rs_gear}, then builds three slots
// (clean / crunch / dist) showing the current pick + edit controls.
async function rbToggleAmpVariants(rsGear) {
    const safeId = rsGear.replace(/[^a-zA-Z0-9_-]/g, '_');
    const el = document.getElementById(`rb-cat-variants-${safeId}`);
    if (!el) return;
    if (!el.classList.contains('hidden')) {
        el.classList.add('hidden');
        return;
    }
    // Mutual exclusivity: opening Variants closes Library (and vice versa).
    // Sibling panels under the same card would otherwise stack up vertically
    // and the user wouldn't see the one they just clicked.
    const libEl = document.getElementById(`rb-cat-lib-${safeId}`);
    if (libEl) libEl.classList.add('hidden');
    el.classList.remove('hidden');
    el.innerHTML = `<div class="text-xs text-gray-500">Loading…</div>`;
    try {
        const r = await fetch(`${window.RB_API}/amp_variants/${encodeURIComponent(rsGear)}`);
        if (!r.ok) throw new Error((await r.json().catch(()=>({}))).error || r.status);
        const data = await r.json();
        el.innerHTML = rbRenderAmpVariantsPanel(rsGear, data);
    } catch (e) {
        el.innerHTML = `<div class="text-xs text-red-400">load failed: ${rbEsc(e.message || e)}</div>`;
    }
}

// Build HTML for the three-slot variants panel. Pre-fills each slot
// with the current variant (if any) and shows the default range
// labels next to each level name.
function rbRenderAmpVariantsPanel(rsGear, data) {
    const safeId = rsGear.replace(/[^a-zA-Z0-9_-]/g, '_');
    const variants = data.variants || {};
    const defaults = data.default_levels || {};
    const levels = ['clean', 'crunch', 'dist'];

    // Quick mode header: paste ONE tone3000 link, "Load captures", and
    // every level row gets the same dropdown populated below — for the
    // common case where you want all three variants from the same
    // capturer's page. Per-level rows still let you override with a
    // different link if needed (collapsed by default to keep the panel
    // calm).
    const quickHeader = `
        <div class="bg-emerald-900/15 border border-emerald-800/30 rounded p-2.5 mb-3">
            <div class="text-[11px] text-emerald-300 font-medium mb-1">⚡ Quick — one link for all 3 levels</div>
            <div class="text-[10px] text-gray-500 mb-2">
                Paste a tone3000 amp page (URL or ID). After loading, pick
                one capture per level from the dropdowns below — no need
                to know what a model_id is.
            </div>
            <div class="flex items-center gap-2">
                <input id="rb-amp-quick-tone-${safeId}" type="text"
                       placeholder="https://tone3000.com/tones/37987   or just   37987"
                       class="flex-1 bg-dark-900 border border-gray-800 rounded text-[11px] text-gray-200 px-2 py-1 font-mono">
                <button onclick="rbAmpVariantsQuickLoad('${rbEsc(rsGear)}')"
                        class="bg-emerald-700 hover:bg-emerald-600 text-white text-[11px] px-3 py-1 rounded whitespace-nowrap">⬇ Load captures</button>
            </div>
            <div id="rb-amp-quick-status-${safeId}" class="text-[10px] text-gray-500 mt-1.5"></div>
        </div>`;

    const rows = levels.map(level => {
        const v = variants[level] || {};
        const def = defaults[level] || { rs_gain_range: [0, 100] };
        const range = v.rs_gain_range || def.rs_gain_range;
        const tone3000Id = v.tone3000_id || '';
        const isSaved = !!v.tone3000_id;
        const captureName = (v.notes || '').trim();
        const slotPrefix = `rb-amp-variants-${safeId}-${level}`;
        // Saved-state header line: prefer the human capture name
        // (notes) over the generic "✓ saved". Truncate to keep the row
        // compact — full text on hover.
        let savedBadge;
        if (isSaved) {
            const shown = captureName ? captureName : `tone3000 #${tone3000Id}`;
            savedBadge = `<span class="text-[10px] text-emerald-400 truncate max-w-[24rem]"
                                title="${rbEsc(captureName || ('tone3000 #' + tone3000Id))}">✓ ${rbEsc(shown)}</span>`;
        } else {
            savedBadge = '<span class="text-[10px] text-gray-600">empty</span>';
        }
        return `
            <div class="bg-dark-800/60 border border-gray-800/40 rounded p-2 mb-2" id="${slotPrefix}">
                <div class="flex items-center justify-between gap-2 mb-1.5">
                    <div class="flex items-center gap-2 min-w-0">
                        <span class="font-semibold text-emerald-300 capitalize">${level}</span>
                        <span class="text-[10px] text-gray-500 whitespace-nowrap">Gain ${range[0]}-${range[1]}</span>
                        ${savedBadge}
                    </div>
                    ${isSaved ? `<button onclick="rbDeleteAmpVariant('${rbEsc(rsGear)}', '${level}')"
                                        class="text-[10px] text-red-400 hover:text-red-300 px-1.5 py-0.5 flex-shrink-0">Remove</button>` : ''}
                </div>
                <div class="flex items-center gap-2 mb-1.5">
                    <span class="text-[10px] text-gray-500 whitespace-nowrap">Capture:</span>
                    <select id="${slotPrefix}-model"
                            class="flex-1 bg-dark-900 border border-gray-800 rounded text-[10px] text-gray-200 px-1 py-1"
                            disabled>
                        <option value="">(load captures via ⚡ Quick or 🔗 Use a different link)</option>
                    </select>
                </div>
                <details class="text-[10px] text-gray-500 mb-1">
                    <summary class="cursor-pointer hover:text-gray-300 select-none">🔗 Use a different tone3000 link for ${level}</summary>
                    <div class="flex items-center gap-2 mt-1.5">
                        <input id="${slotPrefix}-tone" type="text" placeholder="tone3000 URL or ID"
                               value="${rbEsc(tone3000Id)}"
                               class="flex-1 bg-dark-900 border border-gray-800 rounded text-[11px] text-gray-200 px-2 py-1 font-mono">
                        <button onclick="rbInspectAmpVariant('${rbEsc(rsGear)}', '${level}')"
                                class="bg-dark-600 hover:bg-dark-500 text-gray-200 text-[10px] px-2 py-1 rounded whitespace-nowrap">⬇ Load</button>
                    </div>
                </details>
                <div class="flex items-center gap-2">
                    <button onclick="rbSaveAmpVariant('${rbEsc(rsGear)}', '${level}')"
                            class="bg-emerald-700 hover:bg-emerald-600 text-white text-[11px] px-2.5 py-1 rounded">💾 Save ${level}</button>
                    <span id="${slotPrefix}-status" class="text-[10px] text-gray-500"></span>
                </div>
            </div>`;
    }).join('');
    return `
        <div class="text-xs text-gray-400 mb-2">
            Map a capture to each gain range. The song's Gain knob picks
            which one plays. Leave a level empty to skip it (the closest
            variant covers that range).
        </div>
        ${quickHeader}
        ${rows}`;
}

// Quick mode: paste one tone3000 link, fetch captures once, populate
// the dropdown for every level. The user then picks one capture per
// level and saves. The per-level "Use a different link" override
// still works on top of this — its own dropdown wins for that level.
async function rbAmpVariantsQuickLoad(rsGear) {
    const safeId = rsGear.replace(/[^a-zA-Z0-9_-]/g, '_');
    const input = document.getElementById(`rb-amp-quick-tone-${safeId}`);
    const statusEl = document.getElementById(`rb-amp-quick-status-${safeId}`);
    if (!input || !statusEl) return;
    const raw = (input.value || '').trim();
    const m = raw.match(/(\d+)\s*$/);
    if (!m) {
        statusEl.textContent = 'enter a tone3000 URL or numeric ID';
        statusEl.className = 'text-[10px] text-amber-300 mt-1.5';
        return;
    }
    const toneId = parseInt(m[1], 10);
    statusEl.textContent = 'fetching captures…';
    statusEl.className = 'text-[10px] text-gray-500 mt-1.5';
    try {
        const r = await fetch(`${window.RB_API}/tone3000/captures/${toneId}`);
        const data = await r.json();
        if (!r.ok) throw new Error(data.error || r.status);
        const caps = data.captures || [];
        if (!caps.length) {
            statusEl.textContent = 'no captures in this tone';
            statusEl.className = 'text-[10px] text-amber-300 mt-1.5';
            return;
        }
        // Populate every level's dropdown with the same list. The
        // model_id is hidden inside the option's value — the user only
        // sees the capture name + size + license. Each level also
        // gets a `data-tone-id` so Save knows which tone3000 page this
        // capture came from (quick mode = shared id; per-level mode
        // overrides per row).
        const optsHtml = '<option value="">(pick a capture for this level)</option>' +
            caps.map(c => {
                const meta = [c.size || '?', c.license || ''].filter(Boolean).join(' · ');
                return `<option value="${c.model_id}">${rbEsc(c.name)}${meta ? ` — ${rbEsc(meta)}` : ''}</option>`;
            }).join('');
        for (const level of ['clean', 'crunch', 'dist']) {
            const sel = document.getElementById(`rb-amp-variants-${safeId}-${level}-model`);
            if (sel) {
                sel.innerHTML = optsHtml;
                sel.dataset.toneId = String(toneId);
                sel.dataset.source = 'quick';
                sel.disabled = false;
                // Stash the capture names so Save can record `notes`
                // (human-readable label) without re-querying the API.
                sel._rbCaptures = caps;
            }
        }
        statusEl.innerHTML = `<span class="text-emerald-400">✓ ${caps.length} captures loaded — pick one per level and Save</span>`;
        statusEl.className = 'text-[10px] mt-1.5';
    } catch (e) {
        statusEl.textContent = `failed: ${e.message || e}`;
        statusEl.className = 'text-[10px] text-red-400 mt-1.5';
    }
}

// Inspect the captures inside a tone3000 page (GET /tone3000/captures/{id})
// and populate this LEVEL's dropdown only. Used by the per-level
// "Use a different link" override; the Quick mode populates all 3
// dropdowns in one go via rbAmpVariantsQuickLoad.
async function rbInspectAmpVariant(rsGear, level) {
    const safeId = rsGear.replace(/[^a-zA-Z0-9_-]/g, '_');
    const slotPrefix = `rb-amp-variants-${safeId}-${level}`;
    const input = document.getElementById(`${slotPrefix}-tone`);
    const statusEl = document.getElementById(`${slotPrefix}-status`);
    const select  = document.getElementById(`${slotPrefix}-model`);
    if (!input || !statusEl || !select) return;
    const raw = (input.value || '').trim();
    const m = raw.match(/(\d+)\s*$/);
    if (!m) {
        statusEl.textContent = 'enter a tone3000 URL or numeric ID';
        statusEl.className = 'text-[10px] text-amber-300';
        return;
    }
    const toneId = parseInt(m[1], 10);
    statusEl.textContent = 'fetching captures…';
    statusEl.className = 'text-[10px] text-gray-500';
    try {
        const r = await fetch(`${window.RB_API}/tone3000/captures/${toneId}`);
        const data = await r.json();
        if (!r.ok) throw new Error(data.error || r.status);
        const caps = data.captures || [];
        if (!caps.length) {
            statusEl.textContent = 'no captures in this tone';
            statusEl.className = 'text-[10px] text-amber-300';
            return;
        }
        // Order matters: the capture's title (which encodes knob
        // settings like "G7 B5 M5 T5 P5 V5") is what the user reads to
        // match a game gain level — put it first. Size/license
        // are secondary metadata tail-tagged.
        select.innerHTML = `<option value="">(pick a capture for this level)</option>` +
            caps.map(c => {
                const meta = [c.size || '?', c.license || ''].filter(Boolean).join(' · ');
                return `<option value="${c.model_id}">${rbEsc(c.name)}${meta ? ` — ${rbEsc(meta)}` : ''}</option>`;
            }).join('');
        select.dataset.toneId = String(toneId);
        select.dataset.source = 'custom';
        select.disabled = false;
        select._rbCaptures = caps;
        statusEl.textContent = `${caps.length} capture${caps.length === 1 ? '' : 's'} loaded — pick one and Save`;
        statusEl.className = 'text-[10px] text-emerald-400';
    } catch (e) {
        statusEl.textContent = `failed: ${e.message || e}`;
        statusEl.className = 'text-[10px] text-red-400';
    }
}

// Persist a single variant. POSTs to /amp_variants/{rs_gear}/{level}.
// Picks tone3000_id from the SELECT's dataset (Quick mode + per-level
// both stash it there) so we don't depend on the per-level URL input
// being filled — Quick mode users never touched that field.
async function rbSaveAmpVariant(rsGear, level) {
    const safeId = rsGear.replace(/[^a-zA-Z0-9_-]/g, '_');
    const slotPrefix = `rb-amp-variants-${safeId}-${level}`;
    const select = document.getElementById(`${slotPrefix}-model`);
    const statusEl = document.getElementById(`${slotPrefix}-status`);
    if (!select || !statusEl) return;
    const toneIdStr = (select.dataset && select.dataset.toneId) || '';
    if (!toneIdStr) {
        statusEl.textContent = 'load captures first (⚡ Quick or 🔗 different link)';
        statusEl.className = 'text-[10px] text-amber-300';
        return;
    }
    const tone3000Id = parseInt(toneIdStr, 10);
    const modelId = (select.value) ? parseInt(select.value, 10) : null;
    if (!modelId) {
        statusEl.textContent = 'pick a capture from the dropdown first';
        statusEl.className = 'text-[10px] text-amber-300';
        return;
    }
    // Find the chosen capture's human name and pass it as `notes` so
    // the saved-row badge shows "✓ G3 B5 M5 T5 P5 V5" instead of just
    // "✓ saved". Falls back gracefully if the capture metadata isn't
    // attached to the SELECT for any reason.
    let notes = '';
    const caps = select._rbCaptures || [];
    const match = caps.find(c => String(c.model_id) === String(modelId));
    if (match && match.name) notes = match.name;

    statusEl.textContent = 'saving…';
    statusEl.className = 'text-[10px] text-gray-500';
    try {
        const r = await fetch(`${window.RB_API}/amp_variants/${encodeURIComponent(rsGear)}/${encodeURIComponent(level)}`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                tone3000_id: tone3000Id,
                model_id: modelId,
                notes: notes,
            }),
        });
        const data = await r.json();
        if (!r.ok) throw new Error(data.error || r.status);
        statusEl.textContent = '✓ saved — re-run Batch all to download';
        statusEl.className = 'text-[10px] text-emerald-400';
        // Re-render the panel so the saved badge appears.
        setTimeout(() => rbReopenAmpVariants(rsGear), 600);
    } catch (e) {
        statusEl.textContent = `save failed: ${e.message || e}`;
        statusEl.className = 'text-[10px] text-red-400';
    }
}

// Remove a single variant.
async function rbDeleteAmpVariant(rsGear, level) {
    if (!confirm(`Remove the "${level}" variant for ${rsGear}?`)) return;
    try {
        const r = await fetch(`${window.RB_API}/amp_variants/${encodeURIComponent(rsGear)}/${encodeURIComponent(level)}`, {
            method: 'DELETE',
        });
        if (!r.ok) {
            const data = await r.json().catch(()=>({}));
            alert(`delete failed: ${data.error || r.status}`);
            return;
        }
        rbReopenAmpVariants(rsGear);
    } catch (e) {
        alert(`delete failed: ${e.message || e}`);
    }
}

// Helper: close + re-open the panel so it reloads from the backend after
// a Save / Delete. Cheaper than rendering diffs in place.
function rbReopenAmpVariants(rsGear) {
    const safeId = rsGear.replace(/[^a-zA-Z0-9_-]/g, '_');
    const el = document.getElementById(`rb-cat-variants-${safeId}`);
    if (!el) return;
    el.classList.add('hidden');
    rbToggleAmpVariants(rsGear);
}

// (catalog-level bulk Replace removed: the Gear tab no longer exposes
// a global swap. Per-song gear swapping lives in the Songs editor's
// 🔁 Swap button — the backend POST /gear/replace_with endpoint still
// supports both modes, the UI just doesn't surface the global one.)

// Open the catalog-card library picker (bulk-assigns to every preset using
// this rs_gear_type). `category` tells us whether to list NAMs or IRs.
async function rbToggleCatalogLibrary(rsGear, category, vstPath, vstFormat) {
    const safeId = rsGear.replace(/[^a-zA-Z0-9_-]/g, '_');
    const el = document.getElementById(`rb-cat-lib-${safeId}`);
    if (!el) return;
    // Mutual exclusivity with the Variants panel (sibling under the
    // same card) — opening Library closes Variants so the user only
    // sees the one they just clicked.
    const varEl = document.getElementById(`rb-cat-variants-${safeId}`);
    if (varEl) varEl.classList.add('hidden');
    el.classList.toggle('hidden');
    if (el.classList.contains('hidden')) return;
    if (el.dataset.built === '1') return;
    el.dataset.built = '1';
    el._rbVstPath = vstPath || '';
    el._rbVstFormat = vstFormat || 'VST3';
    el._rbCategory = category || '';
    const fileLabel = category === 'cab' ? 'IRs' : 'NAMs';
    el.innerHTML = `
        <div class="flex items-center gap-1 mb-2 border-b border-gray-800">
            <button id="rb-cat-lib-tab-files-${safeId}" onclick="rbCatLibTab('${rbEsc(rsGear)}', 'files')"
                    class="px-3 py-1 text-xs border-b-2">📚 ${fileLabel}</button>
            <button id="rb-cat-lib-tab-plugins-${safeId}" onclick="rbCatLibTab('${rbEsc(rsGear)}', 'plugins')"
                    class="px-3 py-1 text-xs border-b-2">🎛 Plugins</button>
        </div>
        <div id="rb-cat-lib-content-${safeId}"></div>`;
    rbCatLibTab(rsGear, 'files');
}

// Switch the gear-catalog library picker between local NAM/IR files (bulk-
// assign to every preset using this gear) and the scanned VST/AU plugins
// (reusing the catalog VST panel). VST path/format are stashed on the element.
async function rbCatLibTab(rsGear, tab) {
    const safeId = rsGear.replace(/[^a-zA-Z0-9_-]/g, '_');
    const el = document.getElementById(`rb-cat-lib-${safeId}`);
    const content = document.getElementById(`rb-cat-lib-content-${safeId}`);
    if (!el || !content) return;
    for (const t of ['files', 'plugins']) {
        const b = document.getElementById(`rb-cat-lib-tab-${t}-${safeId}`);
        if (b) {
            const on = t === tab;
            b.classList.toggle('border-indigo-400', on);
            b.classList.toggle('text-indigo-300', on);
            b.classList.toggle('border-transparent', !on);
            b.classList.toggle('text-gray-400', !on);
        }
    }
    if (tab === 'plugins') {
        content.innerHTML = rbRenderCatalogVstPanelBody(
            `rb-cat-vst-${safeId}`, rsGear, el._rbVstPath || '', el._rbVstFormat || 'VST3');
        return;
    }
    const kind = (el._rbCategory === 'cab') ? 'ir' : 'nam';
    if (el.dataset.filesLoaded !== '1') {
        content.innerHTML = `<div class="text-xs text-gray-500">loading library…</div>`;
        try {
            // Fetch the whole bucket for this `kind` (no `category` query
            // param) so the picker can show NAMs/IRs from EVERY subdir
            // grouped together. The user might want to assign an amp NAM
            // to a pedal slot (or vice versa) for experimentation — the
            // category-restricted version blocked that. Defaults to the
            // current gear's category being expanded; others collapsed.
            const r = await fetch(`${window.RB_API}/local_files?kind=${kind}`);
            if (!r.ok) throw new Error(`HTTP ${r.status}`);
            const data = await r.json();
            el._rbAllFiles = data.files || [];
            el.dataset.kind = kind;
            el.dataset.filesLoaded = '1';
        } catch (e) {
            content.innerHTML = `<div class="text-xs text-red-400">Failed to load library: ${rbEsc(e.message || e)}</div>`;
            return;
        }
    }
    rbRenderCatalogLibraryList(content, el._rbAllFiles, rsGear, kind, '');
}

// Friendly labels for the subdir categories the v1.2 storage layout
// uses. Anything not matching one of these (legacy flat files, RS-
// extracted IRs under rocksmith/, etc.) lands in the appropriate
// "other" bucket per kind.
const _RB_LIB_CATEGORY_LABEL = {
    amps:      '🎚 Amps',
    pedals:    '🎛 Pedals',
    racks:     '📦 Racks',
    cabs:      '🔊 Cabs',
    rocksmith: '🎮 Cab IRs',
    other:     '… Other',
};

// Pick the bucket for a relative filename based on its subdir prefix.
// Falls back to "other" when no subdir is present (legacy flat layout)
// or the subdir isn't one we know about.
function rbLibBucketFor(name) {
    const i = name.indexOf('/');
    if (i < 0) return 'other';
    const head = name.slice(0, i);
    return (head in _RB_LIB_CATEGORY_LABEL) ? head : 'other';
}

function rbRenderCatalogLibraryList(container, files, rsGear, kind, filter) {
    const safeId = rsGear.replace(/[^a-zA-Z0-9_-]/g, '_');
    const inputId = `rb-cat-lib-search-${safeId}`;
    const countId = `rb-cat-lib-count-${safeId}`;
    const rowsId  = `rb-cat-lib-rows-${safeId}`;
    container.innerHTML = `
        <div class="text-[10px] text-indigo-300 mb-1">
            Pick from your downloaded ${kind === 'ir' ? 'IRs' : 'NAMs'} · "Use for all" applies to every preset using <code>${rbEsc(rsGear)}</code>
        </div>
        <div class="flex items-center gap-2 mb-2">
            <input id="${inputId}" type="text" placeholder="🔍 Filter…"
                   oninput="rbFilterCatalogLibrary('${rbEsc(rsGear)}')"
                   value="${rbEsc(filter || '')}"
                   class="flex-1 bg-dark-800 border border-gray-800 rounded text-[11px] text-gray-200 px-2 py-1">
            <span id="${countId}" class="text-[10px] text-gray-500">${files.length}/${files.length}</span>
        </div>
        <div id="${rowsId}" class="max-h-72 overflow-y-auto"></div>`;
    rbRenderCatalogLibraryRows(container, files, rsGear, kind, filter);
}

function rbRenderCatalogLibraryRows(container, files, rsGear, kind, filter) {
    const safeId = rsGear.replace(/[^a-zA-Z0-9_-]/g, '_');
    const rowsEl  = document.getElementById(`rb-cat-lib-rows-${safeId}`);
    const countEl = document.getElementById(`rb-cat-lib-count-${safeId}`);
    if (!rowsEl) return;
    const f = (filter || '').toLowerCase().trim();
    const filtered = f
        ? files.filter(x => (x.name + ' ' + (x.title || '')).toLowerCase().includes(f))
        : files;
    const titleCounts = {};
    filtered.forEach(x => { if (x.title) titleCounts[x.title] = (titleCounts[x.title] || 0) + 1; });
    // Group files by subdir bucket — keeps the picker readable when
    // the user has hundreds of files spread across categories.
    const buckets = {};
    for (const file of filtered) {
        const b = rbLibBucketFor(file.name);
        (buckets[b] = buckets[b] || []).push(file);
    }
    // Render order is intent-aware: the current gear's category first
    // (expanded by default), then the rest in the canonical order
    // amps → pedals → racks → cabs → rocksmith → other.
    const containerEl = document.getElementById(`rb-cat-lib-${safeId}`);
    const currentCat = (containerEl && containerEl._rbCategory) || '';
    const currentBucket = (
        currentCat === 'amp' ? 'amps' :
        currentCat === 'pedal' ? 'pedals' :
        currentCat === 'rack' ? 'racks' :
        currentCat === 'cab' ? 'cabs' : null
    );
    const canonOrder = ['amps', 'pedals', 'racks', 'cabs', 'rocksmith', 'other'];
    const orderedBuckets = [
        ...(currentBucket && buckets[currentBucket] ? [currentBucket] : []),
        ...canonOrder.filter(b => b !== currentBucket && buckets[b]),
    ];
    // Track open/closed sections on the container so re-rendering on
    // filter input preserves what the user expanded. By default the
    // current-category bucket is open; the rest are collapsed unless
    // there's an active filter (then everything stays open so
    // matches are visible).
    if (!containerEl._rbBucketOpen) {
        containerEl._rbBucketOpen = {};
        for (const b of orderedBuckets) {
            containerEl._rbBucketOpen[b] = (b === currentBucket);
        }
    }
    const renderRow = (file) => {
        const usedBadge = file.use_count > 0
            ? `<span class="text-[10px] text-amber-300/80" title="${rbEsc((file.used_for_gears || []).join(', '))}">used ${file.use_count}×</span>`
            : `<span class="text-[10px] text-gray-600">unused</span>`;
        const safeName = file.name.replace(/'/g, "\\'");
        return `
            <div class="flex items-center gap-2 px-2 py-1 hover:bg-indigo-900/20 rounded">
                <span class="flex-1 text-[11px] text-gray-200 truncate" title="${rbEsc(file.name)}">${rbLibLabel(file, titleCounts)}</span>
                ${usedBadge}
                <button onclick="rbAuditionFile('${rbEsc(safeName)}', '${rbEsc(kind === 'ir' ? 'ir' : 'nam')}', null)"
                        title="Audition in isolation"
                        class="text-[10px] text-gray-400 hover:text-gray-200 px-1">▶</button>
                <button onclick="rbCatalogBulkAssignLocal('${rbEsc(rsGear)}', '${rbEsc(safeName)}', '${rbEsc(kind)}')"
                        title="Apply to every preset using ${rbEsc(rsGear)}"
                        class="bg-indigo-700 hover:bg-indigo-600 text-white text-[10px] px-2 py-0.5 rounded">Use for all</button>
            </div>`;
    };
    const groupsHtml = orderedBuckets.map(b => {
        const list = buckets[b];
        // With an active filter, expand all matching buckets so the
        // user sees what they searched for.
        const open = f ? true : !!containerEl._rbBucketOpen[b];
        const label = _RB_LIB_CATEGORY_LABEL[b] || b;
        const isCurrent = (b === currentBucket);
        const rows = list.slice(0, 50).map(renderRow).join('');
        const moreNote = list.length > 50
            ? `<div class="text-[10px] text-gray-500 italic px-2 py-0.5">…and ${list.length - 50} more in this category (refine search)</div>`
            : '';
        return `
            <details ${open ? 'open' : ''}
                     onclick="event.stopPropagation()"
                     ontoggle="rbCatLibToggleBucket('${rbEsc(rsGear)}','${rbEsc(b)}', this.open)"
                     class="mb-1">
                <summary class="cursor-pointer select-none px-2 py-1 text-[11px] ${isCurrent ? 'text-indigo-300 font-semibold' : 'text-gray-400'} hover:text-gray-200">
                    ${label} <span class="text-gray-600">(${list.length})</span>${isCurrent ? ' <span class="text-[9px] text-indigo-400">· this gear&apos;s category</span>' : ''}
                </summary>
                ${rows}${moreNote}
            </details>`;
    }).join('');
    rowsEl.innerHTML = groupsHtml || '<div class="text-xs text-gray-500 italic">no matches</div>';
    if (countEl) countEl.textContent = `${filtered.length}/${files.length}`;
}

// Persist which buckets the user expanded/collapsed so a filter
// re-render doesn't reset their open state.
function rbCatLibToggleBucket(rsGear, bucket, isOpen) {
    const safeId = rsGear.replace(/[^a-zA-Z0-9_-]/g, '_');
    const el = document.getElementById(`rb-cat-lib-${safeId}`);
    if (!el) return;
    el._rbBucketOpen = el._rbBucketOpen || {};
    el._rbBucketOpen[bucket] = !!isOpen;
}

function rbFilterCatalogLibrary(rsGear) {
    const safeId = rsGear.replace(/[^a-zA-Z0-9_-]/g, '_');
    const container = document.getElementById(`rb-cat-lib-${safeId}`);
    if (!container || !container._rbAllFiles) return;
    const input = document.getElementById(`rb-cat-lib-search-${safeId}`);
    rbRenderCatalogLibraryRows(container, container._rbAllFiles, rsGear,
                               container.dataset.kind || 'nam', input ? input.value : '');
}

// Bulk-assign a local file (NAM or IR) to every preset_pieces row for this
// rs_gear_type. Uses the same /upload_for_gear endpoint flow — except no
// upload, just point at an existing file.
async function rbCatalogBulkAssignLocal(rsGear, fileName, kind) {
    if (!confirm(`Apply "${fileName}" to every preset using ${rsGear}?`)) return;
    try {
        const r = await fetch(`${window.RB_API}/use_local_for_gear`, {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({
                rs_gear: rsGear,
                local_file: fileName,
                local_kind: kind,
            }),
        });
        if (!r.ok) {
            const err = await r.json().catch(() => ({}));
            throw new Error(err.error || `HTTP ${r.status}`);
        }
        const data = await r.json();
        alert(`Applied "${fileName}" — ${data.pieces_updated || 0} piece(s) updated across ${data.presets_updated || 0} preset(s).`);
        // Reload the catalog so cards reflect the new assignment.
        setTimeout(() => rbLoadCatalog(), 400);
    } catch (e) {
        alert(`Bulk assign failed: ${e.message || e}`);
    }
}

// Open/close + lazy-fill the catalog card's VST panel. Lazy-fill avoids
// 1000s of dropdown <option> nodes when the user has many installed plugins.
function rbToggleCatalogVstPanel(panelId, rsGear, currentVstPath, currentFormat) {
    const el = document.getElementById(panelId);
    if (!el) return;
    el.classList.toggle('hidden');
    if (el.classList.contains('hidden')) return;
    if (el.dataset.filled === '1') return;
    el.dataset.filled = '1';
    el.innerHTML = rbRenderCatalogVstPanelBody(panelId, rsGear, currentVstPath, currentFormat);
    // Async hint from suggest catalog.
    rbLoadCatalogVstSuggestions(rsGear, panelId);
}

function rbRenderCatalogVstPanelBody(panelId, rsGear, currentVstPath, currentFormat) {
    const known = rbState.knownVsts || [];
    // Look up any staged path (file picker landed here without scan, e.g.).
    const el = document.getElementById(panelId);
    const stagedPath = (el && el.dataset.stagedPath) || currentVstPath || '';
    const stagedName = stagedPath ? stagedPath.split('/').pop() : '(none selected)';

    // Plugin selector. Two flavours:
    //   - Scanned VSTs exist → single dropdown with a tiny "hide
    //     instruments" toggle. No separate search input — the dropdown
    //     already filters by name when you start typing in many
    //     browsers, and the per-row knob editor lives in the per-song
    //     editor anyway.
    //   - No scan yet → hint to use Pick file. The file picker is the
    //     scan-less path.
    let pluginSelector;
    if (known.length === 0) {
        pluginSelector = `
            <div class="text-[11px] text-gray-400">
                No plugins scanned yet — scan in <span class="text-gray-300">Settings → VST / Audio Unit plugins</span>,
                or use 📁 Pick file below.
            </div>`;
    } else {
        const selId = `${panelId}-select`;
        const opts = rbBuildVstOptions(stagedPath, '', true);
        pluginSelector = `
            <div class="flex items-center gap-2">
                <select id="${selId}" data-staged="${rbEsc(stagedPath)}"
                        onchange="rbCatalogStagePath('${rbEsc(panelId)}', this.value)"
                        class="flex-1 bg-dark-800 border border-gray-800 rounded text-xs text-gray-200 px-2 py-1">${opts}</select>
                <label class="text-[10px] text-gray-400 flex items-center gap-1 whitespace-nowrap">
                    <input id="${selId}-hideinst" type="checkbox" checked
                           onchange="rbFilterVstSelect('${rbEsc(selId)}')"> hide instruments
                </label>
            </div>`;
    }
    return `
        <div class="text-xs text-purple-300 font-semibold mb-1">VST3 / Audio Unit</div>
        ${pluginSelector}
        <div class="flex items-center gap-2 flex-wrap mt-2">
            <button onclick="rbCatalogPickFile('${rbEsc(panelId)}','${rbEsc(rsGear)}','${rbEsc(currentFormat)}')"
                    title="Browse to a .vst3 / .component bundle"
                    class="bg-dark-700 hover:bg-dark-600 text-gray-200 text-xs px-2 py-1 rounded">
                📁 Pick file
            </button>
            <input id="${panelId}-pathinput" type="text"
                   placeholder="or paste path…"
                   value="${rbEsc(stagedPath)}"
                   onchange="rbCatalogUpdatePathFromInput('${rbEsc(panelId)}','${rbEsc(rsGear)}', this.value)"
                   class="flex-1 bg-dark-800 border border-gray-800 rounded text-[10px] text-gray-400 px-2 py-1 font-mono">
        </div>
        <div id="${panelId}-selected" class="text-[10px] text-purple-200/80 break-all mt-1">Selected: ${rbEsc(stagedName)}</div>
        <div class="mt-2">
            <button onclick="rbCatalogAssignVst('${rbEsc(panelId)}','${rbEsc(rsGear)}')"
                    class="bg-purple-700 hover:bg-purple-600 text-white text-xs px-3 py-1.5 rounded">
                ✓ Use this plugin for ${rbEsc(rsGear)}
            </button>
            <span class="text-[10px] text-gray-500 ml-2">Per-song knob tweaks happen in the Songs editor.</span>
        </div>
        <div id="${panelId}-status" class="text-[10px] text-gray-500 mt-1"></div>`;
}

function rbCatalogStagePath(panelId, path) {
    const el = document.getElementById(panelId);
    if (el) el.dataset.stagedPath = path;
}

function rbCatalogResolveStagedPath(panelId) {
    // Resolution order, highest priority first:
    //   1. The manual path input — what the user explicitly pasted/typed
    //      ALWAYS wins over the dropdown. This is the fix for the bug
    //      where pasting a .component path got silently replaced by
    //      whatever VST happened to be selected in the scanned-plugin
    //      dropdown when the user clicked "Assign to ALL".
    //   2. dataset.stagedPath — what previous interactions parked on the
    //      panel (file-picker output, deliberate stage from another
    //      source). Used as a stable fallback when the input is empty.
    //   3. The scanned-plugin dropdown — only kicks in when neither the
    //      input nor the dataset have anything, i.e. the user hasn't
    //      touched anything manually and the dropdown is the only source.
    const input = document.getElementById(`${panelId}-pathinput`);
    if (input && input.value && input.value.trim()) return input.value.trim();
    const el = document.getElementById(panelId);
    if (el && el.dataset.stagedPath) return el.dataset.stagedPath;
    const select = document.getElementById(`${panelId}-select`);
    if (select && select.value) return select.value;
    return '';
}

// Manual path input → stage AND optionally auto-assign across all
// presets when the path looks like a real plugin (absolute path ending
// in .vst3 or .component). Mirrors rbUpdatePathFromInput in the
// per-song flow.
async function rbCatalogUpdatePathFromInput(panelId, rsGear, path) {
    rbCatalogStagePath(panelId, path);
    const sel = document.getElementById(`${panelId}-selected`);
    if (sel) {
        const name = (path || '').split('/').pop() || '(none selected)';
        sel.textContent = `Selected: ${name}`;
    }
    const looksReady = /^\/.+\.(vst3|component)$/i.test((path || '').trim());
    if (looksReady) {
        await rbCatalogAssignVst(panelId, rsGear).catch((e) =>
            console.warn('[rig_builder] catalog auto-assign from path input failed:', e));
    }
}

async function rbCatalogPickFile(panelId, rsGear, currentFormat) {
    const host = window.slopsmithDesktop;
    const statusEl = document.getElementById(`${panelId}-status`);
    const setStatus = (m) => { if (statusEl) statusEl.textContent = m; };
    if (!host || typeof host.pickFile !== 'function') {
        return alert('File picker not available on this Slopsmith build.');
    }
    try {
        const picked = await host.pickFile([
            { name: 'VST3 plugin',  extensions: ['vst3'] },
            { name: 'Audio Unit',   extensions: ['component'] },
            { name: 'All Files',    extensions: ['*'] },
        ]);
        if (!picked) return;
        const path = Array.isArray(picked) ? picked[0] : picked;
        if (!path) return;
        const el = document.getElementById(panelId);
        if (el) {
            el.dataset.stagedPath = path;
            el.innerHTML = rbRenderCatalogVstPanelBody(panelId, rsGear, path, currentFormat);
        }
        const newStatus = document.getElementById(`${panelId}-status`);
        if (newStatus) newStatus.textContent = `picked ${path.split('/').pop()}`;
    } catch (e) {
        setStatus(`pick failed: ${e.message || e}`);
    }
}

async function rbCatalogScanVsts(panelId, rsGear, curPath, curFormat) {
    const statusEl = document.getElementById(`${panelId}-status`);
    const setStatus = (msg) => { if (statusEl) statusEl.textContent = msg; };
    try {
        await rbDoVstScan(setStatus);
        const el = document.getElementById(panelId);
        if (el) el.innerHTML = rbRenderCatalogVstPanelBody(panelId, rsGear, curPath, curFormat);
        const newStatus = document.getElementById(`${panelId}-status`);
        if (newStatus) newStatus.textContent = `found ${rbState.knownVsts.length} plugins`;
    } catch (e) {
        setStatus(`scan failed: ${e.message || e}`);
    }
}

async function rbCatalogLoadAndEdit(panelId) {
    const api = window.slopsmithDesktop && window.slopsmithDesktop.audio;
    if (!api) return alert('Native VST hosting not available');
    const path = rbCatalogResolveStagedPath(panelId);
    if (!path) return alert('Pick a plugin first (📁 Pick file or dropdown)');
    const statusEl = document.getElementById(`${panelId}-status`);
    if (statusEl) statusEl.textContent = `loading ${path.split('/').pop()}…`;
    try {
        await rbTeardownVstEditor(api);
        await api.startAudio().catch(() => {});
        const slotId = await rbSafeLoadStandaloneVst(api, path);
        if (slotId == null || slotId < 0) throw new Error(rbVstRefusedMsg());
        rbState._vstEditorSlot = slotId;
        if (api.openPluginEditor) {
            await api.openPluginEditor(slotId).catch((e) => console.warn('openPluginEditor:', e));
        }
        if (statusEl) statusEl.textContent = `loaded slot ${slotId} — tweak knobs, then "Capture state" or just "Assign".`;
    } catch (e) {
        if (statusEl) statusEl.textContent = `load failed: ${rbFriendlyVstLoadError(e)}`;
    }
}

async function rbCatalogCaptureState(panelId) {
    const api = window.slopsmithDesktop && window.slopsmithDesktop.audio;
    if (!api || typeof api.savePreset !== 'function') {
        return alert('savePreset() not available');
    }
    if (rbState._vstEditorSlot == null) {
        return alert('Load the plugin first with "▶ Load & Edit".');
    }
    const statusEl = document.getElementById(`${panelId}-status`);
    if (statusEl) statusEl.textContent = 'capturing…';
    try {
        const blob = await api.savePreset();
        if (!blob) throw new Error('savePreset returned empty');
        // Stash on the panel element so Assign picks it up.
        const el = document.getElementById(panelId);
        if (el) el.dataset.pendingState = typeof blob === 'string' ? blob : JSON.stringify(blob);
        if (statusEl) statusEl.textContent = `captured (${(el?.dataset.pendingState || '').length} bytes). Click "Assign" to apply.`;
    } catch (e) {
        if (statusEl) statusEl.textContent = `capture failed: ${e.message || e}`;
    }
}

async function rbCatalogAssignVst(panelId, rsGear) {
    const path = rbCatalogResolveStagedPath(panelId);
    if (!path) return alert('Pick a plugin first (📁 Pick file or dropdown)');
    const fmt = path.toLowerCase().endsWith('.component') ? 'AudioUnit' : 'VST3';
    const el = document.getElementById(panelId);
    const pendingState = el?.dataset.pendingState || null;
    const statusEl = document.getElementById(`${panelId}-status`);
    if (statusEl) statusEl.textContent = 'applying to all presets using this gear…';
    try {
        const r = await fetch(`${window.RB_API}/vst/assign`, {
            method: 'POST', headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({
                rs_gear_type: rsGear, vst_path: path, vst_format: fmt,
                vst_state: pendingState,
            }),
        });
        if (!r.ok) {
            const err = await r.json().catch(() => ({}));
            throw new Error(err.error || r.status);
        }
        const data = await r.json();
        if (statusEl) statusEl.textContent = `✓ Assigned: ${data.pieces_updated} preset pieces across ${data.presets_updated} presets`;
        // Refresh the catalog so the card now shows the VST badge.
        setTimeout(() => rbLoadCatalog(), 600);
    } catch (e) {
        if (statusEl) statusEl.textContent = `assign failed: ${e.message || e}`;
    }
}

async function rbLoadCatalogVstSuggestions(rsGearType, panelId) {
    try {
        const r = await fetch(`${window.RB_API}/vst/suggest/${encodeURIComponent(rsGearType)}`);
        if (!r.ok) return;
        const data = await r.json();
        const suggestions = (data && data.suggestions) || [];
        if (suggestions.length === 0) return;
        const statusEl = document.getElementById(`${panelId}-status`);
        if (!statusEl) return;
        const parts = suggestions.slice(0, 3).map(s => {
            const badge = s.installed ? '✓' : '↓';
            return `${badge} ${rbEsc(s.name)}`;
        }).join(' · ');
        if (!statusEl.textContent || statusEl.textContent.length < 5) {
            statusEl.innerHTML = `Hint: ${parts}`;
        }
    } catch (_) { /* best-effort */ }
}

// Audition a VST in isolation (catalog row ▶). Mirrors rbAuditionFile but for
// stage type 0 (VST) instead of 1 (NAM) / 2 (IR).
// Direct "edit this VST" from the Gear catalog — loads the plugin into a
// throwaway slot and pops the native editor window. Saves the "📚 Library
// → re-pick the same VST → open editor" detour once a gear already has a
// VST assigned. Stops any other preview/audition first and closes any
// open VST editor window (orphaned windows are the known crash trigger).
// DIAGNOSTIC — sweep one param across [0..1] in N steps to discover the
// display→normalized mapping (especially for stepped/enum params whose
// step layout isn't documented). User invokes from DevTools console:
//   await window.rbSweepParam(slot, 'Type')             // 21 steps
//   await window.rbSweepParam(slot, 'Type', 51)         // 51 steps for fine detail
// Reads the `text` field returned by getParameters at each value — that's
// the display string the plugin uses (e.g. "Saturate" / "Hard Clip").
// Logs one line per UNIQUE display value with its normalized range.
// Use after a freshly-loaded VST so you know the slot id (returned by
// loadVST, also visible in [rig_builder restore] logs).
window.rbSweepParam = async function (slotId, paramName, steps) {
    const api = window.slopsmithDesktop && window.slopsmithDesktop.audio;
    if (!api || typeof api.setParameter !== 'function' || typeof api.getParameters !== 'function') {
        console.error('[rb-sweep] No audio API or missing setParameter / getParameters'); return;
    }
    const params = await api.getParameters(slotId);
    if (!Array.isArray(params)) { console.error('[rb-sweep] getParameters returned non-array'); return; }
    const target = params.find(p => (p.name || p.label || '').toLowerCase() === String(paramName).toLowerCase());
    if (!target) {
        console.error(`[rb-sweep] No param named "${paramName}". Available:`, params.map(p => p.name || p.label).slice(0, 30));
        return;
    }
    const pid = target.id ?? target.paramId ?? target.index;
    const N = Math.max(2, parseInt(steps || 21, 10));
    console.log(`[rb-sweep] slot=${slotId} param "${paramName}" (id=${pid}) — sweeping ${N} points from 0.0 to 1.0`);
    const rows = [];
    for (let i = 0; i < N; i++) {
        const v = i / (N - 1);
        await api.setParameter(slotId, pid, v);
        // Re-fetch the single param so `text` reflects post-set display.
        const fresh = await api.getParameters(slotId);
        const cur = fresh.find(p => (p.id ?? p.paramId ?? p.index) === pid);
        const text = cur ? (cur.text ?? cur.display ?? '<no-text>') : '<missing>';
        rows.push({ v: v.toFixed(3), text });
    }
    // Collapse adjacent rows with the same text into ranges.
    const ranges = [];
    let runStart = rows[0]; let last = rows[0];
    for (let i = 1; i < rows.length; i++) {
        if (rows[i].text !== last.text) {
            ranges.push({ from: runStart.v, to: last.v, text: last.text });
            runStart = rows[i];
        }
        last = rows[i];
    }
    ranges.push({ from: runStart.v, to: last.v, text: last.text });
    console.log(`[rb-sweep] "${paramName}" display mapping (${ranges.length} unique values):`);
    ranges.forEach(r => console.log(`    [${r.from} .. ${r.to}]  →  ${r.text}`));
    return ranges;
};

async function rbHardResetVstAudio(api) {
    try {
        if (api.setGain) {
            await api.setGain('input', 0.0);
            await api.setGain('chain', 0.0);
        }
        if (api.setMonitorMute) await api.setMonitorMute(true);
        if (api.clearChain) await api.clearChain();
        await new Promise(r => setTimeout(r, 150));
        if (api.setGain) {
            await api.setGain('input', 1.0);
            await api.setGain('chain', 0.0);
        }
    } catch (_) {}
}

async function rbResetStandaloneVstHost(api) {
    if (!api) return;

    try { if (api.closePluginEditor && rbState._vstEditorSlot != null) await api.closePluginEditor(rbState._vstEditorSlot); } catch (_) {}

    rbState._vstEditorSlot = null;
    rbState._vstEditorInChain = false;

    try {
        if (api.setGain) {
            await api.setGain('input', 0.0);
            await api.setGain('chain', 0.0);
        }
        if (api.setMonitorMute) await api.setMonitorMute(true);
        if (api.clearChain) await api.clearChain();
    } catch (_) {}

    await new Promise(r => setTimeout(r, 150));
}

async function rbRecoverFailedVstLoad(api, partialSlot = null) {
    try { if (api?.closePluginEditor && partialSlot != null) await api.closePluginEditor(partialSlot); } catch (_) {}

    try {
        if (api?.setGain) {
            await api.setGain('input', 0.0);
            await api.setGain('chain', 0.0);
        }
        if (api?.setMonitorMute) await api.setMonitorMute(true);
        if (api?.clearChain) await api.clearChain();
        if (api?.stopAudio) await api.stopAudio();
    } catch (_) {}

    rbState._vstEditorSlot = null;
    rbState._vstEditorInChain = false;

    await new Promise(r => setTimeout(r, 250));

    try {
        if (api?.setGain) {
            await api.setGain('input', 1.0);
            await api.setGain('chain', 0.0);
        }
    } catch (_) {}
}

async function rbSafeLoadStandaloneVst(api, vstPath) {
    console.log('[rig_builder vst] preparing clean host:', vstPath);

    await rbResetStandaloneVstHost(api);

    let slotId = null;

    try {
        // Route through the native-audio readiness guard so we never hand the
        // VST sandbox a sampleRate=0 device (0.3.0 audio-effects migration),
        // while keeping this wrapper's host-reset + crash recovery.
        slotId = await rbLoadVSTWhenReady(api, vstPath);
        console.log('[rig_builder vst] loadVST returned:', slotId);

        if (slotId == null || slotId < 0) {
            throw new Error(rbVstRefusedMsg());
        }

        rbState._vstEditorSlot = slotId;
        rbState._vstEditorInChain = false;

        return slotId;
    } catch (e) {
        console.warn('[rig_builder vst] load failed, recovering host:', e);
        await rbRecoverFailedVstLoad(api, slotId);
        console.log('[rig_builder vst] recovery completed');
        throw e;
    }
}

// Inline catalog editor: when we have an in-app canvas recreation of the
// plugin UI, show it right in the expanded gear card (draggable knobs →
// live setParameter) instead of popping the native window. Falls back to
// the native-window path (rbCatalogEditVst) for plugins without a canvas.
function rbCatalogSavedParamsForGear(rsGear) {
    const g = rbFindGear(rsGear);
    if (!g || g.category !== 'amp') return null;
    return g && g.vst_state ? rbParseVstStateParams(g.vst_state) : null;
}

async function rbApplyCatalogGearVstParams(api, slotId, vstPath, rsGear) {
    const saved = rbCatalogSavedParamsForGear(rsGear);
    if (saved && Object.keys(saved).length) {
        return rbRestoreSavedParamsToSlot(api, slotId, saved, vstPath);
    }

    if (!rsGear) return null;

    const vstStem = vstPath.split('/').pop()
        .replace(/\.(vst3|component)$/i, '')
        .toLowerCase();

    try {
        const r = await fetch(`${window.RB_API}/vst/knob_mapping?rs_gear_type=${encodeURIComponent(rsGear)}&vst_name=${encodeURIComponent(vstStem)}`);
        const data = await r.json();
        const staticBlock = data && data.mapping && data.mapping._static;

        if (staticBlock && typeof staticBlock === 'object') {
            return rbRestoreSavedParamsToSlot(api, slotId, staticBlock, vstPath);
        }
    } catch (e) {
        console.warn('[rig_builder catalog-edit] default param apply skipped:', e);
    }

    return null;
}

let _rbStandaloneVstLoadSeq = 0;

function rbStandaloneVstLoadToken() {
    _rbStandaloneVstLoadSeq += 1;
    return _rbStandaloneVstLoadSeq;
}

function rbStandaloneVstLoadActive(token) {
    return token === _rbStandaloneVstLoadSeq;
}

async function rbQuarantineFailedStandaloneVst(api, token) {
    if (token != null && !rbStandaloneVstLoadActive(token)) return;
    try {
        if (api && api.setGain) {
            await api.setGain('chain', 0.0);
            await api.setGain('input', 1.0);
        }
        if (api && api.setMonitorMute) await api.setMonitorMute(true);
        if (api && api.clearChain) await api.clearChain();
        if (api && api.stopAudio) await api.stopAudio();
    } catch (_) {}
}

async function rbMakeStandaloneVstAudible(api, opts) {
    if (!api) return;

    const noUnmute = !!(opts && opts.noUnmute);

    // Feed input but keep the wet path (chain) MUTED while we start audio, so
    // the freshly-loaded plugin settles its startup state (tube DC blocker,
    // filter ringing) on real input WITHOUT us hearing the transient ("pop").
    try { if (api.setGain) await api.setGain('input', 1.0); } catch (_) {}
    try { if (api.startAudio) await api.startAudio(); } catch (_) {}

    if (noUnmute) return;

    // Settle window: the tube DC blocker has a ~26 ms time-constant; give a
    // generous margin so even the heavier amps stabilise before we open up.
    // Override with window.__rbVstSettleMs.
    const settleMs = (typeof window.__rbVstSettleMs === 'number')
        ? Math.max(0, window.__rbVstSettleMs | 0) : 80;
    await new Promise(r => setTimeout(r, settleMs));

    try { if (api.setMonitorMute) await api.setMonitorMute(false); } catch (_) {}

    // Fade the wet path 0 → 1.0 in a few steps so opening it doesn't click.
    try {
        if (api.setGain) {
            for (const v of [0.25, 0.5, 0.8, 1.0]) {
                await api.setGain('chain', v);
                await new Promise(r => setTimeout(r, 6));
            }
        }
    } catch (_) {}
}

async function rbCatalogEditInline(safeId, vstPath, vstFormat, rsGear, stem) {
    const el = document.getElementById(`rb-cat-edit-${safeId}`);
    if (!el) return rbCatalogEditVst(vstPath, vstFormat, rsGear);
    let waitedForCanvas = false;
    if (!window.RBPedalCanvas) {
        waitedForCanvas = true;
        el.classList.remove('hidden');
        el.innerHTML = `<div class="text-xs text-gray-500">loading VST UI…</div>`;
        for (let i = 0; i < 30 && !window.RBPedalCanvas; i++) {
            await new Promise(r => setTimeout(r, 50));
        }
    }
    if (!window.RBPedalCanvas) return rbCatalogEditVst(vstPath, vstFormat, rsGear);
    const api = rbNativeAudio();
    if (!api || typeof api.loadVST !== 'function') return alert('Native VST hosting not available.');
    // Toggle close.
    if (!waitedForCanvas && !el.classList.contains('hidden')) {
        el.classList.add('hidden');
        el.innerHTML = '';
        rbStandaloneVstLoadToken();
        await rbTeardownVstEditor(api).catch(() => {});
        try { if (api.setMonitorMute) await api.setMonitorMute(true); } catch (_) {}
        return;
    }
    // Mutual exclusivity with the other sub-panels.
    document.getElementById(`rb-cat-lib-${safeId}`)?.classList.add('hidden');
    document.getElementById(`rb-cat-variants-${safeId}`)?.classList.add('hidden');
    el.classList.remove('hidden');
    el.innerHTML = `<div class="text-xs text-gray-500">loading ${rbEsc(vstPath.split('/').pop())}…</div>`;
    const loadToken = rbStandaloneVstLoadToken();
    try {
        await rbCloseActiveVstEditor().catch(() => {});
        if (rbState.listeningTone !== null || rbState._auditionId) {
            await rbStopPreview().catch(() => {});
        }

        const slotId = await rbSafeLoadStandaloneVst(api, vstPath);

        if (!rbStandaloneVstLoadActive(loadToken)) return;

        const baseStage = {
            type: 0,
            slot: /^Amp_|^Bass_Amp_|^DI_Amp_/.test(String(rsGear || '')) ? 'amp' : 'pedal',
            path: vstPath,
            format: vstFormat || 'VST3',
            rs_gear: rsGear || '',
            bypassed: false,
        };

        await rbApplyCatalogGearVstParams(api, slotId, vstPath, rsGear);

        const chainForLeveling = await rbAppendFinalLevelerToStandaloneVstChain(api, baseStage);

        await rbMakeStandaloneVstAudible(api);
        await rbStartFinalChainNormalizer(chainForLeveling);

    
        setTimeout(() => rbSignalChainLoaded().catch(() => {}), 250);
        // Snapshot current params → canvas model (logical values + idMap).
        let model = { values: {}, idMap: {}, logicalParams: [] };
        try {
            const raw = (typeof api.getParameters === 'function' ? await api.getParameters(slotId) : []) || [];
            model = rbBuildCanvasModel(raw, null);
        } catch (_) {}
        // No faithful in-app canvas recreation → open the plugin's OWN native
        // window (the real UI) instead of generic synthesized sliders. rbCatalog
        // EditVst falls back to a message only if the plugin is also UI-less.
        if (!window.RBPedalCanvas.has(stem)) {
            el.classList.add('hidden'); el.innerHTML = '';
            return rbCatalogEditVst(vstPath, vstFormat, rsGear);
        }
        el.innerHTML = `
            <div class="flex items-center justify-between mb-1">
                <div class="text-[11px] text-purple-300 font-semibold">In-Slopsmith editor · ${rbEsc(vstPath.split('/').pop().replace(/\.(vst3|component)$/i, ''))}</div>
                <button onclick="event.stopPropagation(); rbCatalogEditInline('${safeId}','${rbEsc(vstPath).replace(/'/g,"\\'")}','${rbEsc(vstFormat)}','${rbEsc(rsGear)}','${stem}')"
                        title="Close inline editor" class="text-[10px] text-gray-400 hover:text-gray-200 px-1">✕</button>
            </div>
            <div class="flex justify-center">
                <canvas id="rb-cat-canvas-${safeId}" style="width:${rbCanvasDisplayWidth(stem)}px;max-width:100%;cursor:ns-resize;touch-action:none"></canvas>
            </div>
            <div class="text-[10px] text-gray-500 text-center mt-1">Drag a knob up/down · then 📚 Library → Assign to save</div>`;
        const canvas = document.getElementById(`rb-cat-canvas-${safeId}`);
        const draw = () => window.RBPedalCanvas.attach(canvas, stem, {
            values: model.values, params: model.logicalParams, interactive: true,
            onChange: (logicalId, val) => { const realId = model.idMap[logicalId] ?? logicalId;
                try { api.setParameter(slotId, realId, val); } catch (_) {} },
        });
        if (window.RBPedalCanvas.ready) window.RBPedalCanvas.ready().then(draw);
        draw();
    } catch (e) {
        await rbQuarantineFailedStandaloneVst(api, loadToken);
        if (rbStandaloneVstLoadActive(loadToken)) {
            el.innerHTML = `<div class="text-xs text-red-400">load failed: ${rbEsc(rbFriendlyVstLoadError(e))}</div>`;
        }
    }
}

async function rbCatalogEditVst(vstPath, vstFormat, rsGear) {
    const api = rbNativeAudio();
    if (!api || typeof api.loadVST !== 'function') {
        alert('Native VST hosting not available.');
        return;
    }
    const loadToken = rbStandaloneVstLoadToken();
    try {
        await rbCloseActiveVstEditor();
        if (rbState.listeningTone !== null || rbState._auditionId) {
            await rbStopPreview();
        }
        if (api.clearChain) await api.clearChain().catch(() => {});
        const slotId = await rbSafeLoadStandaloneVst(api, vstPath);

        if (!rbStandaloneVstLoadActive(loadToken)) return;

        if (slotId == null || slotId < 0) {
            await rbQuarantineFailedStandaloneVst(api, loadToken);
            throw new Error(rbVstRefusedMsg());
        }
        rbState._vstEditorSlot = slotId;
        const baseStage = {
            type: 0,
            slot: /^Amp_|^Bass_Amp_|^DI_Amp_/.test(String(rsGear || '')) ? 'amp' : 'pedal',
            path: vstPath,
            format: vstFormat || 'VST3',
            rs_gear: rsGear || '',
            bypassed: false,
        };

        await rbApplyCatalogGearVstParams(api, slotId, vstPath, rsGear);

        const chainForLeveling = await rbAppendFinalLevelerToStandaloneVstChain(api, baseStage);

        await rbMakeStandaloneVstAudible(api);
        await rbStartFinalChainNormalizer(chainForLeveling);
        if (api.openPluginEditor) {
            await api.openPluginEditor(slotId).catch((e) => {
                console.warn('[rig_builder] openPluginEditor failed:', e);
                alert(`Couldn't open editor for this plugin (the native window may have crashed). Plugin is loaded — try again or use the inline editor in a song's slot.`);
            });
        } else {
            alert('This Slopsmith build has no openPluginEditor API.');
        }
    } catch (e) {
        await rbQuarantineFailedStandaloneVst(api, loadToken);
        if (rbStandaloneVstLoadActive(loadToken)) {
            alert(`load failed: ${rbFriendlyVstLoadError(e)}`);
        }
    }
}

function rbRestoreAuditionButton(btn) {
    if (!btn) return;
    btn.disabled = false;
    btn.textContent = btn.dataset.origLabel || '▶';
}

async function rbAuditionVst(vstPath, vstFormat, btnId, rsGear) {
    const api = rbNativeAudio();
    if (!api) return;
    const btn = document.getElementById(btnId);
    // Toggle off if already auditioning this one.
    if (rbState._auditionId === btnId) {
        await rbStopPreview();
        return;   // rbStopPreview restores btn.dataset.origLabel
    }
    if (rbState.listeningTone !== null || rbState._auditionId) {
        await rbStopPreview();
    }
    const loadToken = rbStandaloneVstLoadToken();
    // Stash the original button text so stop/swap can restore it.
    if (btn && !btn.dataset.origLabel) btn.dataset.origLabel = btn.textContent;
    try {
        if (btn) { btn.disabled = true; btn.textContent = '…'; }
        // Close any open VST editor window BEFORE touching the chain — an
        // orphaned editor pointing at the slot we're about to wipe is the
        // known crash trigger on consecutive ▶ Audition clicks.
        await rbCloseActiveVstEditor();
        if (api.clearChain) await api.clearChain().catch(() => {});
        // Use api.loadVST directly (same path as rbCatalogEditVst) instead of
        // /native_preset_one + loadPreset. The loadPreset path was crashing
        // on rapid sequential ▶ clicks between two VST pedals (the engine
        // appears to mishandle the residual VST stage when the new chain is
        // pushed before the old one fully unloads). loadVST is a one-shot
        // single-stage path that doesn't have that race.
        if (typeof api.loadVST !== 'function') {
            throw new Error('engine has no loadVST API (WASM-only build?)');
        }
        const slotId = await rbSafeLoadStandaloneVst(api, vstPath);
        if (!rbStandaloneVstLoadActive(loadToken)) {
            rbRestoreAuditionButton(btn);
            return;
        }
        if (slotId == null || slotId < 0) throw new Error(rbVstRefusedMsg());
        const baseStage = {
            type: 0,
            slot: /^Amp_|^Bass_Amp_|^DI_Amp_/.test(String(rsGear || '')) ? 'amp' : 'pedal',
            path: vstPath,
            format: vstFormat || 'VST3',
            rs_gear: rsGear || '',
            bypassed: false,
        };

        await rbApplyCatalogGearVstParams(api, slotId, vstPath, rsGear);

        // Agrega RB Final Leveler después del VST que estás escuchando.
        const chainForLeveling = await rbAppendFinalLevelerToStandaloneVstChain(api, baseStage);

        const wasRunning = api.isAudioRunning ? await api.isAudioRunning().catch(() => true) : true;
        await rbMakeStandaloneVstAudible(api);

        await rbStartFinalChainNormalizer(chainForLeveling);

        rbState._previewStartedAudio = !wasRunning;
        rbState._previewMode = 'native';
        rbState._auditionId = btnId;
        if (btn) {
            btn.disabled = false;
            const orig = btn.dataset.origLabel || '';
            const labelTail = orig.replace(/^\s*▶\s*/, '');
            btn.textContent = labelTail ? `⏸ ${labelTail}` : '⏸';
        }
    } catch (e) {
        await rbQuarantineFailedStandaloneVst(api, loadToken);
        rbRestoreAuditionButton(btn);
        if (rbStandaloneVstLoadActive(loadToken)) alert(`Audition failed: ${rbFriendlyVstLoadError(e)}`);
    }
}

// Preview a tone LIVE through the full chain. Persists the selection, then
// asks the backend for a native_preset containing EVERY NAM stage (pedal →
// amp → …) + the cab IR, and loads it straight into the native engine — so
// this both *tests* and *realises* multi-NAM playback without touching the
// app bundle. The engine's `slotsLoaded` (logged to the console) tells us
// how many stages it actually accepted. If there's no native engine
// (WASM-only), it falls back to nam_tone's single-NAM preview.
async function rbListenTone(toneIdx, filename) {
    const btn = document.getElementById(`rb-listen-${toneIdx}`);

    // Toggle off if this tone is already previewing.
    if (rbState.listeningTone === toneIdx) {
        await rbStopPreview();
        if (btn) btn.textContent = '▶ Listen';
        return;
    }
    // Stop a different tone's preview first.
    if (rbState.listeningTone !== null) {
        const prev = document.getElementById(`rb-listen-${rbState.listeningTone}`);
        await rbStopPreview();
        if (prev) prev.textContent = '▶ Listen';
    }

    // First-listen path skips rbStopPreview, so close any open VST editor here
    // too before the clearChain below (avoids the orphaned-window crash).
    await rbCloseActiveVstEditor();

    if (btn) { btn.disabled = true; btn.textContent = '⏳ Loading…'; }
    const presetId = await rbPersistTone(toneIdx, filename);
    if (presetId === null) { if (btn) { btn.disabled = false; btn.textContent = '▶ Listen'; } return; }

    const api = rbNativeAudio();
    try {
        if (api) {
            const payload = await (await fetch(`${window.RB_API}/native_preset_full/${presetId}`)).json();
            const chain = (payload.native_preset && payload.native_preset.chain) || [];
            if (chain.length === 0) {
                alert('This tone has no pieces with an assigned file yet.');
                if (btn) { btn.disabled = false; btn.textContent = '▶ Listen'; }
                return;
            }
            rbState._previewPayload = payload;
            rbApplyBypassToChain(payload, toneIdx);   // honour any pre-set bypasses
            // AWAIT pre-load mute so chain gain is at 0 before clearChain+
            // loadPreset run. Target gain is computed from the chain
            // (amp+cab → ×2.0, amp only → ×0.5) so Listen mode normalises
            // levels the same way the song-playback path does.
            await rbPreLoadMute(chain.length, rbChainGainTargetFor(chain)).catch(() => {});
            const loaded = await rbLoadNativePresetPayload(api, payload, {
                mode: 'preview',
                ref: presetId,
                authorization: 'user-action',
            });
            const res = loaded.result;
            const got = res && res.slotsLoaded;
            console.log(`[rig_builder] chain sent=${chain.length} (NAM=${payload.nam_stage_count}) · slotsLoaded=${got}`, res, loaded.viaAudioEffects ? '(audio-effects executor)' : '(legacy loadPreset)');
            await rbSyncAudioEffectsCapability('listen-tone', { chain, mode: 'preview', userAction: true });
            // Force bypass to match the spec: engine sometimes keeps a slot
            // bypassed across reloads (the "bypass stuck" Discord report).
            await rbReapplyBypassToChain(api, chain);
            // Re-apply persisted VST params: the chain JSON's `state` field
            // for type 0 stages doesn't reliably restore plugin params in
            // every engine build, so we walk the loaded chain and call
            // setParameter for each saved {paramId: value} entry.
            await rbReapplyVstParamsToChain(api, chain).catch((e) =>
                console.warn('[rig_builder] re-apply VST params:', e));
            // Input gain to chain-input-drive (pre-chain, safe to set).
            // Don't touch chain gain or monitor mute — rbPreLoadMute fades
            // chain back to its target and un-mutes on its own timer
            // with a smooth ramp. Forcing them here defeats the fade.
            if (api.setGain) {
                await rbApplyChainInputDrive({ chain });
                await rbStartFinalChainNormalizer(chain);
            }
            const wasRunning = api.isAudioRunning ? await api.isAudioRunning().catch(() => true) : true;
            await api.startAudio();
            rbState._previewStartedAudio = !wasRunning;
            rbState._previewMode = 'native';
            rbState.listeningTone = toneIdx;
            if (btn) {
                btn.disabled = false;
                btn.textContent = '⏸ Stop';
                btn.title = `Chain: ${chain.length} stages (NAM=${payload.nam_stage_count}); engine loaded ${got}`;
            }
            if (payload.nam_stage_count >= 2 && typeof got === 'number' && got < chain.length) {
                console.warn(`[rig_builder] engine loaded ${got}/${chain.length} stages → it does not chain all NAMs`);
            }
        } else if (typeof window.namStartPresetTest === 'function') {
            await window.namStartPresetTest(presetId);   // WASM fallback: single NAM
            await rbSyncAudioEffectsCapability('listen-tone-fallback', { chain: [], mode: 'wasm-fallback', fallback: true });
            rbState._previewMode = 'nam';
            rbState.listeningTone = toneIdx;
            if (btn) { btn.disabled = false; btn.textContent = '⏸ Stop'; btn.title = '1-NAM preview (WASM engine, no chaining)'; }
        } else {
            alert('Audio engine unavailable. Open the “NAM” plugin once to initialize it.');
            if (btn) { btn.disabled = false; btn.textContent = '▶ Listen'; }
        }
    } catch (e) {
        await rbStopPreview();
        if (btn) { btn.disabled = false; btn.textContent = '▶ Listen'; }
        alert(`Could not play: ${e && e.message ? e.message : e}`);
    }
}

// ── Settings ───────────────────────────────────────────────────────

async function rbLoadSettings() {
    let s;
    try {
        const r = await fetch(`${window.RB_API}/settings`);
        s = await r.json();
    } catch (e) {
        return;
    }
    const megaCb = document.getElementById('rb-mega-chain-mode');
    if (megaCb) megaCb.checked = !!s.mega_chain_mode;
    // Inverted-sense checkbox: the user opts OUT of curated-only by
    // ticking the box (= allow tone3000 fuzzy fallback). The persisted
    // setting is still `curated_only`; the UI just shows the opposite.
    const allowFuzzy = document.getElementById('rb-allow-tone3000-fallback');
    if (allowFuzzy) allowFuzzy.checked = !s.curated_only;
    // "Bypass all the game cabs" — reflects the persisted setting; toggling
    // it POSTs to /settings which bulk-flips preset_pieces.bypassed for cabs.
    const bypassCabs = document.getElementById('rb-bypass-all-cabs');
    if (bypassCabs) bypassCabs.checked = !!s.bypass_all_cabs;
    // Mirror the persisted flag onto the runtime mirror so RbMegaChain
    // sees it even if the user never opens Settings. rbLoadSettings is
    // called from rbInit so this runs at page-load.
    window.__rbMegaChainSetting = !!s.mega_chain_mode;
    // Refresh the chain-input drive cache too — picks up any change the
    // user made via Settings (or via a direct settings POST in DevTools).
    if (typeof s.nam_chain_input_drive === 'number') {
        window.__rbChainInputDrive = s.nam_chain_input_drive;
    }
    // Input chain (amp drive) knob — range 0–5, default 1×.
    const adv = (typeof s.nam_chain_input_drive === 'number') ? s.nam_chain_input_drive : 1.0;
    if (!window.__rbAmpDriveKnob)
        window.__rbAmpDriveKnob = rbAttachKnob('rb-amp-drive-knob', { min: 0, max: 5, def: 1, value: adv, onChange: rbSetAmpDrive });
    else window.__rbAmpDriveKnob.set(adv);
    const adVal = document.getElementById('rb-amp-drive-val');
    if (adVal) adVal.textContent = adv.toFixed(1) + '×';
    // Output chain (chain volume) knob — range 0–5, default 1×.
    window.__rbChainMakeup = (typeof s.chain_makeup === 'number') ? s.chain_makeup : 1.0;
    if (!window.__rbChainMakeupKnob)
        window.__rbChainMakeupKnob = rbAttachKnob('rb-chain-makeup-knob', { min: 0, max: 5, def: 1, value: window.__rbChainMakeup, onChange: rbSetChainMakeup });
    else window.__rbChainMakeupKnob.set(window.__rbChainMakeup);
    const cmVal = document.getElementById('rb-chain-makeup-val');
    if (cmVal) cmVal.textContent = window.__rbChainMakeup.toFixed(1) + '×';
    // OAuth (Connect with tone3000) state.
    const oauthStatus = document.getElementById('rb-oauth-status');
    const oauthBtn = document.getElementById('rb-oauth-btn');
    const oauthDisc = document.getElementById('rb-oauth-disconnect');
    if (s.tone3000_connected) {
        if (oauthStatus) oauthStatus.innerHTML = `<span class="text-green-400">Connected${s.tone3000_username ? ' as ' + rbEsc(s.tone3000_username) : ''}</span>`;
        if (oauthBtn) oauthBtn.textContent = 'Reconnect';
        if (oauthDisc) oauthDisc.classList.remove('hidden');
    } else {
        if (oauthStatus) oauthStatus.textContent = 'Not connected.';
        if (oauthBtn) oauthBtn.textContent = 'Connect with tone3000';
        if (oauthDisc) oauthDisc.classList.add('hidden');
    }
}

// ── OAuth: Connect with tone3000 ────────────────────────────────────
// Opens the authorize URL in the system browser (the host's nav guard
// re-routes external URLs there), then polls until the backend has
// exchanged the code for tokens.

async function rbOauthConnect() {
    const statusEl = document.getElementById('rb-oauth-status');
    try {
        const origin = window.location.origin;
        const r = await fetch(`${window.RB_API}/oauth/start?origin=${encodeURIComponent(origin)}`);
        const d = await r.json();
        if (!d.authorize_url) throw new Error('no authorize URL');
        window.open(d.authorize_url, '_blank');  // → system browser
        rbRecordPrivilegedOutcome('service.request', 'handled', 'Started tone3000 OAuth sign-in');
        if (statusEl) statusEl.textContent = 'Waiting for tone3000 sign-in in your browser…';
        rbOauthPoll(0);
    } catch (e) {
        if (statusEl) statusEl.textContent = 'Could not start sign-in: ' + (e.message || e);
    }
}

async function rbOauthPoll(n) {
    if (n > 90) {  // ~3 min, then give up quietly
        const statusEl = document.getElementById('rb-oauth-status');
        if (statusEl && statusEl.textContent.startsWith('Waiting')) {
            statusEl.textContent = 'Still not connected. Finish sign-in in your browser, or click Connect again.';
        }
        return;
    }
    try {
        const r = await fetch(`${window.RB_API}/oauth/status`);
        const d = await r.json();
        if (d.connected) {
            rbLoadSettings();
            rbInit();  // refresh status banner
            return;
        }
    } catch (e) { /* keep polling */ }
    setTimeout(() => rbOauthPoll(n + 1), 2000);
}

async function rbOauthDisconnect() {
    await fetch(`${RB_API}/oauth/disconnect`, { method: 'POST' });
    rbRecordPrivilegedOutcome('service.request', 'completed', 'Disconnected tone3000 account');
    rbLoadSettings();
    rbInit();
}

async function rbSaveSettings() {
    const megaCb = document.getElementById('rb-mega-chain-mode');
    const mega_chain_mode = megaCb ? !!megaCb.checked : false;
    await fetch(`${window.RB_API}/settings`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ mega_chain_mode }),
    });
    // Mirror to the runtime so RbMegaChain picks it up without a restart.
    window.__rbMegaChainSetting = mega_chain_mode;
}

// Opt-out toggle for the curated-only flow. The checkbox shows the
// INVERSE of the persisted `curated_only` setting:
//   - unchecked → curated_only = true  (default, recommended)
//   - checked   → curated_only = false (allow tone3000 fuzzy fallback)
// Persists immediately so the next Scan / song-open honours the
// new value.
async function rbSetAllowTone3000Fallback(checked) {
    try {
        await fetch(`${window.RB_API}/settings`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ curated_only: !checked }),
        });
    } catch (e) { /* best-effort */ }
}

// "Bypass all the game cabs" toggle. Posting bypass_all_cabs to /settings
// bulk-flips preset_pieces.bypassed for every cabinet stage (backend side-
// effect), so every tone skips its RS cab and the user can add their own.
async function rbSetBypassAllCabs(checked) {
    const status = document.getElementById('rb-bypass-all-cabs-status');
    if (status) status.textContent = checked ? 'Bypassing every cab…' : 'Re-enabling cabs…';
    try {
        const r = await fetch(`${window.RB_API}/settings`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ bypass_all_cabs: !!checked }),
        });
        if (!r.ok) throw new Error('save failed');
        if (status) status.textContent = checked
            ? '✓ All cabs bypassed. Add your own cab/IR per tone; reopen the song to hear it.'
            : '✓ Cabs re-enabled.';
    } catch (e) {
        if (status) status.textContent = '⚠ Could not save — try again.';
    }
}

// Triggered from the Suggest modal: download a specific tone3000
// capture for an rs_gear, then update any open per-song chain so
// "Save preset" picks the new file up without a re-fetch.
async function rbDownloadForGear(btn, rsGear, toneId) {
    btn.disabled = true;
    btn.textContent = 'Downloading…';
    // Downloading from tone3000 can take a while (and ffmpeg-normalizes
    // IRs server-side), but bound it so a stalled CDN connection turns
    // into a visible error instead of a button stuck on "Downloading…".
    const ctrl = new AbortController();
    const timer = setTimeout(() => ctrl.abort(), 180000);
    let jobId = null;
    try {
        jobId = await rbStartCapabilityJob('rig-builder.download-capture', 'Download and assign tone3000 capture', {
            logicalJobKey: `rig-builder.download-capture-${Date.now()}`,
            targetKind: 'tone3000-capture',
            targetRef: 'capture-assignment',
        });
        const r = await fetch(`${RB_API}/download_for_gear`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ rs_gear: rsGear, tone3000_id: toneId }),
            signal: ctrl.signal,
        });
        const data = await r.json();
        if (!r.ok) {
            rbFinishCapabilityJob(jobId, false, data.error || `HTTP ${r.status}`, 'external-dependency');
            btn.textContent = 'Failed';
            btn.classList.add('bg-red-700');
            alert(data.error || `HTTP ${r.status}`);
            return;
        }
        const assignedNote = data.presets_updated
            ? ` (${data.presets_updated} preset${data.presets_updated === 1 ? '' : 's'})`
            : '';
        btn.textContent = `✓ assigned${assignedNote}`;
        rbFinishCapabilityJob(jobId, true, `Assigned capture to ${data.presets_updated || 0} preset rows`);
        btn.classList.remove('bg-green-700', 'hover:bg-green-600');
        btn.classList.add('bg-dark-600');
        // If the song view is open and any piece matches this rs_gear,
        // stamp the downloaded file in so "Save preset" persists it.
        if (rbState.songTones) {
            for (const t of rbState.songTones.tones) {
                for (const p of t.chain) {
                    if (p.type === rsGear) {
                        p._uploaded_file = data.file;
                        p._uploaded_kind = data.kind;
                    }
                }
            }
        }
        // The backend already stamped the file onto pending preset_pieces
        // and refreshed the affected presets, so refresh whichever view
        // is open to reflect that the gear is no longer pending. Post-
        // restructure the dashboard + pending tabs are gone; Setup now
        // owns coverage, and Gear owns the pending sub-view.
        if (rbState.currentTab === 'settings') rbLoadCoverage();
        else if (rbState.currentTab === 'gear') rbGearFilter(rbState.currentGearFilter || 'all');
        // Reflect the new assignment in the open song view now (and
        // re-audition if a tone using this gear is currently previewing) —
        // no need to re-select the song.
        rbAfterGearChange(null);
    } catch (e) {
        rbFinishCapabilityJob(jobId, false, e.name === 'AbortError' ? 'Download timed out' : (e.message || e), e.name === 'AbortError' ? 'timeout' : 'external-dependency');
        btn.textContent = e.name === 'AbortError' ? 'Timed out' : 'Error';
        btn.classList.add('bg-red-700');
        alert(e.name === 'AbortError'
            ? 'Download timed out after 3 min — tone3000 may be slow or the model URL is unreachable.'
            : e.message);
    } finally {
        clearTimeout(timer);
        btn.disabled = false;
    }
}

async function rbExportDefaults() {
    const status = document.getElementById('rb-export-defaults-status');
    status.textContent = 'Exporting…';
    let jobId = null;
    try {
        jobId = await rbStartCapabilityJob('rig-builder.export-defaults', 'Export Rig Builder curated defaults', {
            logicalJobKey: 'rig-builder.export-defaults',
            targetKind: 'curated-defaults',
            targetRef: 'default-captures',
        });
        const r = await fetch(`${RB_API}/export_default_captures`, { method: 'POST' });
        const data = await r.json();
        if (!r.ok) {
            rbFinishCapabilityJob(jobId, false, data.error || `HTTP ${r.status}`, 'storage');
            status.innerHTML = `<span class="text-red-400">Error: ${rbEsc(data.error || r.status)}</span>`;
            return;
        }
        status.innerHTML = `<span class="text-green-400">Saved ${data.count} gear → capture defaults to default_captures.json.</span>`;
        rbFinishCapabilityJob(jobId, true, `Exported ${data.count || 0} default captures`);
    } catch (e) {
        status.innerHTML = `<span class="text-red-400">${rbEsc(e.message)}</span>`;
    }
}

// (rbRemapCabMics removed — _auto_fix_cab_mics_for_song now runs on
// every /song fetch, so cab assignments self-heal at song-open time
// without the user needing to visit Setup.)
