// Pure-logic tests for the opt-in VST prompt + rig achievements: load screen.js
// in a bare vm window and exercise the window.__rbTest seam (no DOM, no network).
const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

function load(seed) {
    const store = Object.assign({}, seed);
    const noopEl = { classList: { add() {}, remove() {} }, addEventListener() {},
        querySelector: () => null, prepend() {}, remove() {}, innerHTML: '' };
    const window = {
        console,
        RB_API: '/api/plugins/rig_builder',
        localStorage: {
            getItem: (k) => (k in store ? store[k] : null),
            setItem: (k, v) => { store[k] = String(v); },
        },
        document: {
            getElementById: () => null,
            createElement: () => Object.assign({}, noopEl),
            addEventListener() {},
            querySelectorAll: () => [],
            head: { appendChild() {} },
            body: { appendChild() {} },
        },
        addEventListener() {},
        setTimeout: () => 0,
        clearTimeout() {},
        setInterval: () => 0,
        clearInterval() {},
        requestAnimationFrame: () => 0,
        fetch: () => Promise.resolve({ ok: true, json: () => Promise.resolve({}) }),
        slopsmith: { on() {} },
    };
    window.window = window;
    window.globalThis = window;
    const ctx = vm.createContext(window);
    ctx.document = window.document;
    ctx.localStorage = window.localStorage;
    const src = fs.readFileSync(path.join(__dirname, '..', 'screen.js'), 'utf8');
    vm.runInContext(src, ctx, { filename: 'rig-builder/screen.js' });
    return window;
}

test('screen.js loads and exposes the __rbTest seam', () => {
    const t = load().__rbTest;
    assert.equal(typeof t.rbFormatBytes, 'function');
    assert.equal(t.RB_ACHIEVEMENTS.length, 3);
    assert.equal(t.RB_ACHIEVEMENTS.map((a) => a.id).sort().join(','),
        'rig_from_tone,rig_gearhead,rig_simulated');
    for (const a of t.RB_ACHIEVEMENTS) assert.equal(a.sourceId, 'rig_builder');
});

test('rbFormatBytes renders the download-size disclosure', () => {
    const f = load().__rbTest.rbFormatBytes;
    assert.equal(f(210 * 1024 * 1024), '~210 MB');
    assert.equal(f(2 * 1024 * 1024 * 1024), '~2.0 GB');
    assert.equal(f(0), '');
    assert.equal(f(null), '');
});

test('rbRigSaveAchievement picks gearhead vs from-tone by download history', () => {
    const t = load().__rbTest;
    assert.equal(t.rbRigSaveAchievement(true), 'rig_from_tone');
    assert.equal(t.rbRigSaveAchievement(false), 'rig_gearhead');
});

test('rbHasDownloadedTone reads the latched localStorage flag', () => {
    assert.equal(load().__rbTest.rbHasDownloadedTone(), false);
    const w = load({ 'feedBack-rig-downloaded-tone': '1' });
    assert.equal(w.__rbTest.rbHasDownloadedTone(), true);
});
