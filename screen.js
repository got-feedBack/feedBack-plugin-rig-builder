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
            } catch (_) {
                return origFetch(input, init);
            }
            return new Response(txt, { status: 200, headers: { 'Content-Type': 'application/json' } });
        }).catch(() => origFetch(input, init));               // any error → original
    };
})();

// ── Shared state ────────────────────────────────────────────────────

let rbState = {
    status: null,
    songTones: null,        // currently inspected song
    batchPoll: null,        // setInterval handle while batch is running
    currentTab: 'dashboard',
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

const RB_API = '/api/plugins/rig_builder';
const NAM_API = '/api/plugins/nam_tone';

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
    const apiLine = s.has_tone3000_key
        ? (s.tone3000_api_works
            ? '<span class="text-green-400">tone3000 API connected</span>'
            : '<span class="text-red-400">tone3000 API key invalid</span>')
        : '<span class="text-gray-500">no tone3000 API key (deep-link mode)</span>';
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

    if (name === 'dashboard') rbLoadCoverage();
    if (name === 'pending') rbLoadPending();
    if (name === 'gear') rbLoadCatalog();
    if (name === 'master') rbLoadMasterChain();
    if (name === 'settings') { rbLoadSettings(); rbUpdateScanStatus(); }
}

// ── Dashboard: coverage stats ──────────────────────────────────────

async function rbLoadCoverage() {
    const el = document.getElementById('rb-gear-coverage');
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
    try {
        el.innerHTML = data.tones.map((t, idx) => rbRenderTone(t, idx, filename)).join('');
    } catch (e) {
        // Never leave the panel stuck on "Loading…" if a render throws.
        console.error('[rig_builder] render of tones failed', e);
        el.innerHTML = `<p class="text-red-400">Error rendering tones: ${rbEsc(e.message)}</p>`;
        return;
    }

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
            refreshed.tones.forEach((t, idx) => {
                const wrap = document.createElement('div');
                wrap.innerHTML = rbRenderTone(t, idx, filename);
                container.appendChild(wrap.firstElementChild);
            });
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

function rbRenderTone(tone, toneIdx, filename) {
    const pieces = tone.chain.map((p, pIdx) => rbRenderPiece(p, toneIdx, pIdx)).join('');
    // Badge that flags whether this chain has been edited (saved preset_pieces
    // overriding PSARC) vs still the original PSARC default.
    const sourceBadge = tone.chain_source === 'edited'
        ? `<span class="text-[10px] text-purple-300/80 bg-purple-900/20 border border-purple-800/30 rounded px-1.5 py-0.5"
                  title="This tone's chain has been edited from the PSARC default">✎ edited</span>`
        : `<span class="text-[10px] text-gray-500" title="Untouched — pieces still match the PSARC's GearList">PSARC default</span>`;
    return `
        <div class="bg-dark-700/50 border border-gray-800/50 rounded-xl p-4">
            <div class="flex items-baseline justify-between mb-3">
                <h3 class="text-white font-semibold">${rbEsc(tone.name)}</h3>
                <div class="flex items-center gap-2">
                    ${sourceBadge}
                    <span class="text-xs text-gray-500">${rbEsc(tone.key)}</span>
                </div>
            </div>
            <div class="text-[10px] text-gray-500 mb-2">
                Signal flow: ${tone.chain.length} stage${tone.chain.length === 1 ? '' : 's'} ·
                drag through ▲ ▼ to reorder, ✗ to remove
            </div>
            <div id="rb-chain-${toneIdx}" class="grid grid-cols-1 md:grid-cols-2 gap-3 mb-3">${pieces}</div>
            <div class="flex justify-between items-center gap-2">
                <button onclick="rbOpenAddPiecePicker(${toneIdx}, '${rbEsc(filename).replace(/'/g,"\\'")}')"
                        title="Insert a new gear at the end of this tone's chain"
                        class="bg-emerald-900/30 hover:bg-emerald-900/50 text-emerald-300 border border-emerald-800/40 px-3 py-1.5 rounded text-xs transition">
                    ＋ Add piece to chain
                </button>
                <div class="flex gap-2">
                    <button id="rb-listen-${toneIdx}"
                            onclick="rbListenTone(${toneIdx}, '${rbEsc(filename).replace(/'/g,"\\'")}')"
                            title="Saves the tone and plays it live through the NAM engine (monitors your guitar input)"
                            class="bg-dark-600 hover:bg-dark-500 text-gray-200 px-4 py-2 rounded-lg text-xs transition">
                        ▶ Listen
                    </button>
                    <button onclick="rbSaveTonePreset(${toneIdx}, '${rbEsc(filename).replace(/'/g,"\\'")}')"
                            class="bg-accent hover:bg-accent/80 text-white px-4 py-2 rounded-lg text-xs transition">
                        Save preset
                    </button>
                </div>
            </div>
            <div id="rb-addpiece-modal-${toneIdx}" class="hidden mt-3 bg-emerald-900/10 border border-emerald-800/30 rounded p-3"></div>
        </div>`;
}

// Re-render JUST the chain grid for one tone after a reorder / add / remove
// (avoids a full song refetch). Position numbers, slot badges, and the
// chain-length copy all come from the live tone.chain so it's enough to
// rebuild the inner grid.
function rbReRenderToneChain(toneIdx, filename) {
    const tone = rbState.songTones.tones[toneIdx];
    if (!tone) return;
    const grid = document.getElementById(`rb-chain-${toneIdx}`);
    if (grid) {
        grid.innerHTML = tone.chain.map((p, pIdx) => rbRenderPiece(p, toneIdx, pIdx)).join('');
    }
    // Update the stages count copy too.
    const headerCopy = grid?.previousElementSibling;
    if (headerCopy) {
        headerCopy.innerHTML = `Signal flow: ${tone.chain.length} stage${tone.chain.length === 1 ? '' : 's'} · drag through ▲ ▼ to reorder, ✗ to remove`;
    }
}

function rbRenderPiece(p, toneIdx, pIdx) {
    const isCab = p.rs_category === 'cab';
    const acceptExt = isCab ? '.wav' : '.nam';
    // Resolve the *effective* current assignment, preferring in-memory
    // pending changes over what's persisted in the DB. A piece can be:
    //   - VST   (kind=vst, vst_path set)
    //   - NAM   (kind=nam, file set)
    //   - IR    (kind=ir|rs_ir, file set)
    //   - empty (unassigned)
    const pendingKind = p._uploaded_kind || p._vst_kind;
    const assignedKind = p.assigned && p.assigned.kind;
    const effKind = pendingKind || assignedKind || (p.rs_category === 'cab' ? 'ir' : 'nam');
    const effVstPath = p._vst_path || (p.assigned && p.assigned.vst_path) || '';
    const effVstFormat = p._vst_format || (p.assigned && p.assigned.vst_format) || 'VST3';
    const effFile = p._uploaded_file || (p.assigned && p.assigned.file) || null;
    const hasVst = effKind === 'vst' && !!effVstPath;
    const hasFile = !hasVst && !!effFile;
    const mode = (p.assigned && p.assigned.assigned_mode) || (p._uploaded_file ? 'manual' : '');
    let stageLabel, stageClass;
    if (hasVst) {
        // Show just the filename of the VST bundle (e.g. "TAL-Chorus-LX.vst3"),
        // not the absolute path.
        const vstName = effVstPath.split('/').pop();
        stageLabel = `✓ VST: ${vstName}`;
        stageClass = 'text-purple-300';
    } else if (hasFile) {
        // Prefer the human tone3000 title over the technical
        // tone3000_<id>_m<model>_<rs_gear> filename, but only when we're
        // showing the assigned capture (a manual upload keeps its filename).
        const a = p.assigned;
        const title = (!p._uploaded_file && a && a.file === effFile && a.tone3000_title) ? a.tone3000_title : '';
        stageLabel = `✓ ${title || rbLibShortName(effFile)}`;
        stageClass = 'text-green-400';
    } else {
        stageLabel = '(unassigned)';
        stageClass = 'text-gray-500';
    }
    const bypassed = !!p._bypassed;

    // For cab pieces we may have one or more Rocksmith-extracted IRs
    // available locally (no download needed). When present, surface a
    // one-click select with a dropdown for the mic-position variants.
    const rsIrs = p.rs_irs || [];
    let rsIrControl = '';
    if (rsIrs.length > 0) {
        const options = rsIrs.map((f, i) => `<option value="${rbEsc(f)}">${rbEsc(f.split('/').pop())}</option>`).join('');
        rsIrControl = `
            <div class="flex items-center gap-2 mt-2 bg-green-900/15 border border-green-800/30 rounded px-2 py-1.5">
                <span class="text-xs text-green-400 whitespace-nowrap">Rocksmith IR (${rsIrs.length}):</span>
                <select onchange="rbPickRsIr(this, ${toneIdx}, ${pIdx})"
                        class="flex-1 bg-dark-800 border border-gray-800 rounded text-xs text-gray-300 px-1 py-0.5">${options}</select>
                <button onclick="rbAssignRsIr(this, ${toneIdx}, ${pIdx})"
                        class="bg-green-700 hover:bg-green-600 text-white text-xs px-2 py-0.5 rounded">Use</button>
            </div>`;
    }

    // Rocksmith knob configuration for this piece — the values the in-game
    // tone uses for this gear (e.g. Pedal_Chorus20 with Rate=50 Depth=30 Mix=70).
    // Shown read-only so the user can either replicate manually in the VST
    // editor or click "Apply RS settings" when a translation table exists.
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
            <div class="mt-2 pt-2 border-t border-gray-800/40">
                <div class="text-[10px] text-gray-500 mb-1">Rocksmith settings:</div>
                <div class="flex flex-wrap">${pairs}</div>
            </div>`;
    }

    // Chain editor controls (auto-save on click): position number + ▲ ▼ ✗
    const total = (rbState.songTones && rbState.songTones.tones[toneIdx] && rbState.songTones.tones[toneIdx].chain.length) || 1;
    const isFirst = pIdx === 0;
    const isLast  = pIdx === total - 1;
    return `
        <div class="bg-dark-800 border border-gray-800/50 rounded-lg p-3" data-tone="${toneIdx}" data-piece="${pIdx}">
            <div class="flex items-center justify-between mb-2">
                <div class="flex items-center gap-2 min-w-0">
                    <span class="flex-shrink-0 w-6 h-6 rounded-full bg-dark-900 border border-gray-700 text-[11px] text-gray-300 flex items-center justify-center font-mono"
                          title="Position in the signal flow (1 = first, N = last before output)">${pIdx + 1}</span>
                    <div class="min-w-0">
                        <div class="text-sm text-gray-200 truncate">${rbEsc(p.real_name || p.type)}</div>
                        <div class="text-xs text-gray-500 truncate">
                            ${rbEsc(p.slot)} · ${rbEsc(p.rs_category)} · ${rbEsc(p.type)}
                        </div>
                    </div>
                </div>
                <div class="flex items-center gap-1 flex-shrink-0">
                    <button onclick="rbMovePiece(${toneIdx}, ${pIdx}, -1)"
                            title="Move earlier in the signal flow"
                            ${isFirst ? 'disabled' : ''}
                            class="px-1.5 py-1 rounded text-xs transition ${isFirst ? 'bg-dark-700/40 text-gray-700 cursor-not-allowed' : 'bg-dark-600 hover:bg-dark-500 text-gray-300'}">▲</button>
                    <button onclick="rbMovePiece(${toneIdx}, ${pIdx}, 1)"
                            title="Move later in the signal flow"
                            ${isLast ? 'disabled' : ''}
                            class="px-1.5 py-1 rounded text-xs transition ${isLast ? 'bg-dark-700/40 text-gray-700 cursor-not-allowed' : 'bg-dark-600 hover:bg-dark-500 text-gray-300'}">▼</button>
                    <button onclick="rbRemovePiece(${toneIdx}, ${pIdx})"
                            title="Remove this piece from the chain (the rs_to_real entry stays — you can re-add later)"
                            class="px-1.5 py-1 rounded text-xs bg-red-900/40 hover:bg-red-900/60 text-red-300 border border-red-800/40 transition">✗</button>
                    <button id="rb-bypass-${toneIdx}-${pIdx}" onclick="rbToggleBypass(${toneIdx}, ${pIdx}, this)"
                            title="Bypass: skips this stage in the preview (signal passes through unprocessed — it isn't muted, the chain keeps working)"
                            class="px-2 py-1 rounded text-xs transition ${bypassed ? 'bg-amber-700/40 text-amber-300 border border-amber-600/40' : 'bg-dark-600 hover:bg-dark-500 text-gray-300'}">
                        ${bypassed ? '⤳ Bypassed' : 'Bypass'}
                    </button>
                    <button onclick="rbOpenSuggest('${rbEsc(p.type)}')"
                            class="bg-dark-600 hover:bg-dark-500 text-gray-200 px-2 py-1 rounded text-xs transition">
                        Suggest
                    </button>
                </div>
            </div>
            <div class="flex items-center gap-2">
                <input type="file" accept="${acceptExt}"
                       onchange="rbUploadFile(this, ${toneIdx}, ${pIdx})"
                       class="text-xs text-gray-500 file:bg-dark-700 file:border-0 file:text-gray-300 file:px-2 file:py-1 file:rounded file:text-xs file:cursor-pointer">
                <button onclick="rbToggleLibraryPicker(${toneIdx}, ${pIdx})"
                        title="Pick from your downloaded ${isCab ? 'IRs' : 'NAMs'}"
                        class="bg-indigo-900/30 hover:bg-indigo-900/50 text-indigo-300 border border-indigo-800/40 px-2 py-1 rounded text-xs">
                    📚 Library
                </button>
                ${hasVst ? `
                <button onclick="rbToneEditVst(${toneIdx}, ${pIdx})"
                        title="Load this VST in the engine and edit its parameters with inline sliders"
                        class="bg-purple-900/30 hover:bg-purple-900/50 text-purple-300 border border-purple-800/40 px-2 py-1 rounded text-xs">
                    🎛 Edit VST
                </button>` : ''}
                <span class="rb-piece-file text-xs ${stageClass} truncate" title="${rbEsc(hasVst ? effVstPath : (hasFile ? effFile : ''))}">${rbEsc(stageLabel)}</span>
                ${(hasFile || hasVst) && mode ? `<span class="text-[10px] text-gray-600 whitespace-nowrap">(${rbEsc(mode)})</span>` : ''}
            </div>
            <div id="rb-lib-${toneIdx}-${pIdx}" class="hidden mt-2 bg-indigo-900/10 border border-indigo-800/30 rounded p-2"></div>
            <div id="rb-tone-vst-editor-${toneIdx}-${pIdx}" class="hidden mt-2 bg-purple-900/10 border border-purple-800/30 rounded p-2 space-y-2"></div>
            ${rsKnobsBlock}
            ${rsIrControl}
        </div>`;
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
        return !/^midi\s*cc\b/i.test(n);
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
    const vstPath = piece._vst_path || (piece.assigned && piece.assigned.vst_path) || '';
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
        // Re-apply previously captured params if any.
        const saved = piece._vst_params
            || (piece.assigned && piece.assigned.vst_state
                ? rbParseVstStateParams(piece.assigned.vst_state) : null);
        if (saved && typeof api.setParameter === 'function') {
            for (const [pid, v] of Object.entries(saved)) {
                try { await api.setParameter(slotId, parseInt(pid, 10), parseFloat(v)); } catch (_) {}
            }
        }
        let params = [];
        if (typeof api.getParameters === 'function') {
            try {
                const raw = await api.getParameters(slotId);
                if (Array.isArray(raw)) params = raw;
            } catch (_) {}
        }
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
        if (api.openPluginEditor) {
            api.openPluginEditor(slotId).catch(() => {});
        }
        rbToneRenderInlineVstParams(toneIdx, pIdx);
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
    const effVstPath = piece._vst_path || (piece.assigned && piece.assigned.vst_path) || '';
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

// ── Local library picker (pick from already-downloaded NAMs / IRs) ────

// Open/close the per-piece library picker. Loads the file list on first
// open and caches it; the dropdown then renders client-side filtering.
async function rbToggleLibraryPicker(toneIdx, pIdx) {
    const el = document.getElementById(`rb-lib-${toneIdx}-${pIdx}`);
    if (!el) return;
    el.classList.toggle('hidden');
    if (el.classList.contains('hidden')) return;
    if (el.dataset.built === '1') return;
    el.dataset.built = '1';
    const fileLabel = rbState.songTones.tones[toneIdx].chain[pIdx].rs_category === 'cab' ? 'IRs' : 'NAMs';
    el.innerHTML = `
        <div class="flex items-center gap-1 mb-2 border-b border-gray-800">
            <button id="rb-lib-tab-files-${toneIdx}-${pIdx}" onclick="rbLibTab(${toneIdx}, ${pIdx}, 'files')"
                    class="px-3 py-1 text-xs border-b-2">📚 ${fileLabel}</button>
            <button id="rb-lib-tab-plugins-${toneIdx}-${pIdx}" onclick="rbLibTab(${toneIdx}, ${pIdx}, 'plugins')"
                    class="px-3 py-1 text-xs border-b-2">🎛 Plugins</button>
        </div>
        <div id="rb-lib-content-${toneIdx}-${pIdx}"></div>`;
    rbLibTab(toneIdx, pIdx, 'files');
}

// Switch the per-piece library picker between local NAM/IR files and the
// scanned VST/AU plugins. The Plugins tab reuses the full VST panel (search,
// category groups, hide-instruments, assign, param editor). Files are loaded
// once and cached on the outer element so the filter can re-render quickly.
async function rbLibTab(toneIdx, pIdx, tab) {
    const el = document.getElementById(`rb-lib-${toneIdx}-${pIdx}`);
    const content = document.getElementById(`rb-lib-content-${toneIdx}-${pIdx}`);
    if (!el || !content) return;
    const piece = rbState.songTones.tones[toneIdx].chain[pIdx];
    for (const t of ['files', 'plugins']) {
        const b = document.getElementById(`rb-lib-tab-${t}-${toneIdx}-${pIdx}`);
        if (b) {
            const on = t === tab;
            b.classList.toggle('border-indigo-400', on);
            b.classList.toggle('text-indigo-300', on);
            b.classList.toggle('border-transparent', !on);
            b.classList.toggle('text-gray-400', !on);
        }
    }
    if (tab === 'plugins') {
        const cur = piece._vst_staged_path || (piece.assigned && piece.assigned.vst_path) || '';
        const fmt = piece._vst_format || (piece.assigned && piece.assigned.vst_format) || 'VST3';
        content.innerHTML = rbRenderVstPanelBody(toneIdx, pIdx, cur, fmt);
        return;
    }
    const kind = piece.rs_category === 'cab' ? 'ir' : 'nam';
    if (el.dataset.filesLoaded !== '1') {
        content.innerHTML = `<div class="text-xs text-gray-500">loading library…</div>`;
        try {
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
    rbRenderLibraryList(content, el._rbAllFiles, toneIdx, pIdx, kind, '');
}

// Initial render of the library picker: lays out the (stable) header
// with the search input + count badge + the rows container. The input is
// never re-created after this, so typing doesn't lose focus.
function rbRenderLibraryList(container, files, toneIdx, pIdx, kind, filter) {
    const inputId = `rb-lib-search-${toneIdx}-${pIdx}`;
    const countId = `rb-lib-count-${toneIdx}-${pIdx}`;
    const rowsId  = `rb-lib-rows-${toneIdx}-${pIdx}`;
    container.innerHTML = `
        <div class="flex items-center gap-2 mb-2">
            <input id="${inputId}" type="text" placeholder="🔍 Filter ${kind === 'ir' ? 'IRs' : 'NAMs'}…"
                   oninput="rbFilterLibrary(${toneIdx}, ${pIdx})"
                   value="${rbEsc(filter || '')}"
                   class="flex-1 bg-dark-800 border border-gray-800 rounded text-[11px] text-gray-200 px-2 py-1">
            <span id="${countId}" class="text-[10px] text-gray-500">${files.length}/${files.length}</span>
        </div>
        <div id="${rowsId}" class="max-h-64 overflow-y-auto"></div>`;
    rbRenderLibraryRows(container, files, toneIdx, pIdx, kind, filter);
}

// Inner-only render: refreshes the rows + count badge based on the current
// filter, but leaves the search <input> alone so focus + cursor position
// survive every keystroke. Called both on initial paint and on every
// oninput event.
// Disambiguate library rows that share a tone3000 title (several captures
// can all be called "EQ"): show the title, and append the technical filename
// in muted text only when the title alone is ambiguous in the shown list.
// Short, readable form of a downloaded filename: drop the
// tone3000_<id>_m<model>_ prefix and the extension, leaving the descriptive
// tail (the Rocksmith gear), e.g. "tone3000_31843_m146073_Rack_StudioEQ.nam"
// -> "Rack_StudioEQ". Non-tone3000 files just lose their extension.
function rbLibShortName(name) {
    const base = String(name || '').replace(/\.[^./]+$/, '');
    const m = base.match(/^tone3000_\d+_m\d+_(.+)$/);
    return m ? m[1] : base;
}

function rbLibLabel(file, titleCounts) {
    const t = file.title;
    const short = rbLibShortName(file.name);
    if (!t) return rbEsc(short);
    if ((titleCounts[t] || 0) > 1) {
        return `${rbEsc(t)} <span class="text-gray-500">· ${rbEsc(short)}</span>`;
    }
    return rbEsc(t);
}

function rbRenderLibraryRows(container, files, toneIdx, pIdx, kind, filter) {
    const rowsEl  = document.getElementById(`rb-lib-rows-${toneIdx}-${pIdx}`);
    const countEl = document.getElementById(`rb-lib-count-${toneIdx}-${pIdx}`);
    if (!rowsEl) return;
    const f = (filter || '').toLowerCase().trim();
    const filtered = f
        ? files.filter(x => (x.name + ' ' + (x.title || '')).toLowerCase().includes(f))
        : files;
    const titleCounts = {};
    filtered.forEach(x => { if (x.title) titleCounts[x.title] = (titleCounts[x.title] || 0) + 1; });
    const rows = filtered.slice(0, 50).map(file => {
        const usedFor = (file.used_for_gears || []).slice(0, 2).join(', ');
        const usedBadge = file.use_count > 0
            ? `<span class="text-[10px] text-amber-300/80" title="${rbEsc(usedFor)}">used ${file.use_count}×</span>`
            : `<span class="text-[10px] text-gray-600">unused</span>`;
        const safeName = file.name.replace(/'/g, "\\'");
        return `
            <div class="flex items-center gap-2 px-2 py-1 hover:bg-indigo-900/20 rounded cursor-pointer"
                 onclick="rbPickFromLibrary(${toneIdx}, ${pIdx}, '${rbEsc(safeName)}', '${rbEsc(kind)}')">
                <span class="flex-1 text-[11px] text-gray-200 truncate" title="${rbEsc(file.name)}">${rbLibLabel(file, titleCounts)}</span>
                ${usedBadge}
                <button onclick="event.stopPropagation(); rbAuditionFile('${rbEsc(safeName)}', '${rbEsc(kind === 'ir' ? 'ir' : 'nam')}', null)"
                        title="Audition in isolation"
                        class="text-[10px] text-gray-400 hover:text-gray-200 px-1">▶</button>
            </div>`;
    }).join('');
    const moreNote = filtered.length > 50
        ? `<div class="text-[10px] text-gray-500 italic mt-1">…and ${filtered.length - 50} more (refine search)</div>`
        : '';
    rowsEl.innerHTML = (rows || '<div class="text-xs text-gray-500 italic">no matches</div>') + moreNote;
    if (countEl) countEl.textContent = `${filtered.length}/${files.length}`;
}

function rbFilterLibrary(toneIdx, pIdx) {
    const container = document.getElementById(`rb-lib-${toneIdx}-${pIdx}`);
    if (!container || !container._rbAllFiles) return;
    const input = document.getElementById(`rb-lib-search-${toneIdx}-${pIdx}`);
    rbRenderLibraryRows(container, container._rbAllFiles, toneIdx, pIdx,
                        container.dataset.kind || 'nam', input ? input.value : '');
}

// Apply a chosen file from the library to this piece. Mirrors the upload
// flow: set _uploaded_file + _uploaded_kind, re-render, re-audition.
function rbPickFromLibrary(toneIdx, pIdx, fileName, kind) {
    const piece = rbState.songTones.tones[toneIdx].chain[pIdx];
    piece._uploaded_file = fileName;
    piece._uploaded_kind = kind;
    // Picking from local library = drop any pending VST assignment so
    // the NAM/IR takes priority (kind precedence is in rbPersistTone).
    piece._vst_path = null;
    piece._vst_format = null;
    piece._vst_kind = null;
    piece._vst_state = null;
    // Collapse the picker so the song-list isn't covered after the click.
    const lib = document.getElementById(`rb-lib-${toneIdx}-${pIdx}`);
    if (lib) lib.classList.add('hidden');
    rbAfterGearChange(toneIdx);
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
    const effVstPath = p._vst_path || (p.assigned && p.assigned.vst_path) || '';
    const effFile = p._uploaded_file || (p.assigned && p.assigned.file) || null;
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
                vst_path: p._vst_path || (p.assigned && p.assigned.vst_path) || '',
                vst_format: p._vst_format || (p.assigned && p.assigned.vst_format) || 'VST3',
                vst_state: p._vst_state ?? (p.assigned && p.assigned.vst_state) ?? null,
                params: {},
                assigned_mode: 'master',
                bypassed: !!p._bypassed,
            };
        }
        const file = p._uploaded_file || (p.assigned && p.assigned.file) || null;
        const kindRaw = p._uploaded_kind || (p.assigned && p.assigned.kind) || null;
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
    const vstPath = piece._vst_path || (piece.assigned && piece.assigned.vst_path) || '';
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
        // Re-apply any previously-captured param state.
        const saved = piece._vst_params
            || (piece.assigned && piece.assigned.vst_state
                ? rbParseVstStateParams(piece.assigned.vst_state) : null);
        if (saved && typeof api.setParameter === 'function') {
            for (const [pid, v] of Object.entries(saved)) {
                try { await api.setParameter(slotId, parseInt(pid, 10), parseFloat(v)); } catch (_) {}
            }
        }
        // Grab the live param list (after the restore so values reflect it).
        let params = [];
        if (typeof api.getParameters === 'function') {
            try {
                const raw = await api.getParameters(slotId);
                if (Array.isArray(raw)) params = raw;
            } catch (_) {}
        }
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
        // Open the plugin's own editor window too as an optional visual
        // — the inline sliders still drive everything.
        if (api.openPluginEditor) {
            api.openPluginEditor(slotId).catch(() => {});
        }
        rbMasterRenderInlineVstParams(role, idx);
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
    for (const [rsKnobName, rule] of Object.entries(mapping)) {
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
function rbUpdatePathFromInput(toneIdx, pIdx, path) {
    rbStagePath(toneIdx, pIdx, path);
    const sel = document.getElementById(`rb-vst-selected-${toneIdx}-${pIdx}`);
    if (sel) {
        const name = (path || '').split('/').pop() || '(none selected)';
        sel.textContent = `Selected: ${name}`;
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
            const cur = piece._vst_path || (piece.assigned && piece.assigned.vst_path) || '';
            const fmt = piece._vst_format || (piece.assigned && piece.assigned.vst_format) || 'VST3';
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
        let params = [];
        if (typeof api.getParameters === 'function') {
            try {
                const raw = await api.getParameters(slotId);
                if (Array.isArray(raw)) params = raw;
            } catch (e) {
                console.warn('[rig_builder] getParameters failed:', e);
            }
        }
        piece._vst_param_meta = params;
        // If we have previously-captured param values, re-apply them so the
        // editor opens with the user's saved tweaks instead of plugin defaults.
        const savedParams = piece._vst_params || (piece.assigned && piece.assigned.vst_state
            ? rbParseVstStateParams(piece.assigned.vst_state) : null);
        if (savedParams && typeof api.setParameter === 'function') {
            for (const [pid, v] of Object.entries(savedParams)) {
                try { await api.setParameter(slotId, parseInt(pid, 10), parseFloat(v)); } catch (_) {}
            }
            // Re-query to reflect the restored values.
            if (typeof api.getParameters === 'function') {
                try {
                    const refreshed = await api.getParameters(slotId);
                    if (Array.isArray(refreshed)) piece._vst_param_meta = refreshed;
                } catch (_) {}
            }
        }
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
    if (typeof api.getChainState !== 'function' || typeof api.setParameter !== 'function') return;
    let loaded;
    try { loaded = await api.getChainState(); } catch (_) { return; }
    if (!Array.isArray(loaded)) return;
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
        for (const [pid, v] of Object.entries(params)) {
            try {
                await api.setParameter(slotId, parseInt(pid, 10), parseFloat(v));
            } catch (e) {
                console.warn(`[rig_builder] setParameter slot=${slotId} param=${pid}:`, e);
            }
        }
    }
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
    // any live preview reloads.
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
                vst_path: p._vst_path || (p.assigned && p.assigned.vst_path) || '',
                vst_format: p._vst_format || (p.assigned && p.assigned.vst_format) || 'VST3',
                vst_state: p._vst_state ?? (p.assigned && p.assigned.vst_state) ?? null,
                params: p.knobs || {},
                assigned_mode: p._vst_kind ? 'manual_vst' : (p.assigned && p.assigned.assigned_mode) || 'manual_vst',
                bypassed: !!p._bypassed,
            };
        }
        const file = p._uploaded_file || (p.assigned && p.assigned.file) || null;
        const kindRaw = p._uploaded_kind || (p.assigned && p.assigned.kind) || null;
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
        if (b) { b.disabled = false; b.textContent = '▶'; }
    }
}

// ── Per-stage bypass (audition each piece in/out of the chain) ─────────
function rbUpdateBypassBtn(btn, on) {
    if (!btn) return;
    btn.textContent = on ? '⤳ Bypassed' : 'Bypass';
    btn.className = 'px-2 py-1 rounded text-xs transition ' + (on
        ? 'bg-amber-700/40 text-amber-300 border border-amber-600/40'
        : 'bg-dark-600 hover:bg-dark-500 text-gray-300');
}

function rbToggleBypass(toneIdx, pIdx, btn) {
    const piece = rbState.songTones.tones[toneIdx].chain[pIdx];
    piece._bypassed = !piece._bypassed;
    rbUpdateBypassBtn(btn, piece._bypassed);
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
    try {
        if (api.clearChain) await api.clearChain().catch(() => {});
        await api.loadPreset(JSON.stringify(payload.native_preset));
        // Engine sometimes leaves a slot bypassed across reloads — force each
        // slot's bypass to match the spec so toggling un-bypass actually un-bypasses.
        await rbReapplyBypassToChain(api, payload.native_preset.chain || []);
        if (api.setMonitorMute) await api.setMonitorMute(false).catch(() => {});
    } catch (e) { console.warn('[rig_builder] reload preview failed', e); }
}

// Re-render the open song's tone cards from current in-memory state (keeps
// _uploaded_file + _bypassed), restoring the active preview button label.
function rbRerenderSong() {
    const el = document.getElementById('rb-song-tones');
    if (!el || !rbState.songTones || !rbState.currentSongFile) return;
    el.innerHTML = rbState.songTones.tones
        .map((t, idx) => rbRenderTone(t, idx, rbState.currentSongFile)).join('');
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
async function rbAuditionFile(file, kind, btnId) {
    const btn = btnId ? document.getElementById(btnId) : null;
    if (rbState._auditionId === btnId) { await rbStopPreview(); return; }
    await rbStopPreview();   // stop any other preview/audition first
    const api = rbNativeAudio();
    if (!api) { alert('Audio engine unavailable. Open the “NAM” plugin once to initialize it.'); return; }
    if (btn) { btn.disabled = true; btn.textContent = '⏳'; }
    try {
        const url = `${RB_API}/native_preset_one?file=${encodeURIComponent(file)}&kind=${encodeURIComponent(kind || 'nam')}`;
        const payload = await (await fetch(url)).json();
        const chain = payload.native_preset && payload.native_preset.chain;
        if (!Array.isArray(chain) || !chain.length) throw new Error('file not found');
        if (api.clearChain) await api.clearChain().catch(() => {});
        const res = await api.loadPreset(JSON.stringify(payload.native_preset));
        if (!res || res.success === false) throw new Error((res && res.error) || 'loadPreset failed');
        if (api.setGain) { await api.setGain('input', 1.0).catch(() => {}); await api.setGain('chain', 1.0).catch(() => {}); }
        if (api.setMonitorMute) await api.setMonitorMute(false).catch(() => {});
        const wasRunning = api.isAudioRunning ? await api.isAudioRunning().catch(() => true) : true;
        await api.startAudio();
        rbState._previewStartedAudio = !wasRunning;
        rbState._previewMode = 'native';
        rbState._auditionId = btnId;
        if (btn) { btn.disabled = false; btn.textContent = '⏸'; }
    } catch (e) {
        if (btn) { btn.disabled = false; btn.textContent = '▶'; }
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
async function rbLoadCatalog() {
    const el = document.getElementById('rb-catalog');
    if (!el) return;
    if (rbState._auditionId) await rbStopPreview();   // stop stale audition before re-render
    el.innerHTML = '<p class="text-gray-500">Loading…</p>';
    let data;
    try { data = await (await fetch(`${RB_API}/gear_catalog`)).json(); }
    catch (e) { el.innerHTML = `<p class="text-red-400">Error: ${rbEsc(e.message)}</p>`; return; }
    const cats = (data && data.categories) || {};
    const keys = Object.keys(cats);
    if (!keys.length) {
        el.innerHTML = '<p class="text-gray-500">No gear yet. Map a song first (the “By song” tab or the Dashboard batch).</p>';
        return;
    }
    const LABEL = { amp: 'Amplifiers', pedal: 'Pedals', cab: 'Cabinets', rack: 'Racks', other: 'Other' };
    try {
        el.innerHTML = keys.map(cat => {
            const items = cats[cat] || [];
            const cards = items.map(g => rbRenderCatalogCard(g)).join('');
            return `
                <div>
                    <h3 class="text-white font-semibold mb-3">${rbEsc(LABEL[cat] || cat)} <span class="text-gray-500 text-xs">(${items.length})</span></h3>
                    <div class="grid grid-cols-1 md:grid-cols-2 gap-3">${cards}</div>
                </div>`;
        }).join('');
    } catch (e) {
        console.error('[rig_builder] catalog render failed', e);
        el.innerHTML = `<p class="text-red-400">Error rendering: ${rbEsc(e.message)}</p>`;
    }
}

function rbRenderCatalogCard(g) {
    const btnId = `rb-aud-${_rbCatalogSeq++}`;
    const img = g.image
        ? `<img src="${rbEsc(g.image)}" alt="" loading="lazy" class="w-14 h-14 rounded object-cover bg-dark-900 flex-shrink-0" onerror="this.replaceWith(Object.assign(document.createElement('div'),{className:'w-14 h-14 rounded bg-dark-900 flex-shrink-0'}))">`
        : `<div class="w-14 h-14 rounded bg-dark-900 flex items-center justify-center text-gray-700 text-[10px] flex-shrink-0">no photo</div>`;
    // VST takes priority over NAM/IR in the label/audition button: kind='vst'
    // with vst_path set means the user has explicitly chosen a plugin for
    // every preset using this gear.
    const isVst = g.kind === 'vst' && g.vst_path;
    let parent;
    if (isVst) {
        const vstName = g.vst_path.split('/').pop();
        parent = `<span class="text-purple-300" title="${rbEsc(g.vst_path)}">✓ VST: ${rbEsc(vstName)}</span>`;
    } else if (g.assigned) {
        parent = `<span class="text-green-400" title="${rbEsc(g.file || '')}">✓ ${rbEsc(g.tone3000_title || rbLibShortName(g.file) || 'assigned')}</span>`;
    } else {
        parent = `<span class="text-gray-500">(unassigned)</span>`;
    }
    let listenBtn = '';
    if (isVst) {
        listenBtn = `<button id="${btnId}" onclick="rbAuditionVst('${rbEsc(g.vst_path).replace(/'/g,"\\'")}','${rbEsc(g.vst_format || 'VST3')}','${btnId}')"
                            title="Listen to this VST in isolation" class="bg-purple-700/50 hover:bg-purple-600/60 text-purple-100 px-2.5 py-1 rounded text-xs flex-shrink-0">▶</button>`;
    } else if (g.assigned) {
        listenBtn = `<button id="${btnId}" onclick="rbAuditionFile('${rbEsc(g.file).replace(/'/g,"\\'")}', '${rbEsc(g.kind || 'nam')}', '${btnId}')"
                            title="Listen to this gear in isolation" class="bg-dark-600 hover:bg-dark-500 text-gray-200 px-2.5 py-1 rounded text-xs flex-shrink-0">▶</button>`;
    }
    const t3kLink = g.tone3000_url
        ? `<a href="${rbEsc(g.tone3000_url)}" target="_blank" title="Ver en tone3000" class="text-xs text-gray-500 hover:text-gray-300 flex-shrink-0">↗</a>` : '';

    // VST panel — same UX as in the per-song view, but uses bulk-assign via
    // /vst/assign (writes the choice to EVERY preset_piece for this gear).
    const vstPanelId = `rb-cat-vst-${g.rs_gear.replace(/[^a-zA-Z0-9_-]/g,'_')}`;
    const knownCount = rbState.knownVsts ? rbState.knownVsts.length : 0;

    return `
        <div class="bg-dark-700/50 border border-gray-800/50 rounded-lg p-3 flex flex-col gap-2">
            <div class="flex items-center gap-3">
                ${img}
                <div class="min-w-0 flex-1">
                    <div class="text-gray-200 truncate">${rbEsc(g.real_name)}</div>
                    <div class="text-xs text-gray-500 truncate">${rbEsc(g.rs_gear)}</div>
                    <div class="text-xs mt-0.5 truncate">${parent}</div>
                </div>
                <div class="flex items-center gap-2 flex-shrink-0">
                    ${t3kLink}${listenBtn}
                    <button onclick="rbOpenSuggest('${rbEsc(g.rs_gear)}')"
                            class="bg-dark-600 hover:bg-dark-500 text-gray-200 px-2.5 py-1 rounded text-xs">Search</button>
                    <button onclick="rbToggleCatalogLibrary('${rbEsc(g.rs_gear)}','${rbEsc(g.category || '')}','${rbEsc(g.vst_path || '')}','${rbEsc(g.vst_format || 'VST3')}')"
                            title="Pick a downloaded NAM/IR or an installed VST/AU and bulk-assign to every preset using this gear"
                            class="bg-indigo-900/30 hover:bg-indigo-900/50 text-indigo-300 border border-indigo-800/40 px-2.5 py-1 rounded text-xs">📚 Library</button>
                </div>
            </div>
            <div id="rb-cat-lib-${rbEsc(g.rs_gear).replace(/[^a-zA-Z0-9_-]/g,'_')}" class="hidden bg-indigo-900/10 border border-indigo-800/30 rounded p-2"></div>
        </div>`;
}

// Open the catalog-card library picker (bulk-assigns to every preset using
// this rs_gear_type). `category` tells us whether to list NAMs or IRs.
async function rbToggleCatalogLibrary(rsGear, category, vstPath, vstFormat) {
    const safeId = rsGear.replace(/[^a-zA-Z0-9_-]/g, '_');
    const el = document.getElementById(`rb-cat-lib-${safeId}`);
    if (!el) return;
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

function rbRenderCatalogLibraryList(container, files, rsGear, kind, filter) {
    const safeId = rsGear.replace(/[^a-zA-Z0-9_-]/g, '_');
    const inputId = `rb-cat-lib-search-${safeId}`;
    const countId = `rb-cat-lib-count-${safeId}`;
    const rowsId  = `rb-cat-lib-rows-${safeId}`;
    container.innerHTML = `
        <div class="text-[10px] text-indigo-300 mb-1">
            Pick from your downloaded ${kind === 'ir' ? 'IRs' : 'NAMs'} · click "Use for all" to apply to every preset using <code>${rbEsc(rsGear)}</code>
        </div>
        <div class="flex items-center gap-2 mb-2">
            <input id="${inputId}" type="text" placeholder="🔍 Filter…"
                   oninput="rbFilterCatalogLibrary('${rbEsc(rsGear)}')"
                   value="${rbEsc(filter || '')}"
                   class="flex-1 bg-dark-800 border border-gray-800 rounded text-[11px] text-gray-200 px-2 py-1">
            <span id="${countId}" class="text-[10px] text-gray-500">${files.length}/${files.length}</span>
        </div>
        <div id="${rowsId}" class="max-h-64 overflow-y-auto"></div>`;
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
    const rows = filtered.slice(0, 50).map(file => {
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
    }).join('');
    const moreNote = filtered.length > 50
        ? `<div class="text-[10px] text-gray-500 italic mt-1">…and ${filtered.length - 50} more (refine search)</div>`
        : '';
    rowsEl.innerHTML = (rows || '<div class="text-xs text-gray-500 italic">no matches</div>') + moreNote;
    if (countEl) countEl.textContent = `${filtered.length}/${files.length}`;
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
    let pluginSelector;
    if (known.length === 0) {
        pluginSelector = `
            <div class="text-xs text-gray-400">
                No plugins scanned yet — scan in <span class="text-gray-300">Settings → VST / Audio Unit plugins</span>, or use 📁 Pick file below.
            </div>`;
    } else {
        const selId = `${panelId}-select`;
        const opts = rbBuildVstOptions(stagedPath, '', true);
        pluginSelector = `
            <div class="flex items-center gap-2 mb-1">
                <input id="${selId}-search" type="text" placeholder="🔍 filter by name / brand / category"
                       oninput="rbFilterVstSelect('${rbEsc(selId)}')"
                       class="flex-1 bg-dark-900 border border-gray-800 rounded text-xs text-gray-200 px-2 py-1">
                <label class="text-[10px] text-gray-400 flex items-center gap-1 whitespace-nowrap">
                    <input id="${selId}-hideinst" type="checkbox" checked
                           onchange="rbFilterVstSelect('${rbEsc(selId)}')"> hide instruments
                </label>
            </div>
            <select id="${selId}" data-staged="${rbEsc(stagedPath)}"
                    onchange="rbCatalogStagePath('${rbEsc(panelId)}', this.value)"
                    class="w-full bg-dark-800 border border-gray-800 rounded text-xs text-gray-200 px-2 py-1">${opts}</select>`;
    }
    return `
        <div class="text-xs text-purple-300 font-semibold">VST3 / Audio Unit</div>
        ${pluginSelector}
        <div class="flex items-center gap-2 flex-wrap">
            <button onclick="rbCatalogPickFile('${rbEsc(panelId)}','${rbEsc(rsGear)}','${rbEsc(currentFormat)}')"
                    title="File picker — bypass scan entirely"
                    class="bg-emerald-700 hover:bg-emerald-600 text-white text-xs px-2 py-1 rounded">
                📁 Pick file
            </button>
        </div>
        <div class="flex items-center gap-2">
            <input type="text"
                   placeholder="Or paste path: /Library/Audio/Plug-Ins/VST3/TAL-Chorus-LX.vst3"
                   value="${rbEsc(stagedPath)}"
                   onchange="rbCatalogStagePath('${rbEsc(panelId)}', this.value); var s = document.getElementById('${rbEsc(panelId)}-selected'); if (s) s.textContent = 'Selected: ' + (this.value.split('/').pop() || '(none selected)');"
                   class="flex-1 bg-dark-800 border border-gray-800 rounded text-[11px] text-gray-300 px-2 py-1 font-mono">
        </div>
        <div id="${panelId}-selected" class="text-[10px] text-purple-200/80 break-all">Selected: ${rbEsc(stagedName)}</div>
        <div class="flex items-center gap-2 flex-wrap">
            <button onclick="rbCatalogLoadAndEdit('${rbEsc(panelId)}')"
                    class="bg-blue-700 hover:bg-blue-600 text-white text-xs px-2 py-1 rounded">
                ▶ Load &amp; Edit
            </button>
            <button onclick="rbCatalogCaptureState('${rbEsc(panelId)}')"
                    title="Capture current parameter state from the engine"
                    class="bg-amber-700/60 hover:bg-amber-600/60 text-amber-100 text-xs px-2 py-1 rounded">
                📸 Capture state
            </button>
            <button onclick="rbCatalogAssignVst('${rbEsc(panelId)}','${rbEsc(rsGear)}')"
                    class="bg-purple-700 hover:bg-purple-600 text-white text-xs px-2 py-1 rounded">
                ✓ Assign to ALL ${rbEsc(rsGear)}
            </button>
        </div>
        <div id="${panelId}-status" class="text-[10px] text-gray-500"></div>`;
}

function rbCatalogStagePath(panelId, path) {
    const el = document.getElementById(panelId);
    if (el) el.dataset.stagedPath = path;
}

function rbCatalogResolveStagedPath(panelId) {
    const el = document.getElementById(panelId);
    if (el && el.dataset.stagedPath) return el.dataset.stagedPath;
    const select = document.getElementById(`${panelId}-select`);
    if (select && select.value) return select.value;
    return '';
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
async function rbAuditionVst(vstPath, vstFormat, btnId) {
    const api = rbNativeAudio();
    if (!api) return;
    const btn = document.getElementById(btnId);
    // Toggle off if already auditioning this one.
    if (rbState._auditionId === btnId) {
        await rbStopPreview();
        if (btn) { btn.disabled = false; btn.textContent = '▶'; }
        return;
    }
    if (rbState.listeningTone !== null || rbState._auditionId) {
        await rbStopPreview();
    }
    try {
        if (btn) { btn.disabled = true; btn.textContent = '…'; }
        const url = `${RB_API}/native_preset_one?kind=vst&vst_path=${encodeURIComponent(vstPath)}&vst_format=${encodeURIComponent(vstFormat || 'VST3')}`;
        const payload = await (await fetch(url)).json();
        if (!payload || !payload.native_preset) throw new Error('no preset returned');
        if (api.clearChain) await api.clearChain().catch(() => {});
        const res = await api.loadPreset(JSON.stringify(payload.native_preset));
        if (!res || res.success === false) throw new Error((res && res.error) || 'loadPreset failed');
        if (api.setMonitorMute) await api.setMonitorMute(false).catch(() => {});
        const wasRunning = api.isAudioRunning ? await api.isAudioRunning().catch(() => true) : true;
        await api.startAudio();
        rbState._previewStartedAudio = !wasRunning;
        rbState._previewMode = 'native';
        rbState._auditionId = btnId;
        if (btn) { btn.disabled = false; btn.textContent = '⏸'; }
    } catch (e) {
        if (btn) { btn.disabled = false; btn.textContent = '▶'; }
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
            if (api.setGain) { await api.setGain('input', 1.0).catch(() => {}); await api.setGain('chain', 1.0).catch(() => {}); }
            if (api.setMonitorMute) await api.setMonitorMute(false).catch(() => {});
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
    document.getElementById('rb-aggressive').checked = !!s.aggressive;
    document.getElementById('rb-min-downloads').value = s.min_downloads;
    const status = document.getElementById('rb-api-key-status');
    if (s.has_tone3000_key) {
        status.innerHTML = `<span class="text-green-400">Key configured (${rbEsc(s.tone3000_api_key_preview)})</span>`;
    } else {
        status.textContent = 'No key. Deep-link mode active.';
    }
}

async function rbSaveApiKey() {
    const key = document.getElementById('rb-api-key').value.trim();
    if (!key) return;
    await fetch(`${RB_API}/settings`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ tone3000_api_key: key }),
    });
    document.getElementById('rb-api-key').value = '';
    rbLoadSettings();
    rbInit();  // refresh status banner
}

async function rbSaveSettings() {
    const aggressive = document.getElementById('rb-aggressive').checked;
    const min_downloads = parseInt(document.getElementById('rb-min-downloads').value, 10) || 0;
    await fetch(`${RB_API}/settings`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ aggressive, min_downloads }),
    });
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
        // is open to reflect that the gear is no longer pending.
        if (rbState.currentTab === 'pending') rbLoadPending();
        else if (rbState.currentTab === 'dashboard') rbLoadCoverage();
        else if (rbState.currentTab === 'gear') rbLoadCatalog();
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

async function rbExtractGearMap() {
    const path = document.getElementById('rb-gears-psarc').value.trim();
    if (!path) return;
    const status = document.getElementById('rb-extract-status');
    status.textContent = 'Extracting…';
    try {
        const r = await fetch(`${RB_API}/extract_gear_map`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ gears_psarc: path }),
        });
        const data = await r.json();
        if (!r.ok) {
            status.innerHTML = `<span class="text-red-400">Error: ${rbEsc(data.error || r.status)}</span>`;
            return;
        }
        status.innerHTML = `<span class="text-green-400">Done: ${data.count} entries. Reloading status…</span>`;
        rbInit();
    } catch (e) {
        status.innerHTML = `<span class="text-red-400">${rbEsc(e.message)}</span>`;
    }
}

async function rbExtractIRs() {
    const path = document.getElementById('rb-irs-psarc').value.trim();
    if (!path) return;
    const status = document.getElementById('rb-extract-irs-status');
    status.textContent = 'Extracting IRs (may take 30-60s)…';
    try {
        const r = await fetch(`${RB_API}/extract_irs`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ gears_psarc: path }),
        });
        const data = await r.json();
        if (!r.ok) {
            status.innerHTML = `<span class="text-red-400">Error: ${rbEsc(data.error || r.status)}</span>`;
            return;
        }
        status.innerHTML = `<span class="text-green-400">Done: ${data.count} RS entities with IR. Reloading status…</span>`;
        rbInit();
    } catch (e) {
        status.innerHTML = `<span class="text-red-400">${rbEsc(e.message)}</span>`;
    }
}

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
