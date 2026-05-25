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
    window.showScreen = function (id) {
        origShowScreen(id);
        if (id === 'plugin-rig_builder') {
            rbInit();
        } else if (rbState.listeningTone !== null || rbState._auditionId) {
            // Leaving NAM Rig Builder: stop any live preview/audition so it
            // doesn't keep monitoring the input behind another screen.
            rbStopPreview();
        }
    };
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
                console.log(`[rig_builder] full-chain playback: preset ${m[1]} → ${chain.length} stages`
                    + ` (${data.nam_stage_count} NAM + ${chain.length - data.nam_stage_count} IR)`
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
    if (name === 'settings') rbLoadSettings();
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
        assignedEl.textContent = `${st.assigned} tonos persistidos`;
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
        const cloudTag = mat ? '' : '<span class="text-xs text-blue-400 ml-2">☁ cloud</span>';
        const textColor = mat ? 'text-gray-300' : 'text-gray-500';
        return `
            <div onclick="rbLoadSongTones('${rbEsc(name).replace(/'/g,"\\'")}')"
                 class="cursor-pointer hover:bg-dark-700/50 px-3 py-2 rounded text-sm ${textColor} flex items-center">
                <span class="flex-1 truncate">${rbEsc(name)}</span>
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
        el.innerHTML = `<p class="text-red-400">Error al renderizar los tonos: ${rbEsc(e.message)}</p>`;
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
    return `
        <div class="bg-dark-700/50 border border-gray-800/50 rounded-xl p-4">
            <div class="flex items-baseline justify-between mb-3">
                <h3 class="text-white font-semibold">${rbEsc(tone.name)}</h3>
                <span class="text-xs text-gray-500">${rbEsc(tone.key)}</span>
            </div>
            <div class="grid grid-cols-1 md:grid-cols-2 gap-3 mb-4">${pieces}</div>
            <div class="flex justify-end gap-2">
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
        </div>`;
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
        stageLabel = `✓ ${effFile}`;
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

    // VST panel — collapsed by default. Toggled by the "⚙ VST…" button.
    // Shows the dropdown of installed plugins + Load+Edit/Capture/Use actions.
    // If the user has never scanned, show a Scan button instead of an empty dropdown.
    const vstPanelId = `rb-vst-panel-${toneIdx}-${pIdx}`;
    const knownCount = rbState.knownVsts ? rbState.knownVsts.length : 0;
    const vstControl = `
        <div class="flex items-center gap-2 mt-2">
            <button onclick="rbToggleVstPanel(${toneIdx}, ${pIdx})"
                    class="bg-purple-900/30 hover:bg-purple-900/50 text-purple-300 border border-purple-800/40 px-2 py-1 rounded text-xs transition">
                ⚙ VST…
            </button>
            <span class="text-[10px] text-gray-500">
                ${knownCount > 0 ? `${knownCount} plugins installed` : 'no plugins scanned yet'}
            </span>
        </div>
        <div id="${vstPanelId}" class="hidden mt-2 bg-purple-900/10 border border-purple-800/30 rounded px-2 py-2 space-y-2">
            ${rbRenderVstPanelBody(toneIdx, pIdx, effKind === 'vst' ? effVstPath : '', effVstFormat)}
        </div>`;

    return `
        <div class="bg-dark-800 border border-gray-800/50 rounded-lg p-3" data-tone="${toneIdx}" data-piece="${pIdx}">
            <div class="flex items-center justify-between mb-2">
                <div>
                    <div class="text-sm text-gray-200">${rbEsc(p.real_name || p.type)}</div>
                    <div class="text-xs text-gray-500">
                        ${rbEsc(p.slot)} · ${rbEsc(p.rs_category)} · ${rbEsc(p.type)}
                    </div>
                </div>
                <div class="flex items-center gap-1">
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
                <span class="rb-piece-file text-xs ${stageClass} truncate" title="${rbEsc(hasVst ? effVstPath : (hasFile ? effFile : ''))}">${rbEsc(stageLabel)}</span>
                ${(hasFile || hasVst) && mode ? `<span class="text-[10px] text-gray-600 whitespace-nowrap">(${rbEsc(mode)})</span>` : ''}
            </div>
            ${rsKnobsBlock}
            ${rsIrControl}
            ${vstControl}
        </div>`;
}

// ── VST panel rendering + handlers ────────────────────────────────────

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
                No scanned plugin list. Use 📁 Pick file (safe) or 🔍 Scan (may crash if a plugin is malformed).
            </div>`;
    } else {
        const opts = known.map(p => {
            const sel = (p.path === stagedPath) ? 'selected' : '';
            const tag = p.format ? `[${rbEsc(p.format)}]` : '';
            return `<option value="${rbEsc(p.path)}" ${sel}>${rbEsc(p.name || p.path.split('/').pop())} ${tag}</option>`;
        }).join('');
        pluginSelector = `
            <div class="flex items-center gap-2">
                <select id="rb-vst-select-${toneIdx}-${pIdx}"
                        onchange="rbStagePath(${toneIdx}, ${pIdx}, this.value)"
                        class="flex-1 bg-dark-800 border border-gray-800 rounded text-xs text-gray-200 px-2 py-1">${opts}</select>
                <button onclick="rbScanForVsts(${toneIdx}, ${pIdx})"
                        title="Re-scan installed plugins (may crash Slopsmith if a plugin is malformed — use Pick file if so)"
                        class="text-xs text-gray-400 hover:text-gray-200 px-1">🔄</button>
            </div>`;
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
            ${known.length === 0 ? `
                <button onclick="rbScanForVsts(${toneIdx}, ${pIdx})"
                        title="Try a full scan (may crash if a plugin is malformed)"
                        class="bg-gray-700 hover:bg-gray-600 text-gray-200 text-xs px-2 py-1 rounded">
                    🔍 Try scan
                </button>` : ''}
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

function rbToggleVstPanel(toneIdx, pIdx) {
    const el = document.getElementById(`rb-vst-panel-${toneIdx}-${pIdx}`);
    if (!el) return;
    el.classList.toggle('hidden');
    // First open: fetch suggestions for this gear so the user sees a hint
    // about which free VSTs typically work for this rs_gear_type.
    if (!el.classList.contains('hidden') && !el.dataset.suggestionsLoaded) {
        el.dataset.suggestionsLoaded = '1';
        const piece = rbState.songTones.tones[toneIdx].chain[pIdx];
        rbLoadVstSuggestions(piece.type, toneIdx, pIdx);
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
    try {
        statusSetter && statusSetter('scanning… (up to a minute · don\'t click anything)');
        // scanPlugins returns the list directly per the audio_engine plugin
        // (see bundle/audio_engine/screen.js:744). Older signatures returned
        // void — handle both.
        let plugins;
        const ret = await api.scanPlugins();
        if (Array.isArray(ret)) {
            plugins = ret;
        } else {
            // Older return-void signature → fetch list separately.
            plugins = await api.getKnownPlugins();
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
        if (rbState._vstEditorSlot != null && api.clearChain) {
            await api.clearChain().catch(() => {});
        }
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
    const params = piece._vst_param_meta || [];
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
        const stateObj = { params };
        // Best-effort: also stash the opaque savePreset blob as fallback,
        // for plugins where getParameters didn't return anything useful.
        if (typeof api?.savePreset === 'function' && Object.keys(params).length === 0) {
            try {
                const blob = await api.savePreset();
                if (blob) stateObj.opaque = typeof blob === 'string' ? blob : JSON.stringify(blob);
            } catch (_) { /* best-effort */ }
        }
        piece._vst_state = JSON.stringify(stateObj);
        piece._vst_params = params;
        if (statusEl) {
            const n = Object.keys(params).length;
            statusEl.textContent = n > 0
                ? `captured ${n} param values. Click "Use this VST" to persist.`
                : `captured opaque state (${piece._vst_state.length}b). Click "Use this VST".`;
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

async function rbLoadVstSuggestions(rsGearType, toneIdx, pIdx) {
    try {
        const r = await fetch(`${RB_API}/vst/suggest/${encodeURIComponent(rsGearType)}`);
        if (!r.ok) return;
        const data = await r.json();
        const suggestions = (data && data.suggestions) || [];
        if (suggestions.length === 0) return;
        const statusEl = document.getElementById(`rb-vst-status-${toneIdx}-${pIdx}`);
        if (!statusEl) return;
        // Pretty hint: list 1-3 suggested VSTs with installed badge.
        const parts = suggestions.slice(0, 3).map(s => {
            const badge = s.installed ? '✓' : '↓';
            return `${badge} ${rbEsc(s.name)}`;
        }).join(' · ');
        statusEl.innerHTML = `Hint: ${parts}`;
    } catch (_) { /* best-effort */ }
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
        tone_key: tone.key,
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
function rbApplyBypassToChain(payload, toneIdx) {
    const tone = rbState.songTones && rbState.songTones.tones[toneIdx];
    const chain = (payload && payload.native_preset && payload.native_preset.chain) || [];
    if (!tone) return;
    for (const stage of chain) {
        const piece = tone.chain.find(p => p.type === stage.rs_gear);
        stage.bypassed = !!(piece && piece._bypassed);
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
        if (!Array.isArray(chain) || !chain.length) throw new Error('archivo no encontrado');
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
        el.innerHTML = `<p class="text-red-400">Error al renderizar: ${rbEsc(e.message)}</p>`;
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
        parent = `<span class="text-green-400" title="${rbEsc(g.file || '')}">✓ ${rbEsc(g.tone3000_title || g.file || 'asignado')}</span>`;
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
                    <button onclick="rbToggleCatalogVstPanel('${rbEsc(vstPanelId)}','${rbEsc(g.rs_gear)}','${rbEsc(g.vst_path || '')}','${rbEsc(g.vst_format || 'VST3')}')"
                            class="bg-purple-900/30 hover:bg-purple-900/50 text-purple-300 border border-purple-800/40 px-2.5 py-1 rounded text-xs">⚙ VST…</button>
                </div>
            </div>
            <div id="${vstPanelId}" class="hidden bg-purple-900/10 border border-purple-800/30 rounded px-2 py-2 space-y-2">
                <div class="text-[10px] text-purple-200/70">
                    ${knownCount > 0 ? `${knownCount} plugins installed` : 'no plugins scanned yet'} · bulk-assign to every preset using <code>${rbEsc(g.rs_gear)}</code>
                </div>
            </div>
        </div>`;
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
                No scanned plugin list. Use 📁 Pick file (safe) or 🔍 Scan (may crash Slopsmith if a plugin is malformed).
            </div>`;
    } else {
        const opts = known.map(p => {
            const sel = (p.path === stagedPath) ? 'selected' : '';
            const tag = p.format ? `[${rbEsc(p.format)}]` : '';
            return `<option value="${rbEsc(p.path)}" ${sel}>${rbEsc(p.name || p.path.split('/').pop())} ${tag}</option>`;
        }).join('');
        pluginSelector = `
            <div class="flex items-center gap-2">
                <select id="${panelId}-select"
                        onchange="rbCatalogStagePath('${rbEsc(panelId)}', this.value)"
                        class="flex-1 bg-dark-800 border border-gray-800 rounded text-xs text-gray-200 px-2 py-1">${opts}</select>
                <button onclick="rbCatalogScanVsts('${rbEsc(panelId)}','${rbEsc(rsGear)}','${rbEsc(stagedPath)}','${rbEsc(currentFormat)}')"
                        title="Re-scan installed plugins (may crash)"
                        class="text-xs text-gray-400 hover:text-gray-200 px-1">🔄</button>
            </div>`;
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
            ${known.length === 0 ? `
                <button onclick="rbCatalogScanVsts('${rbEsc(panelId)}','${rbEsc(rsGear)}','${rbEsc(stagedPath)}','${rbEsc(currentFormat)}')"
                        title="Try a full scan (may crash)"
                        class="bg-gray-700 hover:bg-gray-600 text-gray-200 text-xs px-2 py-1 rounded">
                    🔍 Try scan
                </button>` : ''}
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
        if (rbState._vstEditorSlot != null && api.clearChain) {
            await api.clearChain().catch(() => {});
        }
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
