// NAM Rig Builder plugin — Rocksmith tone → NAM preset mapping UI.

(function () {
    // Idempotency: showScreen is wrapped at most once even if screen.js
    // is re-evaluated by the host. Without this guard, each re-eval
    // captures the previous wrapper and we leak closures + run rbInit
    // multiple times per navigation.
    const HOOK_KEY = '__slopsmithRigBuilderInstalled';
    if (window[HOOK_KEY]) return;
    window[HOOK_KEY] = true;

    const origShowScreen = window.showScreen;
    if (typeof origShowScreen === 'function') {
        window.showScreen = function (id) {
            origShowScreen(id);
            if (id === 'plugin-rig_builder') {
                rbInit();
            } else {
                // Leaving NAM Rig Builder: close any open native VST editor
                // window (so the next screen's loadPreset can't crash the host
                // by clearing its slot) and stop any live preview/audition.
                rbOnLeaveRigBuilder();
            }
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
                setTimeout(() => { rbApplyChainInputDrive(); }, reapplyDelay);
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
// BASS handling: tone3000 bass captures (Gallien-Krueger G1.0/G3.0/G5.0,
// CS75B, Bassman, etc.) are typically authored at clean gain settings,
// so the guitar-amp 8× drive over-saturates them — the model is being
// fed +18 dB beyond its capture-time operating point. For bass songs
// (4-string arrangement) or auditioning a Bass_* gear, we use unity
// (1.0) drive instead. Detection sources, in priority:
//   1. opts.isBass: explicit override (audition path passes this based
//      on rs_gear, which the gear catalog has on hand)
//   2. window.highway.getStringCount() <= 4 (song-playback path; the
//      bundle publishes string count from the song_info payload)
//   3. fall through to guitar drive
//
// The engine resets input gain to 1.0 on every chain reload, so we
// have to re-apply after each loadPreset. Hooks:
//   - fetch interceptor (bundle's chain load)
//   - mega-chain build (initial preload at song start)
//   - rbListenTone (Listen ▶ in per-song view)
//   - rbAuditionFile (▶ in Gear catalog)
function rbApplyChainInputDrive(opts) {
    const audio = window.slopsmithDesktop && window.slopsmithDesktop.audio;
    if (!audio || typeof audio.setGain !== 'function') return;
    let isBass = (opts && opts.isBass === true);
    if (!isBass && !(opts && opts.isBass === false)) {
        try {
            const hw = window.highway;
            const sc = hw && typeof hw.getStringCount === 'function'
                ? hw.getStringCount() : null;
            if (typeof sc === 'number' && sc > 0 && sc <= 4) isBass = true;
        } catch (_) {}
    }
    let drive;
    if (isBass) {
        drive = 1.0;
    } else {
        drive = (typeof window.__rbChainInputDrive === 'number' && window.__rbChainInputDrive > 0)
            ? window.__rbChainInputDrive : 8.0;
    }
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
    const calledExplicitly = opts && (opts.isBass === true || opts.isBass === false);
    if (!calledExplicitly && !opts?._isRepoll) {
        setTimeout(() => rbApplyChainInputDrive({ _isRepoll: true }), 1500);
        setTimeout(() => rbApplyChainInputDrive({ _isRepoll: true }), 3500);
    }
    return audio.setGain('input', drive).catch((e) => {
        console.warn('[rig_builder] setGain(input,', drive, ') failed:', e);
    });
}

// Compute the chain-gain target for a given chain spec: looks at what's
// actually active to estimate how much output level the chain will
// produce, and returns a multiplier that brings it to a perceived-flat
// level. Solves the "amp raw is loud / amp through cab is quiet"
// asymmetry without a slider — the caller passes this to rbPreLoadMute
// so the fade-in lands at the right level for whatever this chain has.
//
//   active amp + active cab IR → ×2.0 (compensate cab attenuation)
//   active amp + no cab IR     → ×0.5 (knock the raw-amp spike down)
//   no active amp / fallback   → ×1.0 (don't change anything)
function rbChainGainTargetFor(chainSpec) {
    // User "Chain volume" trim (chain_makeup, default 1.0) — the ONLY level
    // the engine respects (per-stage IR gain is ignored). Multiplies the
    // auto-leveled base below.
    const makeup = (typeof window.__rbChainMakeup === 'number') ? window.__rbChainMakeup : 4.0;
    let base = 1.0;
    if (Array.isArray(chainSpec)) {
        let hasActiveAmp = false, hasActiveCab = false, activeNamCount = 0;
        for (const stage of chainSpec) {
            if (!stage || stage.bypassed) continue;
            if (stage.type === 1) {
                activeNamCount++;
                if (stage.slot === 'amp') hasActiveAmp = true;
            }
            // type 2 = IR; ANY active IR counts as a cab (rs_ir master_pre etc.).
            if (stage.type === 2) hasActiveCab = true;
        }
        // Auto makeup (dB): +6 if a cab IR is active, -6 if amp-only; +2 per
        // extra NAM beyond the first; capped at +18. (Each NAM loses ~2-3 dB;
        // the cab IR ~6 dB.) Only when an amp is active — otherwise leave at 1.
        if (hasActiveAmp) {
            let dB = (hasActiveCab ? 6 : -6) + 2 * Math.max(0, activeNamCount - 1);
            dB = Math.max(-12, Math.min(18, dB));
            base = Math.pow(10, dB / 20);
        }
    }
    window.__rbChainBaseTarget = base;   // remember (pre-trim) for live makeup changes
    return base * makeup;
}

// User cab/chain volume trim. Persists to /settings and applies LIVE via
// setGain('chain', base × trim) — the only gain the engine honours.
async function rbSetChainMakeup(v) {
    const val = Math.max(0.1, Math.min(8.0, parseFloat(v) || 1.0));
    window.__rbChainMakeup = val;
    const cmVal = document.getElementById('rb-chain-makeup-val');
    if (cmVal) cmVal.textContent = val.toFixed(2) + '×';
    const audio = window.slopsmithDesktop && window.slopsmithDesktop.audio;
    if (audio && typeof audio.setGain === 'function') {
        const base = (typeof window.__rbChainBaseTarget === 'number') ? window.__rbChainBaseTarget : 1.0;
        audio.setGain('chain', base * val).catch(() => {});
    }
    fetch(`${RB_API}/settings`, {
        method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ chain_makeup: val }),
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
async function rbPreLoadMute(chainLen, targetGain) {
    if (window.__rbMutePreLoad === false) return;
    if (_rbMuteInFlight) return;            // coalesce rapid tone changes
    _rbMuteInFlight = true;
    const audio = window.slopsmithDesktop && window.slopsmithDesktop.audio;
    if (!audio) { _rbMuteInFlight = false; return; }
    const target = (typeof targetGain === 'number' && isFinite(targetGain) && targetGain >= 0)
        ? Math.max(0, Math.min(32, targetGain))   // was 4 — clamped the auto-level + user makeup; chains can need ~20×
        : 1.0;
    const hold = (typeof window.__rbMutePreLoadHold === 'number')
        ? Math.max(20, window.__rbMutePreLoadHold | 0)
        : 100 + 50 * Math.max(1, chainLen | 0);
    let wasMuted = false;
    try { if (typeof audio.isMonitorMuted === 'function') wasMuted = !!(await audio.isMonitorMuted()); } catch (_) {}
    try {
        // `chain` = post-NAM, pre-output. Setting to 0 silences the guitar
        // signal path without touching the song's backing track.
        if (typeof audio.setGain === 'function') await audio.setGain('chain', 0);
        if (typeof audio.setMonitorMute === 'function') await audio.setMonitorMute(true);
    } catch (_) {}
    setTimeout(async () => {
        try {
            // Un-mute the monitor immediately (it's a hard mute, no transient).
            if (!wasMuted && typeof audio.setMonitorMute === 'function') await audio.setMonitorMute(false);
            // Fade chain gain 0 → target over ~24 ms in 4 steps so the
            // restore doesn't click. Final value is the smart target,
            // not a fixed 1.0 — that's how we normalise across "amp +
            // cab" and "amp only" without a user-facing knob.
            if (typeof audio.setGain === 'function') {
                const steps = [target * 0.25, target * 0.5, target * 0.8, target];
                for (const v of steps) {
                    await audio.setGain('chain', v);
                    await new Promise(r => setTimeout(r, 6));
                }
            }
        } catch (_) {}
        _rbMuteInFlight = false;
    }, hold);
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

            const r = await fetch(`/api/plugins/nam_tone/mappings/${encodeURIComponent(filename)}`);
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

            // Goes through our patched loadPreset (mute + chain-gain 0)
            // so the AMP-on transient is suppressed just like a tone change.
            await api.loadPreset(JSON.stringify(full.native_preset));
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
    currentTab: 'song',        // post-restructure default (Songs is the working tab)
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

// Cache-bust query for gear-photo URLs. Set once per session so:
//   - 200 responses still ETag-validate on each refresh (no extra
//     network traffic — the param doesn't change between renders)
//   - 404 responses cached by the browser from BEFORE a fix (e.g.
//     the case-insensitive lookup landed) get a new URL on the next
//     Slopsmith launch, busting the stale cache miss
// The current epoch is plenty unique; we only need it to differ
// across plugin restarts.
const _RB_GEAR_PHOTO_CB = `?cb=${Date.now()}`;
const NAM_API = '/api/plugins/nam_tone';

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
    let _mega = null;          // last fetched /mega_chain response
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
        if (window.__rbMegaChain === false) return false;
        // Mirror written by rbSaveSettings; falls back to false until
        // /settings has been fetched at least once.
        return !!window.__rbMegaChainSetting;
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
    function _forceBundleAmpOff() {
        const btn = document.getElementById('btn-nam');
        if (!btn) return;
        const isOn = /(?:^|\s)bg-green-/.test(btn.className);
        if (isOn) {
            try { btn.click(); } catch (_) {}
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
        const totalStages = (_mega.native_preset && _mega.native_preset.chain && _mega.native_preset.chain.length) || 0;
        if (!totalStages) return;

        // Build a map: idx → desired bypass. Default for every slot is
        // bypassed=true (passthrough). For master + active-tone slots,
        // use the persisted bypass from the backend.
        const bypassByIdx = new Array(totalStages).fill(true);
        const applyEntry = (entry) => {
            if (!entry || typeof entry.idx !== 'number') return;
            if (entry.idx < 0 || entry.idx >= totalStages) return;
            bypassByIdx[entry.idx] = !!entry.bypassed;
        };
        (_mega.master_pre_slots  || []).forEach(applyEntry);
        (_mega.master_post_slots || []).forEach(applyEntry);
        if (tone && Array.isArray(tone.slots)) tone.slots.forEach(applyEntry);

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
        _activeToneKey = activeToneKey;
    }

    async function buildForSong(filename) {
        if (!_settingOn()) {
            console.log('[rig_builder mega-chain] buildForSong skipped — setting off');
            return false;
        }
        const api = _api();
        if (!api) {
            console.warn('[rig_builder mega-chain] buildForSong aborted — no native audio API');
            return false;
        }
        if (!filename) {
            console.warn('[rig_builder mega-chain] buildForSong aborted — no filename');
            return false;
        }
        // Tear down any previous session before starting a fresh one.
        await teardown(true);   // silent — no stem restore on chained calls

        let resp;
        try {
            resp = await fetch(`${RB_API}/mega_chain/${encodeURIComponent(filename)}`);
        } catch (e) {
            console.warn('[rig_builder mega-chain] fetch failed:', e);
            return false;
        }
        if (!resp.ok) {
            // No mappings for this song, or backend error → silently fall
            // back to the cooperative path. The bundle will still work.
            console.warn(`[rig_builder mega-chain] /mega_chain/${filename} → HTTP ${resp.status} (no tone mappings for this song? Run Batch all or open it in per-song tab first to seed mappings)`);
            return false;
        }
        const mega = await resp.json();
        if (!mega || !mega.native_preset
            || !Array.isArray(mega.native_preset.chain)
            || mega.native_preset.chain.length === 0) {
            console.warn('[rig_builder mega-chain] empty chain returned by backend:', mega);
            return false;
        }
        _mega = mega;

        // 1. Force the bundle's AMP off so it stops fighting us.
        _forceBundleAmpOff();

        // 2. Mute the bundle's guitar stem so the song's DI doesn't
        //    play through alongside our chain.
        _duckGuitarStem();

        // 3. Load the mega-chain into the engine — single loadPreset call.
        //    AWAIT the pre-load mute so the chain output is actually at 0
        //    before clearChain+loadPreset run. Earlier this was a fire-and-
        //    forget call, which raced the loadPreset and let the attack
        //    transient leak through ("still gives feedback sometimes on
        //    initial song load" — Discord report).
        await rbPreLoadMute(
            mega.native_preset.chain.length,
            rbChainGainTargetFor(mega.native_preset.chain)
        ).catch(() => {});
        try {
            if (api.clearChain) await api.clearChain().catch(() => {});
            const res = await api.loadPreset(JSON.stringify(mega.native_preset));
            if (!res || res.success === false) {
                throw new Error((res && res.error) || 'loadPreset failed');
            }
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
                res);
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
                        for (let i = 0; i < loaded.length; i++) {
                            const s = loaded[i];
                            const id = (s && (s.id != null ? s.id : s.slotId != null ? s.slotId : i));
                            _indexToSlotId[i] = id;
                        }
                        const expected = mega.native_preset.chain.length;
                        const got = _indexToSlotId.length;
                        if (got !== expected) {
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
        const initialTone = initialKey ? _findToneByKey(initialKey) : null;
        await _applyActiveTone(initialTone ? initialTone.tone_key : null);
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
            if (!wasRunning && api.startAudio) await api.startAudio();
            // Input gain to chain-input-drive — pre-NAM, drives the amp
            // captures from clean region into their saturation operating
            // point (see rbApplyChainInputDrive comment).
            await rbApplyChainInputDrive();
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
                        console.log(`[rig_builder mega-chain] late base/first-change → "${tone.tone_key}"`);
                    }).catch(() => {});
                    return;
                }
            }
            const fallback = _pickDefaultTone();
            if (!fallback) return;
            _applyActiveTone(fallback.tone_key).then(() => {
                console.warn(
                    `[rig_builder mega-chain] FALLBACK after 10s: applying `
                    + `"${fallback.tone_key}" — highway never published a tone `
                    + `base OR a tone change schedule, AND the early `
                    + `no-schedule detector at t+1500ms didn't fire (so highway `
                    + `looked like it might still be loading). Edge case.`);
            }).catch(() => {});
        }, 10000);

        _active = true;
        return true;
    }

    // Watch the bundle's AMP button and click it back off if anything
    // turns it on while we're active. The bundle re-prends AMP on some
    // events (tone-mapping reload, song restart, MIDI mode toggles…)
    // and once on it will call _namApplyCurrentSongTone, which does a
    // clearChain + loadPreset that destroys our mega-chain. Mute monitor
    // momentarily so the click of the toggle isn't audible.
    let _ampGuardHandle = null;
    function _startAmpGuard() {
        _stopAmpGuard();
        _ampGuardHandle = setInterval(() => {
            if (!_active) return;
            const btn = document.getElementById('btn-nam');
            if (!btn) return;
            const isOn = /(?:^|\s)bg-green-/.test(btn.className);
            if (isOn) {
                console.warn('[rig_builder mega-chain] AMP turned on by bundle — turning it back off');
                try { btn.click(); } catch (_) {}
                // After AMP-off the bundle has already done clearChain;
                // rebuild our mega-chain so audio comes back.
                const filename = window.slopsmith && window.slopsmith.currentSong
                    && window.slopsmith.currentSong.filename;
                if (filename) {
                    setTimeout(() => {
                        buildForSong(filename).catch(e =>
                            console.warn('[rig_builder mega-chain] re-build after AMP-off failed:', e));
                    }, 200);
                }
            }
        }, 500);
    }
    function _stopAmpGuard() {
        if (_ampGuardHandle) { clearInterval(_ampGuardHandle); _ampGuardHandle = null; }
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
        _stopPolling();
        _stopAmpGuard();
        if (!silent) _restoreGuitarStem();
        if (_active) {
            const api = _api();
            if (api && api.clearChain) {
                try { await api.clearChain(); } catch (_) {}
            }
        }
        _active = false;
        _mega = null;
        _activeToneKey = null;
        _indexToSlotId = [];
    }

    function isActive() { return _active; }
    function settingOn() { return _settingOn(); }

    return { buildForSong, teardown, isActive, settingOn };
})();

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
    fetch(`${RB_API}/settings`).then(r => r.json()).then(s => {
        if (s && typeof s.mega_chain_mode !== 'undefined') {
            window.__rbMegaChainSetting = !!s.mega_chain_mode;
            console.log(`[rig_builder mega-chain] boot setting=${window.__rbMegaChainSetting} (read from /settings)`);
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

    function triggerBuild(filename, source) {
        if (!RbMegaChain.settingOn()) {
            console.log('[rig_builder mega-chain] skip — setting off');
            return;
        }
        if (!filename) {
            console.log('[rig_builder mega-chain] skip — no filename from', source);
            return;
        }
        console.log(`[rig_builder mega-chain] song detected via ${source}: ${filename} — scheduling buildForSong in 600 ms`);
        // Give the bundle ~600 ms to inject its #btn-nam etc. so our
        // AMP-off click hits a real button. Also lets the highway
        // stabilise so resolveActiveToneKey reads a sensible value.
        setTimeout(() => {
            RbMegaChain.buildForSong(filename).then(ok => {
                if (!ok) console.warn(`[rig_builder mega-chain] buildForSong returned false for "${filename}" (no mappings? bundle interfered?)`);
            }).catch(e =>
                console.warn('[rig_builder mega-chain] buildForSong threw:', e));
        }, 600);
    }

    let _lastSeenFile = null;

    function hook() {
        if (!window.slopsmith || typeof window.slopsmith.on !== 'function') {
            setTimeout(hook, 500);
            return;
        }
        window.slopsmith.on('song:loaded', (info) => {
            // Some Slopsmith builds emit song:loaded with no payload (or a
            // payload missing `filename`). Fall back to currentSong before
            // giving up — same info, different source.
            const filename = (info && info.filename)
                || (window.slopsmith.currentSong && window.slopsmith.currentSong.filename);
            _lastSeenFile = filename;
            triggerBuild(filename, info && info.filename ? 'song:loaded event' : 'song:loaded event (fallback to currentSong)');
        });
        window.slopsmith.on('song:unloaded', () => {
            _lastSeenFile = null;
            if (RbMegaChain.isActive()) RbMegaChain.teardown(false).catch(() => {});
        });
        // Catch up on a song that was already loaded when we hooked in:
        // the event has already fired and EventEmitter won't replay it.
        const cur = window.slopsmith.currentSong;
        if (cur && cur.filename && cur.filename !== _lastSeenFile) {
            _lastSeenFile = cur.filename;
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
            _lastSeenFile = f;
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
    try {
        const r = await fetch(`${RB_API}/status`);
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
            `Missing <code class="bg-dark-800 px-1 rounded">rs_to_real.json</code>. Go to Settings → Regenerate gear map and point it at your game's <code class="bg-dark-800 px-1 rounded">gears.psarc</code>.`
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
    // Three states for the Rocksmith-IR line:
    //   1. JSON loaded + .wav files on disk → green, count of disk-resident IRs
    //   2. JSON loaded but no .wav (fresh install / no Rocksmith)  → yellow nudge
    //   3. No JSON at all                                          → just hidden
    let irLine = '';
    if (s.rs_cab_to_ir_loaded && s.rs_irs_on_disk > 0) {
        irLine = `<span class="text-green-400">${s.rs_irs_on_disk} Rocksmith cab IRs on disk</span>`;
    } else if (s.rs_cab_to_ir_loaded) {
        irLine = '<span class="text-yellow-400">IR map loaded but the .wav files are not on disk — Settings → Extract Rocksmith IRs</span>';
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
        const st = await (await fetch(`${RB_API}/preload_status`)).json();
        if (st && st.running) {
            rbPreloadStartPolling();
        }
    } catch (_) { /* non-fatal */ }
    summary.textContent = 'Loading inventory…';
    root.innerHTML = '';
    let data;
    try {
        const r = await fetch(`${RB_API}/nam_inventory`);
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
        const r = await fetch(`${RB_API}/nam_file?path=${encodeURIComponent(path)}`, {
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
    try {
        const r = await fetch(`${RB_API}/preload_curated_variants`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({}),
        });
        const d = await r.json();
        if (!r.ok) throw new Error(d.error || r.status);
        if (d.started === false) {
            alert('Already running — current progress is shown live in the Manage tab.');
            rbPreloadStartPolling();
            return;
        }
        rbPreloadStartPolling();
    } catch (e) {
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
        st = await (await fetch(`${RB_API}/preload_status`)).json();
    } catch (e) {
        return;
    }
    const summary = document.getElementById('rb-manage-summary');
    const total = st.total || 0;
    const done = st.done || 0;
    const pct = total > 0 ? Math.round((done / total) * 100) : 0;
    if (st.running) {
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
        alert('Done.\n\n' + lines.join('\n'));
    }
}

async function rbPurgeNams(filter, label) {
    if (!confirm(`Purge ${label}?\n\nThis deletes the file(s) AND reverts every gear using them to Pending. Cannot be undone.`)) {
        return;
    }
    try {
        const r = await fetch(`${RB_API}/nam_purge`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ ...filter, confirm: true }),
        });
        const d = await r.json();
        if (!r.ok) throw new Error(d.error || r.status);
        rbLoadManageTab();
        if ((d.errors || []).length) {
            alert(`Purged ${d.deleted_count} files. ${d.errors.length} errors:\n` +
                  d.errors.slice(0, 5).join('\n'));
        }
    } catch (e) {
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

async function rbStartBatch(mode) {
    mode = mode || 'all';
    const btns = document.querySelectorAll('.rb-batch-btn');
    btns.forEach(b => { b.disabled = true; });
    try {
        const r = await fetch(`${RB_API}/batch_all`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ mode }),
        });
        if (!r.ok) {
            const err = await r.json().catch(() => ({}));
            alert(`Couldn't start batch: ${err.error || r.status}`);
            return;
        }
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
        const r = await fetch(`${RB_API}/batch_status`);
        st = await r.json();
    } catch (e) {
        return;
    }
    const pct = st.total ? Math.round(100 * st.progress / st.total) : 0;
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
    }
}

// ── Pending ────────────────────────────────────────────────────────

async function rbLoadPending() {
    const el = document.getElementById('rb-pending-list');
    el.innerHTML = '<span class="text-gray-500">Loading…</span>';
    let data;
    try {
        const r = await fetch(`${RB_API}/coverage`);
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
        const r = await fetch(`${RB_API}/search?${qs}`);
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
                ? `<img src="${rbEsc(c.images[0])}" alt="" loading="lazy" class="w-12 h-12 rounded object-cover bg-dark-900 flex-shrink-0" onerror="this.style.visibility='hidden'">`
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
                 auto-generated query (a Rocksmith pseudonym) doesn't
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
    document.body.appendChild(modal);
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
        const r = await fetch(`${RB_API}/override_query`, {
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
// debounces an actual /list_songs hit so we don't spam the backend on
// every keystroke. 250 ms is the sweet spot between "feels live" and
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
    const r = await fetch(`${RB_API}/list_songs?q=${encodeURIComponent(q)}`);
    const data = await r.json();
    const el = document.getElementById('rb-song-list');
    if (!data.songs.length) {
        el.innerHTML = '<p class="text-gray-500 text-sm">No matches</p>';
        return;
    }
    // Materialized vs cloud-only: cloud songs get a small icon and a
    // dimmer text color so the user knows clicking will trigger a
    // Drive download before the chain becomes available.
    el.innerHTML = data.songs.map(s => {
        const name = typeof s === 'string' ? s : s.name;
        const mat = typeof s === 'string' ? true : s.materialized;
        const title = (typeof s === 'object' && s.title) || '';
        const artist = (typeof s === 'object' && s.artist) || '';
        const year = (typeof s === 'object' && s.year) || '';
        const cloudTag = mat ? '' : '<span class="text-xs text-blue-400 ml-2">☁ cloud</span>';
        const textColor = mat ? 'text-gray-300' : 'text-gray-500';
        // Two-line display when metadata is available, otherwise just the
        // filename (older library that hasn't been re-scanned by Slopsmith).
        let label;
        if (title || artist) {
            const yearTag = year ? ` <span class="text-gray-600">(${rbEsc(year)})</span>` : '';
            label = `
                <div class="flex-1 min-w-0">
                    <div class="truncate">${rbEsc(title || '(untitled)')}${yearTag}</div>
                    <div class="text-xs text-gray-500 truncate" title="${rbEsc(name)}">${rbEsc(artist || '(unknown artist)')}</div>
                </div>`;
        } else {
            label = `<span class="flex-1 truncate" title="${rbEsc(name)}">${rbEsc(name)}</span>`;
        }
        return `
            <div onclick="rbLoadSongTones('${rbEsc(name).replace(/'/g,"\\'")}')"
                 class="cursor-pointer hover:bg-dark-700/50 px-3 py-2 rounded text-sm ${textColor} flex items-center">
                ${label}
                ${cloudTag}
            </div>`;
    }).join('');
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
    const banner = document.createElement('div');
    banner.className = 'rb-autodl-banner bg-blue-900/15 border border-blue-800/30 rounded-lg p-3 text-sm mb-4';
    banner.innerHTML = `<p class="text-blue-400">⬇ Auto-downloading ${unmappedCount} unassigned piece(s) from tone3000…</p>`;
    container.prepend(banner);
    try {
        const r = await fetch(`${RB_API}/auto_download_song`, {
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
        if (result.rs_ir_used) parts.push(`${result.rs_ir_used} Rocksmith IRs`);
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
        const r = await fetch(`${RB_API}/song/${encodeURIComponent(filename)}`);
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
//   │            │ Rocksmith knobs: Rate=50 …             │
//   ├ Footer ─────────────────────────────────────────────┤
//   │ ＋ Add piece                       ▶ Listen  💾 Save │
//   └─────────────────────────────────────────────────────┘
//
// Photos come from RB_API/gear_photo/<rs_gear> served by routes.py
// (Rocksmith art extracted via extract_gear_photos.py). Missing photos
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
    // Photo lookup: backend returns 404 when no Rocksmith art exists for
    // this rs_gear. The onerror swaps the broken <img> for the sibling
    // placeholder via plain DOM properties — avoids HTML-in-attribute
    // escaping bugs.
    const imgUrl = `${RB_API}/gear_photo/${encodeURIComponent(p.type)}${_RB_GEAR_PHOTO_CB}`;
    const onerr = "this.style.display='none'; var n=this.nextElementSibling; if(n) n.classList.remove('hidden');";
    return `
        <button onclick="rbSelectPiece(${toneIdx}, ${pIdx})"
                class="relative flex-shrink-0 w-28 rounded-lg border ${selCls} p-2 text-left transition focus:outline-none">
            <div class="text-[9px] text-gray-500 mb-1 flex items-center justify-between">
                <span class="font-mono">${pIdx + 1}/${total}</span>
                <span class="uppercase tracking-wide">${rbEsc(p.rs_category || '')}</span>
            </div>
            <div class="relative flex justify-center items-center mb-1.5 h-20 rounded bg-dark-900 overflow-hidden">
                <div class="absolute inset-0 flex items-center justify-center text-[10px] text-gray-600 text-center px-1 leading-tight ${imgBypassCls}">
                    ${rbEsc(p.rs_category || 'gear')}
                </div>
                <img src="${imgUrl}" alt="" loading="lazy"
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
    // …). Falls back to the legacy "Rocksmith IR (N):" filename dropdown
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
                    <span class="text-[10px] text-gray-500">Rocksmith-extracted IRs — click to switch</span>
                </div>
                <div class="flex items-center gap-1.5 flex-wrap">${btns}</div>
            </div>`;
    } else if ((p.rs_irs || []).length > 0) {
        // Legacy fallback: raw dropdown for cabs without a mic map.
        const rsIrs = p.rs_irs;
        const options = rsIrs.map(f => `<option value="${rbEsc(f)}">${rbEsc(f.split('/').pop())}</option>`).join('');
        rsIrControl = `
            <div class="flex items-center gap-2 bg-green-900/15 border border-green-800/30 rounded px-2 py-1.5 mt-2">
                <span class="text-xs text-green-400 whitespace-nowrap">Rocksmith IR (${rsIrs.length}):</span>
                <select onchange="rbPickRsIr(this, ${toneIdx}, ${pIdx})"
                        class="flex-1 bg-dark-800 border border-gray-800 rounded text-xs text-gray-300 px-1 py-0.5">${options}</select>
                <button onclick="rbAssignRsIr(this, ${toneIdx}, ${pIdx})"
                        class="bg-green-700 hover:bg-green-600 text-white text-xs px-2 py-0.5 rounded">Use</button>
            </div>`;
    }

    // Amp gain variant picker — clickable buttons, active level highlighted.
    let ampVariantBadge = '';
    if (p.amp_variant && Array.isArray(p.amp_variant.available) && p.amp_variant.available.length) {
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

    // RS knob badges — read-only summary of Rocksmith's per-piece values.
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
                <div class="text-[10px] text-gray-500 mb-1.5">Rocksmith knob values (read-only)</div>
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
    const imgUrl = `${RB_API}/gear_photo/${encodeURIComponent(p.type)}${_RB_GEAR_PHOTO_CB}`;
    const onerrBig = "this.style.display='none'; var n=this.nextElementSibling; if(n) n.classList.remove('hidden');";

    return `
        <div class="bg-dark-800/40 border-y border-gray-800/40 p-4 space-y-3" data-tone="${toneIdx}" data-piece="${pIdx}">
            <div class="flex items-start gap-4">
                <div class="flex-shrink-0 w-32 h-32 flex items-center justify-center">
                    <img src="${imgUrl}" alt="" loading="lazy"
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
                        title="Load this VST in the engine and edit its parameters with inline sliders"
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
        return true;
    });
}

// Tear down the currently-loaded inline-editor VST: close its native window
// FIRST, then clear its slot. Skipping the close left an orphaned native
// editor window pointing at a slot we then cleared — re-editing (or editing a
// second piece) crashed the host. Resets the tracked slot so the next open
// starts clean.
async function rbTeardownVstEditor(api) {
    const slot = rbState._vstEditorSlot;
    rbState._vstEditorSlot = null;
    if (slot == null || !api) return;
    try { if (api.closePluginEditor) await api.closePluginEditor(slot); } catch (_) {}
    try { if (api.clearChain) await api.clearChain(); } catch (_) {}
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
        // Close + clear any previously-open editor (this or another piece)
        // before loading — closing its native window first avoids the crash.
        await rbTeardownVstEditor(api);
        await api.startAudio().catch(() => {});
        const slotId = await api.loadVST(vstPath);
        if (slotId == null || slotId < 0) {
            editor.innerHTML = `<div class="text-xs text-red-400">engine refused to load this plugin</div>`;
            return;
        }
        rbState._vstEditorSlot = slotId;
        piece._vst_slot_id = slotId;
        // Keep any previously-saved opaque blob so re-saving without a fresh
        // capture (e.g. just closing) doesn't drop it.
        piece._vst_opaque = piece._vst_opaque
            || rbParseVstStateOpaque(piece._vst_state)
            || rbParseVstStateOpaque(piece.assigned && piece.assigned.vst_state);
        // Re-apply previously captured params if any. Helper resolves NAME
        // keys (from apply_vst_state.py bulk-populated states) → numeric
        // ids and clamps values to [0,1]. Without this, name-keyed states
        // silently no-op (parseInt("Threshold")=NaN) → editor opens at
        // plugin defaults.
        const saved = piece._vst_params
            || (piece.assigned && piece.assigned.vst_state
                ? rbParseVstStateParams(piece.assigned.vst_state) : null);
        const params = await rbRestoreSavedParamsToSlot(api, slotId, saved);
        piece._vst_param_meta = params;
        // Seed _vst_params with the FULL current snapshot so subsequent
        // slider drags modify a complete dict (not just the touched ids).
        // Persisting partial dicts was a data-loss bug: untouched params
        // would silently revert to plugin defaults on chain rebuild.
        piece._vst_params = {};
        for (const param of params) {
            const id = param.id ?? param.paramId ?? param.index;
            const v  = param.value ?? param.current;
            if (id != null && typeof v === 'number') piece._vst_params[id] = v;
        }
        // Render the inline slider panel FIRST so a headless plugin (no GUI —
        // e.g. the bundled QTron envelope filter) still gets an editable
        // panel even if openPluginEditor misbehaves for a UI-less plugin
        // (returns non-promise / throws). Native-window plugins are unaffected.
        rbToneRenderInlineVstParams(toneIdx, pIdx);
        if (api.openPluginEditor) {
            try {
                const _ed = api.openPluginEditor(slotId);
                if (_ed && typeof _ed.catch === 'function') _ed.catch(() => {});
            } catch (_) { /* UI-less plugin: no native editor view to open */ }
        }
    } catch (e) {
        editor.innerHTML = `<div class="text-xs text-red-400">load failed: ${rbEsc(e.message || e)}</div>`;
    } finally {
        rbState._vstEditorBusy = false;
    }
}

function rbToneRenderInlineVstParams(toneIdx, pIdx) {
    const editor = document.getElementById(`rb-tone-vst-editor-${toneIdx}-${pIdx}`);
    if (!editor) return;
    const piece = rbState.songTones.tones[toneIdx].chain[pIdx];
    const params = rbFilterVstParams((piece && piece._vst_param_meta) || []);
    const effVstPath = rbEffVstPath(piece);
    const vstName = effVstPath.split('/').pop().replace(/\.(vst3|component)$/i, '');
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
//                      piece to that gear's NAM (variant auto-picked
//                      by the row's Gain knob). Backed by POST
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
        const r = await fetch(`${RB_API}/piece_variant_override`, {
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
    const r = await fetch(`${RB_API}/gears_in_category/${encodeURIComponent(category)}`);
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
            ? `<img src="${rbEsc(g.image)}" alt="" loading="lazy" class="w-9 h-9 rounded object-cover bg-dark-900 flex-shrink-0">`
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
        <div class="text-[10px] text-gray-500 italic mt-2">Gears with curated multi-NAM variants are recommended. Cabs are skipped — use the IR dropdown instead.</div>`;
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
            ? `<img src="${rbEsc(g.image)}" alt="" loading="lazy" class="w-9 h-9 rounded object-cover bg-dark-900 flex-shrink-0">`
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
        const r = await fetch(`${RB_API}/gear/replace_with`, {
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
        const r = await fetch(`${RB_API}/song/${encodeURIComponent(filename)}`);
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
        const r = await fetch(`${RB_API}/master_chain`);
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
        const r = await fetch(`${RB_API}/master_chain/save`, {
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
        const slotId = await api.loadVST(vstPath);
        if (slotId == null || slotId < 0) {
            editor.innerHTML = `<div class="text-xs text-red-400">engine refused to load this plugin</div>`;
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
        const params = await rbRestoreSavedParamsToSlot(api, slotId, saved);
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
        // Render inline sliders FIRST (headless plugins like the bundled QTron
        // have no native window); then open the plugin's own editor window as
        // an optional visual. The inline sliders drive everything regardless.
        rbMasterRenderInlineVstParams(role, idx);
        if (api.openPluginEditor) {
            try {
                const _ed = api.openPluginEditor(slotId);
                if (_ed && typeof _ed.catch === 'function') _ed.catch(() => {});
            } catch (_) { /* UI-less plugin: no native editor view to open */ }
        }
    } catch (e) {
        editor.innerHTML = `<div class="text-xs text-red-400">load failed: ${rbEsc(e.message || e)}</div>`;
    } finally {
        rbState._vstEditorBusy = false;
    }
}

function rbMasterRenderInlineVstParams(role, idx) {
    const editor = document.getElementById(`rb-master-${role}-editor-${idx}`);
    if (!editor) return;
    const piece = rbState.master[role][idx];
    const params = rbFilterVstParams((piece && piece._vst_param_meta) || []);
    const vstName = (piece._vst_path || '').split('/').pop().replace(/\.(vst3|component)$/i, '');
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
            <div class="text-xs text-gray-500 italic mt-1">
                This plugin doesn't expose any parameters to the host (or getParameters() failed).
                Use the plugin's native editor window for tweaks.
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
            const r = await fetch(`${RB_API}/gears_catalog`);
            const data = await r.json();
            _rbGearsCatalog = (data && data.gears) || [];
        } catch (_) { _rbGearsCatalog = []; }
    }
    // Initialise per-picker state. Defaults: Rocksmith section, DAW
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
                🎸 Rocksmith gear <span class="opacity-60 ml-1">${(_rbGearsCatalog || []).length}</span>
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
        body = rbBuildRocksmithPickerBody({
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
        const r = await fetch(`${RB_API}/local_files?kind=${kind}`);
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
            const r = await fetch(`${RB_API}/gears_catalog`);
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

// Per-tone Add picker — now with two top sections: Rocksmith vs VST.
//
// The Rocksmith section browses rs_to_real.json grouped by DAW-style
// subcategories so users find pieces the way they'd look in any DAW
// plugin browser. The VST section lists installed plugins from the
// engine's scan + a paste-path input, so the user can drop a "pure VST"
// (e.g. a limiter) straight into the chain without having to first
// map it to some Rocksmith pedal.
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
                🎸 Rocksmith gear <span class="opacity-60 ml-1">${(_rbGearsCatalog || []).length}</span>
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
        body = rbBuildRocksmithPickerBody({
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

// ── Shared section bodies (Rocksmith + VST) ──
// Both bodies receive the rendering context as inline onclick strings
// so they work in the per-tone picker AND the master-chain picker
// without needing closures over the caller.

function rbBuildRocksmithPickerBody({ dawCat, filter, onCategoryCall, onFilterCall, onAddCall, searchId }) {
    const f = (filter || '').toLowerCase().trim();
    const matches = (_rbGearsCatalog || []).filter(g => {
        if ((g.daw_category || 'other') !== dawCat) return false;
        if (!f) return true;
        return (g.name || '').toLowerCase().includes(f)
            || (g.rs_gear || '').toLowerCase().includes(f)
            || (g.make || '').toLowerCase().includes(f);
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

// Add a "pure VST" piece — no Rocksmith mapping required. Generates a
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
                    title="Apply this song's Rocksmith knob values to the VST params (requires a curated mapping in rs_knob_to_vst_param.json)"
                    class="bg-cyan-700/70 hover:bg-cyan-600/70 text-cyan-100 text-xs px-2 py-1 rounded">
                ⇶ Apply RS settings
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
        const r = await fetch(`${RB_API}/vst/knob_mapping?rs_gear_type=${encodeURIComponent(piece.type)}&vst_name=${encodeURIComponent(vstStem)}`);
        const data = await r.json();
        mapping = data && data.mapping;
    } catch (e) {
        return setStatus(`mapping lookup failed: ${e.message || e}`);
    }
    if (!mapping) {
        return setStatus(`No curated mapping for ${piece.type} × ${vstStem}. Replicate manually using the "Rocksmith settings" panel above, then curate rs_knob_to_vst_param.json so future songs auto-apply.`);
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
            // Try direct index parse first (e.g. param: "5"), then name match.
            const asInt = parseInt(rule.param, 10);
            if (!isNaN(asInt) && String(asInt) === rule.param.trim()) {
                targetId = asInt;
            } else {
                targetId = nameToId[rule.param.toLowerCase()];
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
    if (api && typeof api.loadPluginList === 'function' && typeof api.getKnownPlugins === 'function') {
        try {
            // loadPluginList loads the engine's cached list (no scan). Safe
            // to call even with no cache — it just no-ops.
            await api.loadPluginList();
            const plugins = await api.getKnownPlugins();
            if (Array.isArray(plugins) && plugins.length > 0) {
                rbState.knownVsts = plugins;
                // Sync to our backend cache so future loads work even if
                // the engine cache gets wiped.
                fetch(`${RB_API}/vst/sync_known`, {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({plugins}),
                }).catch(() => {});
                return;
            }
        } catch (_) { /* fall through to backend cache */ }
    }
    try {
        const r = await fetch(`${RB_API}/vst/known`);
        if (!r.ok) return;
        const data = await r.json();
        rbState.knownVsts = Array.isArray(data.plugins) ? data.plugins : [];
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
        await fetch(`${RB_API}/vst/sync_known`, {
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
            const merged = await (await fetch(`${RB_API}/vst/known`)).json();
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
async function rbRestoreSavedParamsToSlot(api, slotId, savedParams) {
    // Small grace period after loadVST. Some JUCE-hosted VST3 plugins (esp.
    // larger ones like MCompressor with 150 params) finish parameter setup
    // a tick or two after loadVST resolves; calling setParameter inside that
    // window can silently no-op even though it returns without throwing.
    // Empirically 50 ms is enough on M1 with kHs / Melda free.
    await new Promise(r => setTimeout(r, 50));
    let params = [];
    if (typeof api.getParameters === 'function') {
        try {
            const raw = await api.getParameters(slotId);
            if (Array.isArray(raw)) params = raw;
        } catch (e) {
            console.warn('[rig_builder restore] getParameters threw:', e);
        }
    }
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
    const sampleParam = params[0] || {};
    console.log(`[rig_builder restore] slot=${slotId} · live param shape keys: ${Object.keys(sampleParam).join(', ')} · first 5 names: ${params.slice(0, 5).map(p => p.name || p.label || '<no-name>').join(' | ')}`);
    let applied = 0;
    const failed = [];
    const appliedDetail = [];
    for (const [pid, v] of Object.entries(savedParams)) {
        let targetId = parseInt(pid, 10);
        let resolvedBy = 'numeric';
        if (isNaN(targetId) || String(targetId) !== String(pid).trim()) {
            targetId = nameToId[String(pid).toLowerCase()];
            resolvedBy = (targetId != null) ? 'name' : 'unresolved';
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
            const refreshed = await api.getParameters(slotId);
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
        const slotId = await api.loadVST(path);
        if (slotId == null || slotId < 0) throw new Error('engine refused to load this plugin');
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
        const params = await rbRestoreSavedParamsToSlot(api, slotId, savedParams);
        piece._vst_param_meta = params;
        rbRenderInlineVstParams(toneIdx, pIdx);
        if (statusEl) {
            statusEl.textContent = `loaded slot ${slotId} · ${params.length} params · tweak below, then "Capture state"`;
        }
    } catch (e) {
        if (statusEl) statusEl.textContent = `load failed: ${e.message || e}`;
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
        const needsResolve = Object.keys(params).some(
            k => isNaN(parseInt(k, 10)) || String(parseInt(k, 10)) !== String(k).trim()
        );
        if (needsResolve && typeof api.getParameters === 'function') {
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
            let targetId = parseInt(pid, 10);
            if ((isNaN(targetId) || String(targetId) !== String(pid).trim()) && nameToId) {
                targetId = nameToId[String(pid).toLowerCase()];
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
    const targetUrl = piece.rs_category === 'cab' ? `${NAM_API}/irs` : `${NAM_API}/models`;

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
        const r = await fetch(`${RB_API}/save_preset`, {
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
        return body.preset_id ?? null;
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

// Stop whatever preview is active (native full-chain or nam_tone fallback).
async function rbStopPreview() {
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

// Reload the current native preview chain. Pass a presetId to refetch the
// chain (after a gear change); omit it to just re-apply bypass flags to the
// already-fetched chain (after a bypass toggle). Audio keeps running.
async function rbReloadPreview(refetchPresetId) {
    if (rbState.listeningTone === null || rbState._previewMode !== 'native') return;
    const api = rbNativeAudio();
    if (!api) return;
    if (refetchPresetId != null) {
        try {
            rbState._previewPayload = await (await fetch(`${RB_API}/native_preset_full/${refetchPresetId}`)).json();
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
        if (api.clearChain) await api.clearChain().catch(() => {});
        await api.loadPreset(JSON.stringify(payload.native_preset));
        // Engine sometimes leaves a slot bypassed across reloads — force each
        // slot's bypass to match the spec so toggling un-bypass actually un-bypasses.
        await rbReapplyBypassToChain(api, chainArr);
        // VST params: the opaque state in the chain JSON doesn't reliably
        // restore plug-in params; walk the chain and call setParameter
        // explicitly so VSTs come up at their saved values, not defaults.
        await rbReapplyVstParamsToChain(api, chainArr).catch((e) =>
            console.warn('[rig_builder] reload re-apply VST params:', e));
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
        const url = `${RB_API}/native_preset_one?file=${encodeURIComponent(file)}&kind=${encodeURIComponent(kind || 'nam')}${gainQs}${gearQs}`;
        const payload = await (await fetch(url)).json();
        const chain = payload.native_preset && payload.native_preset.chain;
        if (!Array.isArray(chain) || !chain.length) throw new Error('file not found');
        if (api.clearChain) await api.clearChain().catch(() => {});
        const res = await api.loadPreset(JSON.stringify(payload.native_preset));
        if (!res || res.success === false) throw new Error((res && res.error) || 'loadPreset failed');
        if (api.setGain) {
            // Chain-level drive matched to the audition target. Bass
            // amps (rs_gear starts with 'Bass_') use unity to avoid
            // over-saturating the tone3000 clean-gain capture; the
            // catalog always knows g.rs_gear so this is reliable.
            const isBass = typeof rsGear === 'string' && rsGear.startsWith('Bass_');
            await rbApplyChainInputDrive({ isBass });
            await api.setGain('chain', 1.0).catch(() => {});
        }
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
    try {
        const r = await fetch(`${RB_API}/audition_candidate`, {
            method: 'POST', headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ rs_gear: rsGear, tone3000_id: toneId }),
        });
        const data = await r.json();
        if (!r.ok) { alert(data.error || `HTTP ${r.status}`); btn.disabled = false; btn.textContent = old; return; }
        btn.disabled = false;
        await rbAuditionFile(data.file, data.kind, btnId);
    } catch (e) {
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

async function rbLoadCatalog() {
    const el = document.getElementById('rb-catalog');
    if (!el) return;
    if (rbState._auditionId) await rbStopPreview();   // stop stale audition before re-render
    el.innerHTML = '<p class="text-gray-500">Loading…</p>';
    let data;
    try { data = await (await fetch(`${RB_API}/gear_catalog`)).json(); }
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

function rbApplyGearFilters() {
    const el = document.getElementById('rb-catalog');
    if (!el || !rbState.gearCatalog) return;
    const search = ((document.getElementById('rb-gear-search') || {}).value || '')
        .toLowerCase().trim();
    const onlyUnassigned = !!((document.getElementById('rb-gear-only-unassigned') || {}).checked);
    const compact = !!((document.getElementById('rb-gear-compact') || {}).checked);

    // Filter items per category based on search + status. Empty
    // categories drop out so we don't render an empty header.
    const filtered = {};
    let total = 0;
    for (const cat in rbState.gearCatalog) {
        const items = rbState.gearCatalog[cat].filter(g => {
            if (onlyUnassigned && g.assigned) return false;
            if (!search) return true;
            const hay = (
                (g.real_name || '') + ' ' +
                (g.make || '') + ' ' +
                (g.model || '') + ' ' +
                (g.rs_gear || '') + ' ' +
                (g.tone3000_title || '')
            ).toLowerCase();
            return hay.includes(search);
        });
        if (items.length) {
            filtered[cat] = items;
            total += items.length;
        }
    }

    // Jump pills — one per category, with the FILTERED count so the
    // user knows how many matches landed in each. Active pill = the
    // category currently scrolled-to is not tracked (it'd need a
    // scroll observer); just style them all uniformly.
    const pillsEl = document.getElementById('rb-gear-jump-pills');
    if (pillsEl) {
        if (Object.keys(filtered).length <= 1) {
            pillsEl.innerHTML = '';   // no point in pills for one section
        } else {
            pillsEl.innerHTML = Object.keys(filtered).map(cat => {
                const collapsed = rbState.gearCollapsedCats.has(cat);
                return `<button onclick="rbScrollToCategory('${cat}')"
                        class="text-xs px-2 py-1 rounded-full transition
                               ${collapsed ? 'bg-dark-800 text-gray-500' : 'bg-dark-600 text-gray-200 hover:bg-dark-500'}">
                    ${rbEsc(RB_GEAR_LABEL[cat] || cat)}
                    <span class="text-gray-400 ml-1">${filtered[cat].length}</span>
                </button>`;
            }).join('');
        }
    }

    // Render sections. Each has a clickable header that toggles collapse.
    if (!total) {
        el.innerHTML = `<div class="text-center text-gray-500 py-10">
            No matches.${search ? ` Try clearing the search.` : ''}</div>`;
        return;
    }
    try {
        el.innerHTML = Object.keys(filtered).map(cat => {
            const items = filtered[cat];
            const collapsed = rbState.gearCollapsedCats.has(cat);
            const label = RB_GEAR_LABEL[cat] || cat;
            const body = collapsed ? '' :
                (compact
                    ? `<div class="bg-dark-800/30 border border-gray-800/30 rounded-lg divide-y divide-gray-800/30">
                          ${items.map(rbRenderCatalogCardCompact).join('')}
                       </div>`
                    : `<div class="grid grid-cols-1 md:grid-cols-2 gap-3">
                          ${items.map(rbRenderCatalogCard).join('')}
                       </div>`);
            return `<div id="rb-cat-${cat}" class="scroll-mt-4">
                <h3 onclick="rbToggleCategoryCollapse('${cat}')"
                    class="text-white font-semibold mb-3 cursor-pointer select-none flex items-center gap-2 hover:text-accent transition">
                    <span class="text-gray-500 text-xs w-3 inline-block">${collapsed ? '▶' : '▼'}</span>
                    ${rbEsc(label)}
                    <span class="text-gray-500 text-xs font-normal">(${items.length})</span>
                </h3>
                ${body}
            </div>`;
        }).join('');
    } catch (e) {
        console.error('[rig_builder] catalog render failed', e);
        el.innerHTML = `<p class="text-red-400">Error rendering: ${rbEsc(e.message)}</p>`;
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
    const c = document.getElementById('rb-gear-compact');
    if (s) s.value = '';
    if (u) u.checked = false;
    if (c) c.checked = false;
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
            <div class="text-gray-200 truncate text-xs"><strong>${rbEsc(g.real_name)}</strong>
                <span class="text-gray-500 text-[10px]">${rbEsc(g.rs_gear)}</span>
            </div>
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
    //   ▸ Rocksmith gear photo (/gear_photo/{rs_gear}) first; if the
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

    // Rocksmith art with tone3000 image as a fallback. The sibling-swap
    // trick avoids the HTML-in-attribute escaping issue we hit in the
    // song editor — onerror just hides this img and reveals the next
    // sibling, which is the next photo source down the chain.
    const rsArt = `${RB_API}/gear_photo/${encodeURIComponent(g.rs_gear)}${_RB_GEAR_PHOTO_CB}`;
    const onerrChain = "this.style.display='none'; var n=this.nextElementSibling; if(n){ if(n.tagName==='IMG'){n.style.display=''} else {n.classList.remove('hidden')} }";
    const t3kImgTag = g.image
        ? `<img src="${rbEsc(g.image)}" alt="" loading="lazy" style="display:none"
               class="max-w-full max-h-full rounded object-cover bg-dark-900"
               onerror="${onerrChain}">`
        : '';
    const photoBlock = `
        <div class="flex-shrink-0 w-16 h-16 flex items-center justify-center rounded bg-dark-900 overflow-hidden transition ${photoOff}"
             title="${isAssigned ? '' : 'Unassigned — no NAM/IR/VST mapped yet'}">
            <img src="${rsArt}" alt="" loading="lazy"
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
        listenBtn = `<button id="${btnId}" onclick="event.stopPropagation(); rbAuditionVst('${rbEsc(g.vst_path).replace(/'/g,"\\'")}','${rbEsc(g.vst_format || 'VST3')}','${btnId}')"
                            title="Listen to this VST in isolation"
                            class="bg-purple-700/50 hover:bg-purple-600/60 text-purple-100 px-3 py-1.5 rounded text-xs">▶ Listen</button>`;
        // Direct "edit this VST" — loads the plugin and opens the native
        // editor window. Saves a click vs the 📚 Library re-pick flow when
        // the gear already has a VST assigned (the common case after the
        // bulk-assign step). Passes rs_gear so rbCatalogEditVst can apply
        // the (gear, vst) `_static` defaults (e.g. kHs Distortion Type for
        // fuzz/od/dist pedals, MEqualizer band-enable flags).
        editBtn = `<button onclick="event.stopPropagation(); rbCatalogEditVst('${rbEsc(g.vst_path).replace(/'/g,"\\'")}','${rbEsc(g.vst_format || 'VST3')}','${rbEsc(g.rs_gear)}')"
                           title="Edit this VST's settings (loads + opens native editor, applies _static defaults)"
                           class="bg-purple-900/40 hover:bg-purple-900/60 text-purple-200 border border-purple-800/50 px-3 py-1.5 rounded text-xs">🎛 Edit</button>`;
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
                    <div class="text-[11px] text-gray-500 font-mono break-all">${rbEsc(g.rs_gear)}</div>
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
        const r = await fetch(`${RB_API}/amp_variants/${encodeURIComponent(rsGear)}`);
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
        const r = await fetch(`${RB_API}/tone3000/captures/${toneId}`);
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
        const r = await fetch(`${RB_API}/tone3000/captures/${toneId}`);
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
        // match a Rocksmith gain level — put it first. Size/license
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
        const r = await fetch(`${RB_API}/amp_variants/${encodeURIComponent(rsGear)}/${encodeURIComponent(level)}`, {
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
        const r = await fetch(`${RB_API}/amp_variants/${encodeURIComponent(rsGear)}/${encodeURIComponent(level)}`, {
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
            const r = await fetch(`${RB_API}/local_files?kind=${kind}`);
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
    rocksmith: '🎮 Rocksmith IRs',
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
        const r = await fetch(`${RB_API}/use_local_for_gear`, {
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
        const slotId = await api.loadVST(path);
        if (slotId == null || slotId < 0) throw new Error('engine refused to load this plugin');
        rbState._vstEditorSlot = slotId;
        if (api.openPluginEditor) {
            await api.openPluginEditor(slotId).catch((e) => console.warn('openPluginEditor:', e));
        }
        if (statusEl) statusEl.textContent = `loaded slot ${slotId} — tweak knobs, then "Capture state" or just "Assign".`;
    } catch (e) {
        if (statusEl) statusEl.textContent = `load failed: ${e.message || e}`;
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
        const r = await fetch(`${RB_API}/vst/assign`, {
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
        const r = await fetch(`${RB_API}/vst/suggest/${encodeURIComponent(rsGearType)}`);
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

async function rbCatalogEditVst(vstPath, vstFormat, rsGear) {
    const api = rbNativeAudio();
    if (!api || typeof api.loadVST !== 'function') {
        alert('Native VST hosting not available.');
        return;
    }
    try {
        await rbCloseActiveVstEditor();
        if (rbState.listeningTone !== null || rbState._auditionId) {
            await rbStopPreview();
        }
        if (api.clearChain) await api.clearChain().catch(() => {});
        const wasRunning = api.isAudioRunning ? await api.isAudioRunning().catch(() => true) : true;
        if (!wasRunning) await api.startAudio().catch(() => {});
        const slotId = await api.loadVST(vstPath);
        if (slotId == null || slotId < 0) {
            alert(`Engine refused to load this plugin:\n${vstPath}`);
            return;
        }
        rbState._vstEditorSlot = slotId;
        // Apply the (gear, vst) `_static` defaults if any — pinned params
        // curated in rs_knob_to_vst_param.json (e.g. kHs Distortion's
        // Mode + Dynamics for fuzz/od/dist subtypes). Without this, the
        // Edit button shows the plugin's defaults regardless of subtype,
        // and the user can't preview what a fuzz-vs-overdrive default
        // sounds like. RS-knob translations are NOT applied here (catalog
        // is gear-level, no per-tone knob values).
        if (rsGear) {
            const vstStem = vstPath.split('/').pop().replace(/\.(vst3|component)$/i, '').toLowerCase();
            try {
                const r = await fetch(`${RB_API}/vst/knob_mapping?rs_gear_type=${encodeURIComponent(rsGear)}&vst_name=${encodeURIComponent(vstStem)}`);
                const data = await r.json();
                const staticBlock = data && data.mapping && data.mapping._static;
                if (staticBlock && typeof staticBlock === 'object') {
                    await rbRestoreSavedParamsToSlot(api, slotId, staticBlock);
                }
            } catch (e) {
                console.warn('[rig_builder catalog-edit] _static apply skipped:', e);
            }
        }
        if (api.openPluginEditor) {
            await api.openPluginEditor(slotId).catch((e) => {
                console.warn('[rig_builder] openPluginEditor failed:', e);
                alert(`Couldn't open editor for this plugin (the native window may have crashed). Plugin is loaded — try again or use the inline editor in a song's slot.`);
            });
        } else {
            alert('This Slopsmith build has no openPluginEditor API.');
        }
    } catch (e) {
        alert(`Edit failed: ${e.message || e}`);
    }
}

async function rbAuditionVst(vstPath, vstFormat, btnId) {
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
        const slotId = await api.loadVST(vstPath);
        if (slotId == null || slotId < 0) throw new Error('engine refused this plugin');
        if (api.setMonitorMute) await api.setMonitorMute(false).catch(() => {});
        const wasRunning = api.isAudioRunning ? await api.isAudioRunning().catch(() => true) : true;
        await api.startAudio();
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
        if (btn) {
            btn.disabled = false;
            btn.textContent = btn.dataset.origLabel || '▶';
        }
        alert(`Audition failed: ${e.message || e}`);
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
            const payload = await (await fetch(`${RB_API}/native_preset_full/${presetId}`)).json();
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
            if (api.clearChain) await api.clearChain().catch(() => {});
            const res = await api.loadPreset(JSON.stringify(payload.native_preset));
            const got = res && res.slotsLoaded;
            console.log(`[rig_builder] chain sent=${chain.length} (NAM=${payload.nam_stage_count}) · slotsLoaded=${got}`, res);
            if (!res || res.success === false) throw new Error((res && res.error) || 'loadPreset failed');
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
            if (api.setGain) { await rbApplyChainInputDrive(); }
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
        const r = await fetch(`${RB_API}/settings`);
        s = await r.json();
    } catch (e) {
        return;
    }
    const megaCb = document.getElementById('rb-mega-chain-mode');
    if (megaCb) megaCb.checked = !!s.mega_chain_mode;
    const bac = document.getElementById('rb-bypass-all-cabs');
    if (bac) bac.checked = !!s.bypass_all_cabs;
    // Inverted-sense checkbox: the user opts OUT of curated-only by
    // ticking the box (= allow tone3000 fuzzy fallback). The persisted
    // setting is still `curated_only`; the UI just shows the opposite.
    const allowFuzzy = document.getElementById('rb-allow-tone3000-fallback');
    if (allowFuzzy) allowFuzzy.checked = !s.curated_only;
    // Mirror the persisted flag onto the runtime mirror so RbMegaChain
    // sees it even if the user never opens Settings. rbLoadSettings is
    // called from rbInit so this runs at page-load.
    window.__rbMegaChainSetting = !!s.mega_chain_mode;
    // Refresh the chain-input drive cache too — picks up any change the
    // user made via Settings (or via a direct settings POST in DevTools).
    if (typeof s.nam_chain_input_drive === 'number') {
        window.__rbChainInputDrive = s.nam_chain_input_drive;
    }
    // Chain volume trim (user cab/chain makeup). Default 4.0.
    window.__rbChainMakeup = (typeof s.chain_makeup === 'number') ? s.chain_makeup : 4.0;
    const cmSlider = document.getElementById('rb-chain-makeup');
    const cmVal = document.getElementById('rb-chain-makeup-val');
    if (cmSlider) cmSlider.value = String(window.__rbChainMakeup);
    if (cmVal) cmVal.textContent = window.__rbChainMakeup.toFixed(2) + '×';
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
        const r = await fetch(`${RB_API}/oauth/start?origin=${encodeURIComponent(origin)}`);
        const d = await r.json();
        if (!d.authorize_url) throw new Error('no authorize URL');
        window.open(d.authorize_url, '_blank');  // → system browser
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
        const r = await fetch(`${RB_API}/oauth/status`);
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
    rbLoadSettings();
    rbInit();
}

async function rbSaveSettings() {
    const megaCb = document.getElementById('rb-mega-chain-mode');
    const mega_chain_mode = megaCb ? !!megaCb.checked : false;
    const bac = document.getElementById('rb-bypass-all-cabs');
    const bypass_all_cabs = bac ? !!bac.checked : false;
    await fetch(`${RB_API}/settings`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ mega_chain_mode, bypass_all_cabs }),
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
        await fetch(`${RB_API}/settings`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ curated_only: !checked }),
        });
    } catch (e) { /* best-effort */ }
}

// Open a native file picker (Electron desktop bridge) and drop the chosen
// path into the given text input. Falls back to manual entry when there's
// no desktop bridge (e.g. running in a plain browser).
async function rbBrowseForPsarc(inputId) {
    const el = document.getElementById(inputId);
    const picker = window.slopsmithDesktop && window.slopsmithDesktop.pickFile;
    if (!picker) {
        if (el) el.focus();
        return;
    }
    try {
        const path = await window.slopsmithDesktop.pickFile([
            { name: 'Rocksmith gear archive', extensions: ['psarc'] },
            { name: 'All Files', extensions: ['*'] },
        ]);
        if (path && el) el.value = path;  // null = user cancelled
    } catch (e) {
        console.error('[rig_builder] file picker failed:', e);
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
    try {
        const r = await fetch(`${RB_API}/download_for_gear`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ rs_gear: rsGear, tone3000_id: toneId }),
            signal: ctrl.signal,
        });
        const data = await r.json();
        if (!r.ok) {
            btn.textContent = 'Failed';
            btn.classList.add('bg-red-700');
            alert(data.error || `HTTP ${r.status}`);
            return;
        }
        const assignedNote = data.presets_updated
            ? ` (${data.presets_updated} preset${data.presets_updated === 1 ? '' : 's'})`
            : '';
        btn.textContent = `✓ assigned${assignedNote}`;
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

// Combined "Extract everything" — runs the 3 PSARC extractors back to
// back against ONE gears.psarc: rebuild rs_to_real.json (gear map),
// pull every amp/pedal/rack/cab PNG, then the cab IRs. Steps 2 and 3
// are tolerant of soft failures (e.g. Pillow missing for photos) so a
// partial setup isn't fatal — the user still gets a usable gear map +
// IRs, and the catalog falls back to placeholders.
// Distill a useful one-line reason out of an extractor's failure payload.
// The backend (extract_gear_map / extract_gear_photos / extract_irs) returns
// `{error: "extractor failed", stderr: "...", stdout_tail: "..."}` on a
// non-zero subprocess exit, but the script's ACTUAL reason (e.g.
// "error: Pillow not installed", an ImportError, a traceback) lives in
// stderr. Surface the most informative line so the user/tester sees WHY
// instead of a generic "extractor failed", and dump the full stderr to the
// console for deeper debugging.
function rbExtractErrDetail(data, fallback) {
    const base = (data && data.error) ? String(data.error) : String(fallback);
    const stderr = (data && typeof data.stderr === 'string') ? data.stderr.trim() : '';
    const stdoutTail = (data && typeof data.stdout_tail === 'string') ? data.stdout_tail.trim() : '';
    let detail = '';
    if (stderr) {
        const lines = stderr.split('\n').map(s => s.trim()).filter(Boolean);
        // Prefer an explicit error/exception line (scan from the end — the
        // real cause is usually the last such line); else fall back to the
        // very last non-empty stderr line.
        const reversed = [...lines].reverse();
        detail = reversed.find(l =>
            /error|exception|traceback|not installed|no module|importerror|modulenotfound|permission|not found/i.test(l)
        ) || lines[lines.length - 1] || '';
        // Full stderr to the console so a tester can copy/paste it to us.
        console.error('[rig_builder extractor stderr]\n' + stderr);
    }
    if (!detail && stdoutTail) {
        const lines = stdoutTail.split('\n').map(s => s.trim()).filter(Boolean);
        detail = lines[lines.length - 1] || '';
    }
    if (!detail) return base;
    // Cap so a stray long line / traceback frame doesn't blow up the layout.
    if (detail.length > 300) detail = detail.slice(0, 300) + '…';
    return `${base}: ${detail}`;
}

async function rbExtractAll() {
    const path = document.getElementById('rb-all-psarc').value.trim();
    if (!path) return;
    const status = document.getElementById('rb-extract-all-status');
    status.textContent = 'Step 1/3: rebuilding gear map…';
    try {
        let r = await fetch(`${RB_API}/extract_gear_map`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ gears_psarc: path }),
        });
        let data = await r.json();
        if (!r.ok) {
            status.innerHTML = `<span class="text-red-400">Gear map failed: ${rbEsc(rbExtractErrDetail(data, r.status))} — is this really gears.psarc?</span>`;
            return;
        }
        const gearCount = data.count;

        // Step 2 — gear photos. Soft failure: a missing Pillow leaves
        // the catalog using placeholders, which is still useful.
        status.innerHTML = `<span class="text-gray-400">Gear map: ${gearCount} entries. Step 2/3: extracting gear photos (~10-20s)…</span>`;
        let photosNote = '';
        try {
            r = await fetch(`${RB_API}/extract_gear_photos`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ gears_psarc: path }),
            });
            const photoData = await r.json();
            if (!r.ok) {
                photosNote = ` <span class="text-yellow-400">(photos skipped: ${rbEsc(rbExtractErrDetail(photoData, r.status))})</span>`;
            } else {
                photosNote = ` <span class="text-gray-500">(photos: ${photoData.total} PNGs)</span>`;
            }
        } catch (e) {
            photosNote = ` <span class="text-yellow-400">(photos skipped: ${rbEsc(e.message || e)})</span>`;
        }

        // Step 3 — cab IRs.
        status.innerHTML = `<span class="text-gray-400">Gear map: ${gearCount}${photosNote}. Step 3/3: extracting cab IRs (30-60s)…</span>`;
        r = await fetch(`${RB_API}/extract_irs`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ gears_psarc: path }),
        });
        data = await r.json();
        if (!r.ok) {
            status.innerHTML = `<span class="text-yellow-400">Gear map OK (${gearCount})${photosNote}, but IR extraction failed: ${rbEsc(rbExtractErrDetail(data, r.status))}</span>`;
            return;
        }
        status.innerHTML = `<span class="text-green-400">Done: ${gearCount} gear entries${photosNote} + ${data.count} cabs with IR. Reloading…</span>`;
        rbInit();
    } catch (e) {
        status.innerHTML = `<span class="text-red-400">${rbEsc(e.message)}</span>`;
    }
}

// NOTE: rbNormalizeRsIrs (the "Normalize existing Rocksmith IRs"
// button) was removed from the Settings UI because peak-normalising
// the WAV samples didn't change the audible level — the engine
// ignores the per-stage IR `gain` we tried to write, AND it doesn't
// have a peak-triggered limiter that the over-unity WEM samples were
// activating either. The real fix lives in the Cab makeup gain
// slider, which goes through `setGain('chain', X)` (the only knob
// the engine actually respects).
//
// The backend `/normalize_rocksmith_irs` endpoint and the
// `_peak_normalize_float32` step inside extract_irs.py are kept —
// they put the IRs into a standard ±1.0 float32 range, which is
// good hygiene even if it doesn't move audible level.

async function rbExportDefaults() {
    const status = document.getElementById('rb-export-defaults-status');
    status.textContent = 'Exporting…';
    try {
        const r = await fetch(`${RB_API}/export_default_captures`, { method: 'POST' });
        const data = await r.json();
        if (!r.ok) {
            status.innerHTML = `<span class="text-red-400">Error: ${rbEsc(data.error || r.status)}</span>`;
            return;
        }
        status.innerHTML = `<span class="text-green-400">Saved ${data.count} gear → capture defaults to default_captures.json.</span>`;
    } catch (e) {
        status.innerHTML = `<span class="text-red-400">${rbEsc(e.message)}</span>`;
    }
}

// (rbRemapCabMics removed — _auto_fix_cab_mics_for_song now runs on
// every /song fetch, so cab assignments self-heal at song-open time
// without the user needing to visit Setup.)
