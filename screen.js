// NAM Rig Builder plugin — Rocksmith tone → NAM preset mapping UI.

(function () {
    // Idempotency: showScreen is wrapped at most once even if screen.js
    // is re-evaluated by the host. Without this guard, each re-eval
    // captures the previous wrapper and we leak closures + run tbInit
    // multiple times per navigation.
    const HOOK_KEY = '__slopsmithNamRigBuilderInstalled';
    if (window[HOOK_KEY]) return;
    window[HOOK_KEY] = true;

    const origShowScreen = window.showScreen;
    window.showScreen = function (id) {
        origShowScreen(id);
        if (id === 'plugin-nam_rig_builder') {
            tbInit();
        } else if (tbState.listeningTone !== null || tbState._auditionId) {
            // Leaving NAM Rig Builder: stop any live preview/audition so it
            // doesn't keep monitoring the input behind another screen.
            tbStopPreview();
        }
    };
})();

// ── Full-chain playback (no bundle edit, survives app updates) ─────────
// Real song playback resolves a tone → preset_id and fetches nam_tone's
// /native-preset/{id}, which the bundle builds from the 2-column presets
// table (single amp + cab). We transparently redirect just that GET to
// nam_rig_builder's /native_preset_full/{id} (identical response shape) so the
// engine receives EVERY NAM stage (pedal → amp → … → cab). Scoped to that
// one URL; everything else passes through untouched. Kill-switch:
// window.__tbChainPlayback = false.
(function () {
    if (window.__tbFetchPatched) return;
    window.__tbFetchPatched = true;
    const origFetch = window.fetch.bind(window);
    const RE = /\/api\/plugins\/nam_tone\/native-preset\/(\d+)(?:[/?#]|$)/;
    window.fetch = function (input, init) {
        let url;
        try { url = typeof input === 'string' ? input : (input && input.url); } catch (_) { url = null; }
        const m = (typeof url === 'string') ? url.match(RE) : null;
        if (!m || window.__tbChainPlayback === false) {
            return origFetch(input, init);
        }
        const fullUrl = `/api/plugins/nam_rig_builder/native_preset_full/${m[1]}`;
        return origFetch(fullUrl, init).then(async (r) => {
            if (!r.ok) return origFetch(input, init);            // build failed → original 2-stage
            const txt = await r.text();
            try {
                const data = JSON.parse(txt);
                const chain = data && data.native_preset && data.native_preset.chain;
                if (!Array.isArray(chain) || chain.length === 0) return origFetch(input, init);
                console.log(`[nam_rig_builder] full-chain playback: preset ${m[1]} → ${chain.length} stages`
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

let tbState = {
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
};

const TB_API = '/api/plugins/nam_rig_builder';
const NAM_API = '/api/plugins/nam_tone';

// ── HTML helper ─────────────────────────────────────────────────────

function tbEsc(s) {
    return String(s ?? '').replace(/[&<>"']/g, (c) => ({
        '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'
    }[c]));
}

// ── Init / status ───────────────────────────────────────────────────

async function tbInit() {
    try {
        const r = await fetch(`${TB_API}/status`);
        tbState.status = await r.json();
    } catch (e) {
        document.getElementById('tb-status').innerHTML = tbBanner(
            'red', 'Error', `Couldn't load /status: ${tbEsc(e.message)}`
        );
        return;
    }
    tbRenderStatus();
    tbShowTab(tbState.currentTab);
}

function tbBanner(color, title, body) {
    const palette = {
        red: 'bg-red-900/20 border-red-800/30 text-red-400',
        yellow: 'bg-yellow-900/20 border-yellow-800/30 text-yellow-400',
        green: 'bg-green-900/20 border-green-800/30 text-green-400',
        blue: 'bg-blue-900/20 border-blue-800/30 text-blue-400',
    }[color] || 'bg-dark-700/50 border-gray-800/50 text-gray-300';
    return `
        <div class="${palette} border rounded-xl p-4 text-sm">
            <p class="font-semibold mb-1">${tbEsc(title)}</p>
            <p class="text-gray-400">${body}</p>
        </div>`;
}

function tbRenderStatus() {
    const s = tbState.status;
    const el = document.getElementById('tb-status');
    if (!s.rs_to_real_loaded) {
        el.innerHTML = tbBanner(
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

function tbShowTab(name) {
    tbState.currentTab = name;
    document.querySelectorAll('.tb-tab-panel').forEach(el => el.classList.add('hidden'));
    const panel = document.getElementById(`tb-tab-${name}`);
    if (panel) panel.classList.remove('hidden');

    document.querySelectorAll('.tb-tab').forEach(b => {
        const active = b.dataset.tbTab === name;
        b.classList.toggle('text-white', active);
        b.classList.toggle('border-accent', active);
        b.classList.toggle('text-gray-400', !active);
        b.classList.toggle('border-transparent', !active);
    });

    if (name === 'dashboard') tbLoadCoverage();
    if (name === 'pending') tbLoadPending();
    if (name === 'gear') tbLoadCatalog();
    if (name === 'settings') tbLoadSettings();
}

// ── Dashboard: coverage stats ──────────────────────────────────────

async function tbLoadCoverage() {
    const el = document.getElementById('tb-gear-coverage');
    const s = tbState.status;
    if (!s || !s.rs_to_real_loaded) {
        el.innerHTML = '<span class="text-yellow-500">rs_to_real.json no cargado.</span>';
        return;
    }
    const cats = s.rs_to_real_by_category || {};
    el.innerHTML = Object.entries(cats).sort()
        .map(([cat, n]) => `<div class="flex justify-between border-b border-gray-800/50 py-1">
            <span class="capitalize">${tbEsc(cat)}</span><span class="text-gray-500">${n}</span></div>`)
        .join('');
}

// ── Dashboard: batch ───────────────────────────────────────────────

async function tbStartBatch() {
    const btn = document.getElementById('tb-batch-btn');
    btn.disabled = true;
    try {
        const r = await fetch(`${TB_API}/batch_all`, { method: 'POST' });
        if (!r.ok) {
            const err = await r.json().catch(() => ({}));
            alert(`Couldn't start batch: ${err.error || r.status}`);
            return;
        }
    } finally {
        btn.disabled = false;
    }
    document.getElementById('tb-batch-progress').classList.remove('hidden');
    if (tbState.batchPoll) clearInterval(tbState.batchPoll);
    tbState.batchPoll = setInterval(tbPollBatch, 1000);
    tbPollBatch();
}

async function tbPollBatch() {
    let st;
    try {
        const r = await fetch(`${TB_API}/batch_status`);
        st = await r.json();
    } catch (e) {
        return;
    }
    const pct = st.total ? Math.round(100 * st.progress / st.total) : 0;
    document.getElementById('tb-batch-pct').textContent = `${pct}%`;
    document.getElementById('tb-batch-count').textContent = `${st.progress} / ${st.total}`;
    document.getElementById('tb-batch-bar').style.width = `${pct}%`;

    const assignedEl = document.getElementById('tb-batch-assigned');
    if (st.assigned) {
        assignedEl.textContent = `${st.assigned} tonos persistidos`;
        assignedEl.classList.remove('hidden');
    }

    const log = document.getElementById('tb-batch-log');
    log.textContent = (st.log || []).join('\n');
    log.scrollTop = log.scrollHeight;

    if (!st.running && tbState.batchPoll) {
        clearInterval(tbState.batchPoll);
        tbState.batchPoll = null;
    }
}

// ── Pending ────────────────────────────────────────────────────────

async function tbLoadPending() {
    const el = document.getElementById('tb-pending-list');
    el.innerHTML = '<span class="text-gray-500">Loading…</span>';
    let data;
    try {
        const r = await fetch(`${TB_API}/coverage`);
        data = await r.json();
    } catch (e) {
        el.innerHTML = `<span class="text-red-400">Error: ${tbEsc(e.message)}</span>`;
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
                <div class="text-gray-200">${tbEsc(it.name)}</div>
                <div class="text-xs text-gray-500">
                    ${tbEsc(it.rs_gear)} · <span class="capitalize">${tbEsc(it.category)}</span> ·
                    ${it.pending_chain_slots}/${it.total_chain_slots} pending
                </div>
            </div>
            <button onclick="tbOpenSuggest('${tbEsc(it.rs_gear)}')"
                    class="bg-accent hover:bg-accent/80 text-white px-3 py-1 rounded-lg text-xs transition">
                Search
            </button>
        </div>
    `).join('');
}

// ── Suggest modal (manual search per gear) ─────────────────────────

async function tbOpenSuggest(rsGear, queryOverride = '', gearsOverride = '') {
    // Build URL with optional overrides so the same modal can be
    // re-invoked when the user edits the query and re-searches.
    const qs = new URLSearchParams({ rs_gear: rsGear });
    if (queryOverride) qs.set('query_override', queryOverride);
    if (gearsOverride) qs.set('gears_override', gearsOverride);
    let data;
    try {
        const r = await fetch(`${TB_API}/search?${qs}`);
        data = await r.json();
    } catch (e) {
        alert(`Search failed: ${e.message}`);
        return;
    }

    // Remove any existing modal so a re-search replaces the previous
    // open one instead of stacking new instances on top.
    document.querySelectorAll('.tb-suggest-modal').forEach(m => m.remove());

    const modal = document.createElement('div');
    modal.className = 'tb-suggest-modal fixed inset-0 z-50 bg-black/70 flex items-center justify-center p-6';
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
                ? `<img src="${tbEsc(c.images[0])}" alt="" loading="lazy" class="w-12 h-12 rounded object-cover bg-dark-900 flex-shrink-0" onerror="this.style.visibility='hidden'">`
                : `<div class="w-12 h-12 rounded bg-dark-900 flex items-center justify-center text-gray-700 text-[10px] flex-shrink-0">no photo</div>`;
            return `
            <div class="bg-dark-800 border border-gray-800 rounded-lg p-3 flex items-center gap-3">
                ${photo}
                <a href="${tbEsc(c.url)}" target="_blank" class="flex-1 min-w-0 hover:text-white transition">
                    <div class="text-gray-200 text-sm truncate">${tbEsc(c.title)}</div>
                    <div class="text-xs text-gray-500">
                        license: ${tbEsc(c.license || 'unknown')} · ${c.downloads_count || 0} dl · ${c.favorites_count || 0} ♥
                    </div>
                </a>
                <button onclick="tbAuditionCandidate(this, '${tbEsc(data.rs_gear)}', ${c.id})"
                        title="Download and listen" class="bg-dark-600 hover:bg-dark-500 text-gray-200 text-xs px-2.5 py-1.5 rounded flex-shrink-0">▶</button>
                <button onclick="tbDownloadForGear(this, '${tbEsc(data.rs_gear)}', ${c.id})"
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
                    <h3 class="text-white font-semibold">${tbEsc(data.rs_gear)}</h3>
                    <p class="text-gray-500 text-xs">platform: ${tbEsc(data.platform)}</p>
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
                    <input type="text" id="tb-suggest-query" value="${tbEsc(data.query)}"
                           class="w-full bg-dark-900 border border-gray-800 rounded px-2 py-1.5 text-sm text-gray-200">
                </div>
                <div class="w-32">
                    <label class="text-xs text-gray-500 block mb-1">gears</label>
                    <select id="tb-suggest-gears" class="w-full bg-dark-900 border border-gray-800 rounded px-2 py-1.5 text-sm text-gray-200">
                        ${['amp','pedal','outboard','ir','full-rig'].map(g =>
                            `<option value="${g}"${data.gears===g?' selected':''}>${g}</option>`
                        ).join('')}
                    </select>
                </div>
                <div class="flex gap-2 w-full">
                    <button onclick="tbSuggestRerun('${tbEsc(data.rs_gear)}')"
                            class="bg-accent hover:bg-accent/80 text-white px-3 py-1.5 rounded text-xs transition">
                        Search again
                    </button>
                    <button onclick="tbSuggestSaveOverride('${tbEsc(data.rs_gear)}')"
                            class="bg-dark-600 hover:bg-dark-500 text-gray-200 px-3 py-1.5 rounded text-xs transition">
                        Save override to rs_to_real.json
                    </button>
                </div>
            </div>

            <div class="space-y-2 mb-4">${candidatesHtml}</div>
            <a href="${tbEsc(data.deep_link)}" target="_blank"
               class="inline-block bg-accent hover:bg-accent/80 text-white px-4 py-2 rounded-lg text-sm transition">
                Open tone3000.com with these filters ↗
            </a>
        </div>`;
    document.body.appendChild(modal);
}

function tbSuggestRerun(rsGear) {
    const q = document.getElementById('tb-suggest-query').value.trim();
    const g = document.getElementById('tb-suggest-gears').value.trim();
    tbOpenSuggest(rsGear, q, g);
}

async function tbSuggestSaveOverride(rsGear) {
    const q = document.getElementById('tb-suggest-query').value.trim();
    const g = document.getElementById('tb-suggest-gears').value.trim();
    if (!q) { alert('Query required'); return; }
    try {
        const r = await fetch(`${TB_API}/override_query`, {
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
        tbOpenSuggest(rsGear, q, g);
    } catch (e) {
        alert(`Error: ${e.message}`);
    }
}

// ── By song ────────────────────────────────────────────────────────

async function tbListSongs() {
    const q = document.getElementById('tb-song-search').value.trim();
    const r = await fetch(`${TB_API}/list_songs?q=${encodeURIComponent(q)}`);
    const data = await r.json();
    const el = document.getElementById('tb-song-list');
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
            <div onclick="tbLoadSongTones('${tbEsc(name).replace(/'/g,"\\'")}')"
                 class="cursor-pointer hover:bg-dark-700/50 px-3 py-2 rounded text-sm ${textColor} flex items-center">
                <span class="flex-1 truncate">${tbEsc(name)}</span>
                ${cloudTag}
            </div>`;
    }).join('');
}

async function tbLoadSongTones(filename) {
    const el = document.getElementById('tb-song-tones');
    tbState.currentSongFile = filename;
    el.innerHTML = '<p class="text-gray-500">Loading…</p>';

    // Try once. If the server signals cloud_only, fire cloud_loader's
    // materialize endpoint and retry once the download finishes.
    let data = await tbFetchSong(filename);
    if (data && data.error === 'cloud_only') {
        el.innerHTML = `<p class="text-blue-400">☁ Downloading "${tbEsc(filename)}" from Google Drive…</p>`;
        const ok = await tbMaterializeFromCloud(filename, el);
        if (!ok) return;
        data = await tbFetchSong(filename);
    }
    if (!data) {
        el.innerHTML = '<p class="text-red-400">Network error loading the song</p>';
        return;
    }
    if (data.error) {
        el.innerHTML = `<p class="text-red-400">${tbEsc(data.error)}</p>`;
        return;
    }
    // Re-rendering the tone list discards the old "Stop" buttons, so
    // stop any in-flight preview to keep engine + UI state consistent.
    if (tbState.listeningTone !== null) {
        tbStopPreview();
    }
    tbState.songTones = data;
    // Seed the per-piece bypass UI state from the persisted value so the
    // Bypass buttons reflect what was saved for this song.
    data.tones.forEach(t => (t.chain || []).forEach(p => { p._bypassed = !!p.bypassed; }));
    try {
        el.innerHTML = data.tones.map((t, idx) => tbRenderTone(t, idx, filename)).join('');
    } catch (e) {
        // Never leave the panel stuck on "Loading…" if a render throws.
        console.error('[nam_rig_builder] render of tones failed', e);
        el.innerHTML = `<p class="text-red-400">Error al renderizar los tonos: ${tbEsc(e.message)}</p>`;
        return;
    }

    // Auto-download trigger: if the user has an API key and any chain
    // piece is unassigned, kick off the song-scoped download flow.
    // The backend skips pieces that already have a file, so re-opening
    // a song with everything mapped is a near-instant no-op.
    if (tbState.status && tbState.status.has_tone3000_key && tbState.status.tone3000_api_works) {
        const unmapped = data.tones.flatMap(t => t.chain).filter(p => !(p.assigned && p.assigned.file)).length;
        if (unmapped > 0) {
            tbAutoDownloadSong(filename, unmapped, el);
        }
    }
}

// Inserts a status banner above the rendered chain and fires the
// backend auto-download. When the backend returns, refreshes the chain
// so the new file assignments are visible without the user having to
// click anything.
async function tbAutoDownloadSong(filename, unmappedCount, container) {
    const banner = document.createElement('div');
    banner.className = 'tb-autodl-banner bg-blue-900/15 border border-blue-800/30 rounded-lg p-3 text-sm mb-4';
    banner.innerHTML = `<p class="text-blue-400">⬇ Auto-downloading ${unmappedCount} unassigned piece(s) from tone3000…</p>`;
    container.prepend(banner);
    try {
        const r = await fetch(`${TB_API}/auto_download_song`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ filename }),
        });
        const result = await r.json();
        if (!r.ok) {
            banner.innerHTML = `<p class="text-red-400">Auto-download failed: ${tbEsc(result.error || r.status)}</p>`;
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
        const refreshed = await tbFetchSong(filename);
        if (refreshed && !refreshed.error) {
            tbState.songTones = refreshed;
            // Wipe out the previous chain HTML and re-render under the banner.
            const stillBanner = banner.cloneNode(true);
            container.innerHTML = '';
            container.appendChild(stillBanner);
            refreshed.tones.forEach((t, idx) => {
                const wrap = document.createElement('div');
                wrap.innerHTML = tbRenderTone(t, idx, filename);
                container.appendChild(wrap.firstElementChild);
            });
        }
    } catch (e) {
        banner.innerHTML = `<p class="text-red-400">Auto-download error: ${tbEsc(e.message)}</p>`;
    }
}

async function tbFetchSong(filename) {
    try {
        const r = await fetch(`${TB_API}/song/${encodeURIComponent(filename)}`);
        return await r.json();
    } catch (e) {
        return null;
    }
}

// Hits cloud_loader's materialize endpoint to pull a 0-byte stub down
// from Drive. Updates the inline status as it goes; returns false on
// failure so the caller can leave a clear message in place.
async function tbMaterializeFromCloud(filename, statusEl) {
    try {
        const url = `/api/cloud_loader/materialize?filename=${encodeURIComponent(filename)}`;
        const r = await fetch(url, { method: 'POST' });
        if (!r.ok) {
            const text = await r.text().catch(() => '');
            statusEl.innerHTML = `
                <div class="bg-yellow-900/20 border border-yellow-800/30 rounded-lg p-3 text-sm">
                    <p class="text-yellow-400 font-semibold mb-1">Could not materialize from Drive</p>
                    <p class="text-gray-400">${tbEsc(text || `HTTP ${r.status}`)}</p>
                    <p class="text-gray-500 text-xs mt-2">Make sure the <code class="bg-dark-800 px-1 rounded">cloud_loader</code> plugin is authenticated.</p>
                </div>`;
            return false;
        }
        const body = await r.json();
        statusEl.innerHTML = `<p class="text-blue-400">☁ Downloaded (${body.size_mb || '?'} MB) — parsing tones…</p>`;
        return true;
    } catch (e) {
        statusEl.innerHTML = `<p class="text-red-400">Error: ${tbEsc(e.message)}</p>`;
        return false;
    }
}

function tbRenderTone(tone, toneIdx, filename) {
    const pieces = tone.chain.map((p, pIdx) => tbRenderPiece(p, toneIdx, pIdx)).join('');
    return `
        <div class="bg-dark-700/50 border border-gray-800/50 rounded-xl p-4">
            <div class="flex items-baseline justify-between mb-3">
                <h3 class="text-white font-semibold">${tbEsc(tone.name)}</h3>
                <span class="text-xs text-gray-500">${tbEsc(tone.key)}</span>
            </div>
            <div class="grid grid-cols-1 md:grid-cols-2 gap-3 mb-4">${pieces}</div>
            <div class="flex justify-end gap-2">
                <button id="tb-listen-${toneIdx}"
                        onclick="tbListenTone(${toneIdx}, '${tbEsc(filename).replace(/'/g,"\\'")}')"
                        title="Saves the tone and plays it live through the NAM engine (monitors your guitar input)"
                        class="bg-dark-600 hover:bg-dark-500 text-gray-200 px-4 py-2 rounded-lg text-xs transition">
                    ▶ Listen
                </button>
                <button onclick="tbSaveTonePreset(${toneIdx}, '${tbEsc(filename).replace(/'/g,"\\'")}')"
                        class="bg-accent hover:bg-accent/80 text-white px-4 py-2 rounded-lg text-xs transition">
                    Save preset
                </button>
            </div>
        </div>`;
}

function tbRenderPiece(p, toneIdx, pIdx) {
    const isCab = p.rs_category === 'cab';
    const acceptExt = isCab ? '.wav' : '.nam';
    // Prefer an in-memory pending change (_uploaded_file, set by upload /
    // RS-IR assign / download-and-assign) over the persisted assignment, so
    // a gear change is reflected immediately on re-render — no re-fetch /
    // re-selecting the song.
    const effFile = p._uploaded_file || (p.assigned && p.assigned.file) || null;
    const hasFile = !!effFile;
    const mode = (p.assigned && p.assigned.assigned_mode) || (p._uploaded_file ? 'manual' : '');
    const fileLabel = hasFile ? `✓ ${effFile}` : '(unassigned)';
    const fileClass = hasFile ? 'text-green-400' : 'text-gray-500';
    const bypassed = !!p._bypassed;

    // For cab pieces we may have one or more Rocksmith-extracted IRs
    // available locally (no download needed). When present, surface a
    // one-click select with a dropdown for the mic-position variants.
    const rsIrs = p.rs_irs || [];
    let rsIrControl = '';
    if (rsIrs.length > 0) {
        const options = rsIrs.map((f, i) => `<option value="${tbEsc(f)}">${tbEsc(f.split('/').pop())}</option>`).join('');
        rsIrControl = `
            <div class="flex items-center gap-2 mt-2 bg-green-900/15 border border-green-800/30 rounded px-2 py-1.5">
                <span class="text-xs text-green-400 whitespace-nowrap">Rocksmith IR (${rsIrs.length}):</span>
                <select onchange="tbPickRsIr(this, ${toneIdx}, ${pIdx})"
                        class="flex-1 bg-dark-800 border border-gray-800 rounded text-xs text-gray-300 px-1 py-0.5">${options}</select>
                <button onclick="tbAssignRsIr(this, ${toneIdx}, ${pIdx})"
                        class="bg-green-700 hover:bg-green-600 text-white text-xs px-2 py-0.5 rounded">Use</button>
            </div>`;
    }

    return `
        <div class="bg-dark-800 border border-gray-800/50 rounded-lg p-3" data-tone="${toneIdx}" data-piece="${pIdx}">
            <div class="flex items-center justify-between mb-2">
                <div>
                    <div class="text-sm text-gray-200">${tbEsc(p.real_name || p.type)}</div>
                    <div class="text-xs text-gray-500">
                        ${tbEsc(p.slot)} · ${tbEsc(p.rs_category)} · ${tbEsc(p.type)}
                    </div>
                </div>
                <div class="flex items-center gap-1">
                    <button id="tb-bypass-${toneIdx}-${pIdx}" onclick="tbToggleBypass(${toneIdx}, ${pIdx}, this)"
                            title="Bypass: skips this stage in the preview (signal passes through unprocessed — it isn't muted, the chain keeps working)"
                            class="px-2 py-1 rounded text-xs transition ${bypassed ? 'bg-amber-700/40 text-amber-300 border border-amber-600/40' : 'bg-dark-600 hover:bg-dark-500 text-gray-300'}">
                        ${bypassed ? '⤳ Bypassed' : 'Bypass'}
                    </button>
                    <button onclick="tbOpenSuggest('${tbEsc(p.type)}')"
                            class="bg-dark-600 hover:bg-dark-500 text-gray-200 px-2 py-1 rounded text-xs transition">
                        Suggest
                    </button>
                </div>
            </div>
            <div class="flex items-center gap-2">
                <input type="file" accept="${acceptExt}"
                       onchange="tbUploadFile(this, ${toneIdx}, ${pIdx})"
                       class="text-xs text-gray-500 file:bg-dark-700 file:border-0 file:text-gray-300 file:px-2 file:py-1 file:rounded file:text-xs file:cursor-pointer">
                <span class="tb-piece-file text-xs ${fileClass} truncate" title="${tbEsc(hasFile ? effFile : '')}">${tbEsc(fileLabel)}</span>
                ${hasFile && mode ? `<span class="text-[10px] text-gray-600 whitespace-nowrap">(${tbEsc(mode)})</span>` : ''}
            </div>
            ${rsIrControl}
        </div>`;
}

function tbPickRsIr(select, toneIdx, pIdx) {
    // Cache the selection on the piece so "Use" reads what's currently
    // shown rather than re-querying the DOM later.
    const piece = tbState.songTones.tones[toneIdx].chain[pIdx];
    piece._selected_rs_ir = select.value;
}

function tbAssignRsIr(btn, toneIdx, pIdx) {
    const piece = tbState.songTones.tones[toneIdx].chain[pIdx];
    const wrapper = btn.closest('[data-piece]');
    const select = wrapper.querySelector('select');
    const file = piece._selected_rs_ir || select.value;
    if (!file) return;
    piece._uploaded_file = file;
    piece._uploaded_kind = 'rs_ir';
    const label = wrapper.querySelector('.tb-piece-file');
    label.textContent = `✓ ${file}`;
    label.classList.add('text-green-400');
    tbAfterGearChange(toneIdx);   // reflect + re-audition immediately
}

async function tbUploadFile(input, toneIdx, pIdx) {
    const file = input.files[0];
    if (!file) return;
    const piece = tbState.songTones.tones[toneIdx].chain[pIdx];
    const targetUrl = piece.rs_category === 'cab' ? `${NAM_API}/irs` : `${NAM_API}/models`;

    const fd = new FormData();
    fd.append('file', file);
    const wrapper = input.closest('[data-piece]');
    const label = wrapper.querySelector('.tb-piece-file');
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
        tbAfterGearChange(toneIdx);   // reflect + re-audition immediately
    } catch (e) {
        label.textContent = `error: ${e.message}`;
        label.classList.add('text-red-400');
    }
}

// Persist the tone's current chain selection. Returns the preset_id on
// success, or null on failure (after alerting). Shared by the explicit
// "Save preset" button and the "Listen" preview — the NAM engine can
// only load a *saved* preset id, so previewing has to persist first.
async function tbPersistTone(toneIdx, filename) {
    const tone = tbState.songTones.tones[toneIdx];
    const pieces = tone.chain.map(p => {
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
        const r = await fetch(`${TB_API}/save_preset`, {
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

async function tbSaveTonePreset(toneIdx, filename) {
    const tone = tbState.songTones.tones[toneIdx];
    const presetId = await tbPersistTone(toneIdx, filename);
    if (presetId !== null) {
        alert(`Preset saved for "${tone.name}". The NAM engine will load it when this song plays.`);
    }
}

// Native desktop audio engine, or null (e.g. browser/WASM-only mode).
function tbNativeAudio() {
    const a = window.slopsmithDesktop && window.slopsmithDesktop.audio;
    return (a && typeof a.loadPreset === 'function' && typeof a.startAudio === 'function') ? a : null;
}

// Stop whatever preview is active (native full-chain or nam_tone fallback).
async function tbStopPreview() {
    const mode = tbState._previewMode;
    const wasListening = tbState.listeningTone;
    const wasAudition = tbState._auditionId;
    tbState._previewMode = null;
    tbState.listeningTone = null;
    tbState._auditionId = null;
    try {
        if (mode === 'nam' && typeof window.namStopPresetTest === 'function') {
            await window.namStopPresetTest();
        } else {
            const api = tbNativeAudio();
            if (api) {
                if (api.setMonitorMute) await api.setMonitorMute(true).catch(() => {});
                if (api.clearChain) await api.clearChain().catch(() => {});
                if (tbState._previewStartedAudio && api.stopAudio) await api.stopAudio().catch(() => {});
            }
        }
    } catch (_) { /* best-effort */ }
    tbState._previewStartedAudio = false;
    tbState._previewPayload = null;
    // Restore whichever button label was showing "⏸ Stop".
    if (wasListening !== null) {
        const b = document.getElementById(`tb-listen-${wasListening}`);
        if (b) b.textContent = '▶ Listen';
    }
    if (wasAudition) {
        const b = document.getElementById(wasAudition);
        if (b) { b.disabled = false; b.textContent = '▶'; }
    }
}

// ── Per-stage bypass (audition each piece in/out of the chain) ─────────
function tbUpdateBypassBtn(btn, on) {
    if (!btn) return;
    btn.textContent = on ? '⤳ Bypassed' : 'Bypass';
    btn.className = 'px-2 py-1 rounded text-xs transition ' + (on
        ? 'bg-amber-700/40 text-amber-300 border border-amber-600/40'
        : 'bg-dark-600 hover:bg-dark-500 text-gray-300');
}

function tbToggleBypass(toneIdx, pIdx, btn) {
    const piece = tbState.songTones.tones[toneIdx].chain[pIdx];
    piece._bypassed = !piece._bypassed;
    tbUpdateBypassBtn(btn, piece._bypassed);
    // Persist the bypass for this song right away so it survives reload /
    // restart (it used to live only in memory until "Save preset").
    if (tbState.currentSongFile) tbPersistTone(toneIdx, tbState.currentSongFile);
    // If this tone is previewing, reload now. "bypassed" makes the engine
    // pass the signal THROUGH the stage (not silence it), so the rest of
    // the chain keeps working — exactly the requested behaviour.
    if (tbState.listeningTone === toneIdx) tbReloadPreview();
}

// Stamp each chain stage's `bypassed` from its matching UI piece.
function tbApplyBypassToChain(payload, toneIdx) {
    const tone = tbState.songTones && tbState.songTones.tones[toneIdx];
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
async function tbReloadPreview(refetchPresetId) {
    if (tbState.listeningTone === null || tbState._previewMode !== 'native') return;
    const api = tbNativeAudio();
    if (!api) return;
    if (refetchPresetId != null) {
        try {
            tbState._previewPayload = await (await fetch(`${TB_API}/native_preset_full/${refetchPresetId}`)).json();
        } catch (e) { console.warn('[nam_rig_builder] refetch preview failed', e); return; }
    }
    const payload = tbState._previewPayload;
    if (!payload) return;
    tbApplyBypassToChain(payload, tbState.listeningTone);
    try {
        if (api.clearChain) await api.clearChain().catch(() => {});
        await api.loadPreset(JSON.stringify(payload.native_preset));
        if (api.setMonitorMute) await api.setMonitorMute(false).catch(() => {});
    } catch (e) { console.warn('[nam_rig_builder] reload preview failed', e); }
}

// Re-render the open song's tone cards from current in-memory state (keeps
// _uploaded_file + _bypassed), restoring the active preview button label.
function tbRerenderSong() {
    const el = document.getElementById('tb-song-tones');
    if (!el || !tbState.songTones || !tbState.currentSongFile) return;
    el.innerHTML = tbState.songTones.tones
        .map((t, idx) => tbRenderTone(t, idx, tbState.currentSongFile)).join('');
    if (tbState.listeningTone !== null) {
        const b = document.getElementById(`tb-listen-${tbState.listeningTone}`);
        if (b) b.textContent = '⏸ Stop';
    }
}

// Call after any gear change (upload / RS-IR assign / download-and-assign):
// reflect it in the UI immediately, and if the affected tone is previewing,
// re-save + reload the chain so the new gear is audible at once.
async function tbAfterGearChange(toneIdx) {
    tbRerenderSong();
    // Auto-persist so per-song gear changes survive reload: an upload /
    // RS-IR assign used to live only in memory until "Save preset". For the
    // download path (toneIdx == null) the global _assign_file_to_gear already
    // wrote the DB, but we still persist the listening tone to capture any
    // in-memory edits and to reload its preview.
    const idx = (toneIdx != null) ? toneIdx : tbState.listeningTone;
    if (idx != null && tbState.currentSongFile) {
        const pid = await tbPersistTone(idx, tbState.currentSongFile);
        if (pid !== null && tbState.listeningTone === idx) await tbReloadPreview(pid);
    }
}

// ── Single-stage audition (catalog ▶ and search-candidate ▶) ──────────
// Loads ONE NAM/IR stage into the engine so you hear that gear in
// isolation. `btnId` is the toggling button; calling again stops it.
async function tbAuditionFile(file, kind, btnId) {
    const btn = btnId ? document.getElementById(btnId) : null;
    if (tbState._auditionId === btnId) { await tbStopPreview(); return; }
    await tbStopPreview();   // stop any other preview/audition first
    const api = tbNativeAudio();
    if (!api) { alert('Audio engine unavailable. Open the “NAM” plugin once to initialize it.'); return; }
    if (btn) { btn.disabled = true; btn.textContent = '⏳'; }
    try {
        const url = `${TB_API}/native_preset_one?file=${encodeURIComponent(file)}&kind=${encodeURIComponent(kind || 'nam')}`;
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
        tbState._previewStartedAudio = !wasRunning;
        tbState._previewMode = 'native';
        tbState._auditionId = btnId;
        if (btn) { btn.disabled = false; btn.textContent = '⏸'; }
    } catch (e) {
        if (btn) { btn.disabled = false; btn.textContent = '▶'; }
        alert(`Could not play: ${e && e.message ? e.message : e}`);
    }
}

// Search-candidate ▶: download the capture (no assign) then audition it.
async function tbAuditionCandidate(btn, rsGear, toneId) {
    if (!btn.id) btn.id = `tb-cand-${toneId}`;
    const btnId = btn.id;
    if (tbState._auditionId === btnId) { await tbStopPreview(); return; }
    await tbStopPreview();
    const old = btn.textContent;
    btn.disabled = true; btn.textContent = '⏳';
    try {
        const r = await fetch(`${TB_API}/audition_candidate`, {
            method: 'POST', headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ rs_gear: rsGear, tone3000_id: toneId }),
        });
        const data = await r.json();
        if (!r.ok) { alert(data.error || `HTTP ${r.status}`); btn.disabled = false; btn.textContent = old; return; }
        btn.disabled = false;
        await tbAuditionFile(data.file, data.kind, btnId);
    } catch (e) {
        btn.disabled = false; btn.textContent = old;
        alert(`Could not download/listen: ${e && e.message ? e.message : e}`);
    }
}

// ── Gear catalog grouped by type ─────────────────────────────
let _tbCatalogSeq = 0;
async function tbLoadCatalog() {
    const el = document.getElementById('tb-catalog');
    if (!el) return;
    if (tbState._auditionId) await tbStopPreview();   // stop stale audition before re-render
    el.innerHTML = '<p class="text-gray-500">Loading…</p>';
    let data;
    try { data = await (await fetch(`${TB_API}/gear_catalog`)).json(); }
    catch (e) { el.innerHTML = `<p class="text-red-400">Error: ${tbEsc(e.message)}</p>`; return; }
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
            const cards = items.map(g => tbRenderCatalogCard(g)).join('');
            return `
                <div>
                    <h3 class="text-white font-semibold mb-3">${tbEsc(LABEL[cat] || cat)} <span class="text-gray-500 text-xs">(${items.length})</span></h3>
                    <div class="grid grid-cols-1 md:grid-cols-2 gap-3">${cards}</div>
                </div>`;
        }).join('');
    } catch (e) {
        console.error('[nam_rig_builder] catalog render failed', e);
        el.innerHTML = `<p class="text-red-400">Error al renderizar: ${tbEsc(e.message)}</p>`;
    }
}

function tbRenderCatalogCard(g) {
    const btnId = `tb-aud-${_tbCatalogSeq++}`;
    const img = g.image
        ? `<img src="${tbEsc(g.image)}" alt="" loading="lazy" class="w-14 h-14 rounded object-cover bg-dark-900 flex-shrink-0" onerror="this.replaceWith(Object.assign(document.createElement('div'),{className:'w-14 h-14 rounded bg-dark-900 flex-shrink-0'}))">`
        : `<div class="w-14 h-14 rounded bg-dark-900 flex items-center justify-center text-gray-700 text-[10px] flex-shrink-0">no photo</div>`;
    const parent = g.assigned
        ? `<span class="text-green-400" title="${tbEsc(g.file || '')}">✓ ${tbEsc(g.tone3000_title || g.file || 'asignado')}</span>`
        : `<span class="text-gray-500">(unassigned)</span>`;
    const listenBtn = g.assigned
        ? `<button id="${btnId}" onclick="tbAuditionFile('${tbEsc(g.file).replace(/'/g,"\\'")}', '${tbEsc(g.kind || 'nam')}', '${btnId}')"
                   title="Listen to this gear in isolation" class="bg-dark-600 hover:bg-dark-500 text-gray-200 px-2.5 py-1 rounded text-xs flex-shrink-0">▶</button>`
        : '';
    const t3kLink = g.tone3000_url
        ? `<a href="${tbEsc(g.tone3000_url)}" target="_blank" title="Ver en tone3000" class="text-xs text-gray-500 hover:text-gray-300 flex-shrink-0">↗</a>` : '';
    return `
        <div class="bg-dark-700/50 border border-gray-800/50 rounded-lg p-3 flex items-center gap-3">
            ${img}
            <div class="min-w-0 flex-1">
                <div class="text-gray-200 truncate">${tbEsc(g.real_name)}</div>
                <div class="text-xs text-gray-500 truncate">${tbEsc(g.rs_gear)}</div>
                <div class="text-xs mt-0.5 truncate">${parent}</div>
            </div>
            <div class="flex items-center gap-2 flex-shrink-0">
                ${t3kLink}${listenBtn}
                <button onclick="tbOpenSuggest('${tbEsc(g.rs_gear)}')"
                        class="bg-dark-600 hover:bg-dark-500 text-gray-200 px-2.5 py-1 rounded text-xs">Search</button>
            </div>
        </div>`;
}

// Preview a tone LIVE through the full chain. Persists the selection, then
// asks the backend for a native_preset containing EVERY NAM stage (pedal →
// amp → …) + the cab IR, and loads it straight into the native engine — so
// this both *tests* and *realises* multi-NAM playback without touching the
// app bundle. The engine's `slotsLoaded` (logged to the console) tells us
// how many stages it actually accepted. If there's no native engine
// (WASM-only), it falls back to nam_tone's single-NAM preview.
async function tbListenTone(toneIdx, filename) {
    const btn = document.getElementById(`tb-listen-${toneIdx}`);

    // Toggle off if this tone is already previewing.
    if (tbState.listeningTone === toneIdx) {
        await tbStopPreview();
        if (btn) btn.textContent = '▶ Listen';
        return;
    }
    // Stop a different tone's preview first.
    if (tbState.listeningTone !== null) {
        const prev = document.getElementById(`tb-listen-${tbState.listeningTone}`);
        await tbStopPreview();
        if (prev) prev.textContent = '▶ Listen';
    }

    if (btn) { btn.disabled = true; btn.textContent = '⏳ Loading…'; }
    const presetId = await tbPersistTone(toneIdx, filename);
    if (presetId === null) { if (btn) { btn.disabled = false; btn.textContent = '▶ Listen'; } return; }

    const api = tbNativeAudio();
    try {
        if (api) {
            const payload = await (await fetch(`${TB_API}/native_preset_full/${presetId}`)).json();
            const chain = (payload.native_preset && payload.native_preset.chain) || [];
            if (chain.length === 0) {
                alert('This tone has no pieces with an assigned file yet.');
                if (btn) { btn.disabled = false; btn.textContent = '▶ Listen'; }
                return;
            }
            tbState._previewPayload = payload;
            tbApplyBypassToChain(payload, toneIdx);   // honour any pre-set bypasses
            if (api.clearChain) await api.clearChain().catch(() => {});
            const res = await api.loadPreset(JSON.stringify(payload.native_preset));
            const got = res && res.slotsLoaded;
            console.log(`[nam_rig_builder] chain sent=${chain.length} (NAM=${payload.nam_stage_count}) · slotsLoaded=${got}`, res);
            if (!res || res.success === false) throw new Error((res && res.error) || 'loadPreset failed');
            if (api.setGain) { await api.setGain('input', 1.0).catch(() => {}); await api.setGain('chain', 1.0).catch(() => {}); }
            if (api.setMonitorMute) await api.setMonitorMute(false).catch(() => {});
            const wasRunning = api.isAudioRunning ? await api.isAudioRunning().catch(() => true) : true;
            await api.startAudio();
            tbState._previewStartedAudio = !wasRunning;
            tbState._previewMode = 'native';
            tbState.listeningTone = toneIdx;
            if (btn) {
                btn.disabled = false;
                btn.textContent = '⏸ Stop';
                btn.title = `Chain: ${chain.length} stages (NAM=${payload.nam_stage_count}); engine loaded ${got}`;
            }
            if (payload.nam_stage_count >= 2 && typeof got === 'number' && got < chain.length) {
                console.warn(`[nam_rig_builder] engine loaded ${got}/${chain.length} stages → it does not chain all NAMs`);
            }
        } else if (typeof window.namStartPresetTest === 'function') {
            await window.namStartPresetTest(presetId);   // WASM fallback: single NAM
            tbState._previewMode = 'nam';
            tbState.listeningTone = toneIdx;
            if (btn) { btn.disabled = false; btn.textContent = '⏸ Stop'; btn.title = '1-NAM preview (WASM engine, no chaining)'; }
        } else {
            alert('Audio engine unavailable. Open the “NAM” plugin once to initialize it.');
            if (btn) { btn.disabled = false; btn.textContent = '▶ Listen'; }
        }
    } catch (e) {
        await tbStopPreview();
        if (btn) { btn.disabled = false; btn.textContent = '▶ Listen'; }
        alert(`Could not play: ${e && e.message ? e.message : e}`);
    }
}

// ── Settings ───────────────────────────────────────────────────────

async function tbLoadSettings() {
    let s;
    try {
        const r = await fetch(`${TB_API}/settings`);
        s = await r.json();
    } catch (e) {
        return;
    }
    document.getElementById('tb-aggressive').checked = !!s.aggressive;
    document.getElementById('tb-min-downloads').value = s.min_downloads;
    const status = document.getElementById('tb-api-key-status');
    if (s.has_tone3000_key) {
        status.innerHTML = `<span class="text-green-400">Key configured (${tbEsc(s.tone3000_api_key_preview)})</span>`;
    } else {
        status.textContent = 'No key. Deep-link mode active.';
    }
}

async function tbSaveApiKey() {
    const key = document.getElementById('tb-api-key').value.trim();
    if (!key) return;
    await fetch(`${TB_API}/settings`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ tone3000_api_key: key }),
    });
    document.getElementById('tb-api-key').value = '';
    tbLoadSettings();
    tbInit();  // refresh status banner
}

async function tbSaveSettings() {
    const aggressive = document.getElementById('tb-aggressive').checked;
    const min_downloads = parseInt(document.getElementById('tb-min-downloads').value, 10) || 0;
    await fetch(`${TB_API}/settings`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ aggressive, min_downloads }),
    });
}

// Triggered from the Suggest modal: download a specific tone3000
// capture for an rs_gear, then update any open per-song chain so
// "Save preset" picks the new file up without a re-fetch.
async function tbDownloadForGear(btn, rsGear, toneId) {
    btn.disabled = true;
    btn.textContent = 'Downloading…';
    // Downloading from tone3000 can take a while (and ffmpeg-normalizes
    // IRs server-side), but bound it so a stalled CDN connection turns
    // into a visible error instead of a button stuck on "Downloading…".
    const ctrl = new AbortController();
    const timer = setTimeout(() => ctrl.abort(), 180000);
    try {
        const r = await fetch(`${TB_API}/download_for_gear`, {
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
        if (tbState.songTones) {
            for (const t of tbState.songTones.tones) {
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
        if (tbState.currentTab === 'pending') tbLoadPending();
        else if (tbState.currentTab === 'dashboard') tbLoadCoverage();
        else if (tbState.currentTab === 'gear') tbLoadCatalog();
        // Reflect the new assignment in the open song view now (and
        // re-audition if a tone using this gear is currently previewing) —
        // no need to re-select the song.
        tbAfterGearChange(null);
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

async function tbExtractGearMap() {
    const path = document.getElementById('tb-gears-psarc').value.trim();
    if (!path) return;
    const status = document.getElementById('tb-extract-status');
    status.textContent = 'Extracting…';
    try {
        const r = await fetch(`${TB_API}/extract_gear_map`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ gears_psarc: path }),
        });
        const data = await r.json();
        if (!r.ok) {
            status.innerHTML = `<span class="text-red-400">Error: ${tbEsc(data.error || r.status)}</span>`;
            return;
        }
        status.innerHTML = `<span class="text-green-400">Done: ${data.count} entries. Reloading status…</span>`;
        tbInit();
    } catch (e) {
        status.innerHTML = `<span class="text-red-400">${tbEsc(e.message)}</span>`;
    }
}

async function tbExtractIRs() {
    const path = document.getElementById('tb-irs-psarc').value.trim();
    if (!path) return;
    const status = document.getElementById('tb-extract-irs-status');
    status.textContent = 'Extracting IRs (may take 30-60s)…';
    try {
        const r = await fetch(`${TB_API}/extract_irs`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ gears_psarc: path }),
        });
        const data = await r.json();
        if (!r.ok) {
            status.innerHTML = `<span class="text-red-400">Error: ${tbEsc(data.error || r.status)}</span>`;
            return;
        }
        status.innerHTML = `<span class="text-green-400">Done: ${data.count} RS entities with IR. Reloading status…</span>`;
        tbInit();
    } catch (e) {
        status.innerHTML = `<span class="text-red-400">${tbEsc(e.message)}</span>`;
    }
}
