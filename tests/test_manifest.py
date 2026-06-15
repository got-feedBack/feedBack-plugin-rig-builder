from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def _manifest() -> dict:
    return json.loads((ROOT / "plugin.json").read_text())


def test_manifest_declares_capability_standards():
    manifest = _manifest()

    assert "capability-pipelines.v1" in manifest["standards"]
    assert "plugin-runtime-idempotent.v1" in manifest["standards"]


def test_manifest_declares_playback_observer_for_008():
    playback = _manifest()["capabilities"]["playback"]

    assert playback["roles"] == ["observer"]
    assert playback["kind"] == "lifecycle"
    assert playback["observes"] == ["ready", "stopped", "ended"]
    assert playback["compatibility"] == "shim-allowed"
    assert playback["ownership"] == "observer-only"
    assert playback["safety"] == "safe"
    assert playback["version"] == 1


def test_manifest_keeps_audio_effects_jobs_and_privileged_surfaces():
    capabilities = _manifest()["capabilities"]

    assert capabilities["audio-effects"]["roles"] == ["provider", "requester", "observer"]
    assert "select-chain" in capabilities["audio-effects"]["commands"]
    assert "upsert-mapping" in capabilities["audio-effects"]["requests"]
    assert capabilities["audio-effects"]["operations"] == ["chain.resolve", "chain.inspect", "segment.activate", "stage.set-bypass", "stage.set-parameter", "fallback"]
    assert capabilities["jobs"]["roles"] == ["provider", "observer"]
    assert "job.enqueue" in capabilities["jobs"]["operations"]
    assert capabilities["privileged-capabilities"]["roles"] == ["provider", "requester", "observer"]
    assert "check-approval-boundary" in capabilities["privileged-capabilities"]["requests"]


def test_screen_registers_executable_audio_effects_provider():
    src = (ROOT / "screen.js").read_text()

    assert "RB_EFFECTS_PLAN_SCHEMA = 'slopsmith.audio_effects.chain_plan.v1'" in src
    assert "rbAudioEffectsApi" in src
    assert "registerProvider" in src
    assert "requests: ['select-chain', 'bypass', 'restore', 'fallback', 'inspect-route', 'upsert-mapping']" in src
    assert "operationHandlers: rbAudioEffectsOperationHandlers()" in src
    assert "'chain.resolve'" in src
    assert "rbBuildAudioEffectsRequestFromPayload" in src
    assert "function rbToneSegmentId" in src
    assert "stageBypass" in src
    assert "invalid-provider-ref" in src
    assert "assets[assetRef] = asset" in src
    assert "asset.stateBase64 = stage.state" in src
    assert "rbLoadChainPlanWithHost" in src
    assert "rbActivateSegmentWithHost" in src
    assert "audioEffects.activateSegment" in src
    assert "rbReleaseAudioEffectsRouteWithHost" in src
    assert "audioEffects.releaseRoute" in src
    assert "rbSetRouteGainsWithHost" in src
    assert "audioEffects.setRouteGain" in src
    assert "rbAudioEffectsLoadOptionsForChain" in src
    assert "options: opts.executorOptions || {}" in src
    assert "rbRegisterAudioEffectsCapability();\n    const audioEffects = rbAudioEffectsApi();" in src
    assert "loadPlan" in src
    assert "rbLoadNativePresetPayload" in src
    assert "rbUpsertAudioEffectsMapping" in src
    assert "provider_ref: rbPresetProviderRef(presetId)" in src
    assert "rbPresetIdFromProviderRef(providerRef)" in src
    assert "body.mirrored_presets" in src
    assert "invalid-response" in src
    assert "resolve-failed" in src
    assert "rbRecordAudioEffectsBridge(reason)" in src
    assert "audio-effects.legacy-tone-db" in src
    assert "rbRecordLegacyToneDbBridge" in src
    assert "audio-effects.legacy-native-load" in src
    assert "rbRecordLegacyNativeLoadBridge" in src
    assert "rbFetchLegacyNamToneMappings(filename)" in src
    assert "owner=rig_builder" in src
    assert "save_preset persisted provider-private legacy tone database rows" in src
    assert src.count("/api/plugins/nam_tone/mappings/") == 1
    assert "requesterId" not in src
    assert "RB_AUDIO_EFFECTS_ROUTE_KEY" not in src
    assert "window.__rbPlaybackSettingsKey = ''" in src
    assert "window.__rbPlaybackSettingsFilename" in src
    assert "slopsmithDesktop.audioEffects" not in src
    assert "function rbEnsureCapabilitiesRegistered" in src
    assert "rbEnsureCapabilitiesRegistered(0)" in src


def test_screen_coalesces_mega_chain_lifecycle_builds():
    src = (ROOT / "screen.js").read_text()

    assert "let _pendingBuildTimer = null" in src
    assert "let _pendingBuildFile = null" in src
    assert "let _buildingFile = null" in src
    assert "build already scheduled" in src
    assert "build already running" in src
    assert "chain already active" in src
    assert "_pendingBuildFile !== filename" in src


def test_screen_blocks_amp_button_while_mega_chain_active():
    src = (ROOT / "screen.js").read_text()

    assert "let _ampToggleAllowed = false" in src
    assert "let _pending = false" in src
    assert "function isPending()" in src
    assert "function settingKnown()" in src
    assert "function rbSelectAudioEffectsRoute" in src
    assert "audioEffects.selectChain" in src
    assert "authorization: 'restore-selection'" in src
    assert "rbSelectAudioEffectsRoute('mega-chain-pending')" in src
    assert "const activatedByHost = await rbActivateSegmentWithHost" in src
    assert "if (!activatedByHost)" in src
    assert "executorOptions: rbAudioEffectsLoadOptionsForChain" in src
    assert "const releasedByHost = await rbReleaseAudioEffectsRouteWithHost" in src
    assert "if (!releasedByHost && api && api.clearChain)" in src
    assert "waiting for /settings before build" in src
    assert "settings-ready catch-up" in src
    assert "function rbInjectPlayerToneButton()" in src
    assert "btn.id = 'btn-rig-tones'" in src
    assert "Rig Tones On" in src
    assert "Rig Tones Loading" in src
    assert "Rig Tones Failed" in src
    assert "bg-red-700/50" in src
    assert "function _markFailed" in src
    assert "state.failed" in src
    assert "Rig Tones Off" in src
    assert "RbMegaChain.markPending(filename)" in src
    assert "window.__rbAmpClickBlockerInstalled" in src
    assert "event.stopImmediatePropagation()" in src
    assert "AMP button click ignored" in src
    assert "let _ampRecoveryTimer = null" in src
    assert "window.RbMegaChain = api" in src


def test_screen_handles_excess_engine_slots_without_negative_mismatch():
    src = (ROOT / "screen.js").read_text()

    assert "loaded.length > expected ? loaded.slice(loaded.length - expected) : loaded" in src
    assert "engine reported ${loaded.length} total slots" in src
    assert "} else if (got < expected)" in src
    assert "const skipped = expected - got" in src


def test_routes_return_mirrored_preset_ids_for_mapping_refs():
    src = (ROOT / "routes.py").read_text()

    assert "mirrored_presets" in src
    assert "mirror_preset_id = _persist_preset_chain" in src


def test_legacy_tone_db_access_is_explicitly_inventoried():
    src = (ROOT / "routes.py").read_text()

    assert src.count('"nam_tone.db"') == 1
    assert "INSERT OR REPLACE INTO tone_mappings" in src
    assert "def _dlc_relative_song_key" in src
    assert "def _tone_mapping_filename_filter" in src
    assert "SELECT DISTINCT filename FROM tone_mappings WHERE NOT ({_filename_filter})" in src
    assert "WHERE {_filename_filter}" in src
    assert "filename IN ({placeholders})" in src
    assert "FROM tone_mappings tm JOIN presets p ON tm.preset_id = p.id" in src
    assert "EXISTS (SELECT 1 FROM preset_pieces pp WHERE pp.preset_id = tone_mappings.preset_id)" in src
    assert "EXISTS (SELECT 1 FROM preset_pieces pp WHERE pp.preset_id = tm.preset_id)" in src
