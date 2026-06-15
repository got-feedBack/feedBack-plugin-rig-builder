"""Wrapper around tone3000.com — REST API when an API key is available,
public-website deep-links as a no-auth fallback.

Verified against the live service: every `/api/v1/*` endpoint returns
401 without a Bearer token, so the only way to drive auto-mode (batch
suggestion + download) is with a key the user has obtained from
tone3000. The deep-link path (`https://www.tone3000.com/search?...`)
returns HTML pages the user can browse manually — useful as v0 default.

If the user later pastes a key into the plugin's settings, the same
client switches into REST mode and the auto-suggest UI lights up.
"""

import base64
import hashlib
import json
import os
import secrets
import sqlite3
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from typing import Any

BASE_URL = "https://www.tone3000.com/api/v1"
WEB_BASE_URL = "https://www.tone3000.com"
OAUTH_AUTHORIZE_URL = f"{BASE_URL}/oauth/authorize"
OAUTH_TOKEN_URL = f"{BASE_URL}/oauth/token"

_USER_AGENT = "slopsmith-tone-bridge/0.0.1"

# Publishable key (OAuth client_id) for the Rig Builder integration. The
# publishable key is designed to be embedded in client code — tone3000's
# docs: "Safe to include in client-side code, mobile apps, and browser
# environments." It only *identifies* the app in the OAuth flow; it cannot
# read or download anything on its own (every API call needs the per-user
# access token minted by the flow). Swap this for a dedicated "Rig Builder"
# app's publishable key if/when one is registered.
DEFAULT_PUBLISHABLE_KEY = "t3k_pub_bngrHev00no0ikJOb4KSy0xJ8Hovvh72"

# Cache TTL: the tone catalog grows daily, but for our purposes a week
# is plenty — captures don't change once uploaded, only new ones appear.
_CACHE_TTL_SECONDS = 7 * 24 * 3600


class Tone3000Client:
    """Minimal client for tone3000's read-only tone search & model APIs.

    `cache_db_path` is created if missing; it stores serialized search
    responses keyed by the request URL so that repeating the same
    library-wide batch hits the network at most once per unique gear.
    """

    def __init__(
        self,
        cache_db_path: str,
        api_key: str | None = None,
        *,
        access_token: str | None = None,
        refresh_token: str | None = None,
        token_expires_at: float = 0,
        publishable_key: str | None = None,
        on_tokens=None,
        timeout: float = 15.0,
    ):
        # Two ways to authenticate, checked in this order:
        #  1. OAuth access token (preferred — minted by a user login, short-
        #     lived, refreshed automatically). Used as Bearer.
        #  2. Secret key (`t3k_cs_…`) pasted by the user (advanced/legacy
        #     server-to-server path). Also used as Bearer.
        self.api_key = api_key or os.environ.get("SLOPSMITH_TONE3000_KEY")
        self.access_token = access_token or None
        self.refresh_token = refresh_token or None
        self.token_expires_at = float(token_expires_at or 0)
        self.publishable_key = publishable_key or DEFAULT_PUBLISHABLE_KEY
        # Called with a dict of settings keys whenever tokens change so the
        # caller can persist them (access tokens rotate on every refresh).
        self.on_tokens = on_tokens
        self.timeout = timeout
        # A 401 we can't recover from flips this flag. The UI uses it to
        # switch from auto-mode to deep-link mode rather than making the
        # user paste/edit a config file.
        self.has_api_access = bool(self.access_token or self.api_key)
        self._lock = threading.Lock()
        self._conn = sqlite3.connect(cache_db_path, check_same_thread=False)
        self._conn.execute("PRAGMA journal_mode=WAL")
        self._conn.execute(
            """
            CREATE TABLE IF NOT EXISTS search_cache (
                url TEXT PRIMARY KEY,
                fetched_at INTEGER NOT NULL,
                response_json TEXT NOT NULL
            )
            """
        )
        self._conn.commit()

    # ── HTTP plumbing ───────────────────────────────────────────────

    def _bearer(self) -> str | None:
        """The token to send as `Authorization: Bearer`. OAuth access token
        wins over the secret key when both are present."""
        return self.access_token or self.api_key

    def _raw_get(self, url: str) -> Any:
        req = urllib.request.Request(url)
        req.add_header("Accept", "application/json")
        req.add_header("User-Agent", _USER_AGENT)
        bearer = self._bearer()
        if bearer:
            req.add_header("Authorization", f"Bearer {bearer}")
        with urllib.request.urlopen(req, timeout=self.timeout) as resp:
            return json.loads(resp.read().decode("utf-8"))

    def _http_get(self, url: str) -> Any:
        # Refresh proactively if the access token is about to expire, then
        # do the request. A 401 still triggers a one-shot refresh + retry in
        # case the server expired it early.
        self._ensure_fresh_token()
        try:
            return self._raw_get(url)
        except urllib.error.HTTPError as e:
            if e.code == 401:
                if self.access_token and self._refresh_token():
                    try:
                        return self._raw_get(url)
                    except urllib.error.HTTPError as e2:
                        if e2.code == 401:
                            self.has_api_access = False
                            return None
                        raise
                # No recoverable credential — just means the user hasn't
                # connected / pasted a key. The UI handles the fallback flow;
                # callers see an empty payload.
                self.has_api_access = False
                return None
            raise

    # ── OAuth (PKCE) ────────────────────────────────────────────────────

    @staticmethod
    def generate_pkce() -> tuple[str, str]:
        """Return (code_verifier, code_challenge) for an OAuth PKCE flow.
        Challenge = base64url(SHA-256(verifier)), no padding, per the spec."""
        verifier = secrets.token_urlsafe(64)[:128]
        digest = hashlib.sha256(verifier.encode("ascii")).digest()
        challenge = base64.urlsafe_b64encode(digest).decode("ascii").rstrip("=")
        return verifier, challenge

    def build_authorize_url(
        self,
        redirect_uri: str,
        code_challenge: str,
        state: str,
        *,
        prompt: str | None = None,
        gears: str | None = None,
        platform: str | None = None,
        login_hint: str | None = None,
    ) -> str:
        """Build the `/oauth/authorize` URL the user opens in their browser."""
        params: dict[str, str] = {
            "client_id": self.publishable_key,
            "redirect_uri": redirect_uri,
            "response_type": "code",
            "code_challenge": code_challenge,
            "code_challenge_method": "S256",
            "state": state,
        }
        if prompt:
            params["prompt"] = prompt
        if gears:
            params["gears"] = gears
        if platform:
            params["platform"] = platform
        if login_hint:
            params["login_hint"] = login_hint
        return f"{OAUTH_AUTHORIZE_URL}?{urllib.parse.urlencode(params)}"

    def _post_token(self, body: dict) -> dict:
        data = urllib.parse.urlencode(body).encode("ascii")
        req = urllib.request.Request(OAUTH_TOKEN_URL, data=data, method="POST")
        req.add_header("Content-Type", "application/x-www-form-urlencoded")
        req.add_header("Accept", "application/json")
        req.add_header("User-Agent", _USER_AGENT)
        with urllib.request.urlopen(req, timeout=self.timeout) as resp:
            return json.loads(resp.read().decode("utf-8"))

    def exchange_code(self, code: str, code_verifier: str, redirect_uri: str) -> dict:
        """Exchange an authorization code for tokens (called from the OAuth
        callback). Applies + persists the resulting tokens."""
        tokens = self._post_token({
            "grant_type": "authorization_code",
            "code": code,
            "code_verifier": code_verifier,
            "redirect_uri": redirect_uri,
            "client_id": self.publishable_key,
        })
        self._apply_tokens(tokens)
        return tokens

    def _ensure_fresh_token(self) -> None:
        """Refresh the access token if it's missing/expiring within 60s."""
        if not self.access_token or not self.refresh_token:
            return
        if self.token_expires_at and time.time() < self.token_expires_at - 60:
            return
        self._refresh_token()

    def _refresh_token(self) -> bool:
        """Use the refresh token to mint a new access token. Returns True on
        success. On `invalid_grant` (refresh token expired) the tokens are
        cleared so the UI prompts a re-connect."""
        if not self.refresh_token:
            return False
        try:
            tokens = self._post_token({
                "grant_type": "refresh_token",
                "refresh_token": self.refresh_token,
                "client_id": self.publishable_key,
            })
        except urllib.error.HTTPError as e:
            if e.code == 400:  # invalid_grant → refresh token expired
                self._clear_tokens()
            return False
        except urllib.error.URLError:
            return False
        self._apply_tokens(tokens)
        return True

    def _apply_tokens(self, tokens: dict) -> None:
        if tokens.get("access_token"):
            self.access_token = tokens["access_token"]
        # tone3000 rotates the refresh token on each exchange/refresh.
        if tokens.get("refresh_token"):
            self.refresh_token = tokens["refresh_token"]
        expires_in = tokens.get("expires_in")
        if expires_in:
            self.token_expires_at = time.time() + float(expires_in)
        self.has_api_access = bool(self.access_token or self.api_key)
        self._emit_tokens()

    def _clear_tokens(self) -> None:
        self.access_token = None
        self.refresh_token = None
        self.token_expires_at = 0
        self.has_api_access = bool(self.api_key)
        self._emit_tokens()

    def _emit_tokens(self) -> None:
        if not self.on_tokens:
            return
        try:
            self.on_tokens({
                "tone3000_access_token": self.access_token or "",
                "tone3000_refresh_token": self.refresh_token or "",
                "tone3000_token_expires_at": self.token_expires_at or 0,
            })
        except Exception:
            pass

    def get_user(self) -> dict | None:
        """Currently authenticated user (id, username, …). Not cached."""
        return self._http_get(f"{BASE_URL}/user")

    def _cached_get(self, url: str) -> Any:
        """GET with a sqlite cache. Stale entries are ignored, not removed —
        kept for debugging and because we may want them as a fallback if
        tone3000 is offline."""
        now = int(time.time())
        with self._lock:
            row = self._conn.execute(
                "SELECT fetched_at, response_json FROM search_cache WHERE url = ?",
                (url,),
            ).fetchone()
        if row is not None and now - row[0] < _CACHE_TTL_SECONDS:
            return json.loads(row[1])

        data = self._http_get(url)
        payload = json.dumps(data)
        with self._lock:
            self._conn.execute(
                "INSERT OR REPLACE INTO search_cache (url, fetched_at, response_json) "
                "VALUES (?, ?, ?)",
                (url, now, payload),
            )
            self._conn.commit()
        return data

    # ── Public API ──────────────────────────────────────────────────

    def search_tones(
        self,
        query: str,
        gears: str | None = None,
        platform: str = "nam",
        sort: str = "downloads-all-time",
        page: int = 1,
        page_size: int = 10,
    ) -> dict:
        """Run a paginated search.

        gears: one of amp|full-rig|pedal|outboard|ir (see tone3000 docs).
        platform: nam|ir|aida-x|aa-snapshot|proteus. Defaults to nam since
        that's the only one we wire up in v0; we override to "ir" when
        searching for cabinets.
        """
        params: dict[str, str] = {
            "query": query,
            "page": str(page),
            "page_size": str(min(page_size, 25)),
            "sort": sort,
            "platform": platform,
        }
        if gears:
            params["gears"] = gears
        qs = urllib.parse.urlencode(params)
        return self._cached_get(f"{BASE_URL}/tones/search?{qs}")

    def list_models(self, tone_id: int) -> dict:
        """List downloadable .nam models attached to a tone.

        The `/models` endpoint paginates at 10 per page by default and
        caps at 25 even with `page_size` — a tone like the Gallien
        Krueger RB800 (19 captures, one per gain step) silently drops
        captures past the first page if you don't iterate. Loop until
        we get a short page; combine all `data` arrays into one
        payload so callers see the full list.

        The combined payload preserves any top-level metadata from the
        first page (`title`, `total_count`, etc.) so downstream code
        that reads those keys still works.
        """
        per_page = 25  # tone3000's max page_size for this endpoint
        first = self._cached_get(
            f"{BASE_URL}/models?tone_id={tone_id}&page=1&page_size={per_page}")
        if not isinstance(first, dict):
            return first
        first_data = list(first.get("data") or [])
        all_data = list(first_data)
        # Loop pages 2..N. Stop when a page comes back short — that
        # means we've reached the last one. The 40-page cap is a sanity
        # ceiling so a buggy API can't spin forever (40 × 25 = 1000
        # captures, way past any real tone).
        if len(first_data) == per_page:
            page = 2
            while page <= 40:
                extra = self._cached_get(
                    f"{BASE_URL}/models?tone_id={tone_id}&page={page}&page_size={per_page}")
                if not isinstance(extra, dict):
                    break
                extra_data = extra.get("data") or []
                if not extra_data:
                    break
                all_data.extend(extra_data)
                if len(extra_data) < per_page:
                    break
                page += 1
        # Return the first-page payload with the union of `data`. We
        # use dict(first) so the cached entry isn't mutated.
        merged = dict(first)
        merged["data"] = all_data
        return merged

    def get_model(self, model_id: int) -> dict:
        return self._cached_get(f"{BASE_URL}/models/{model_id}")

    def download_model_file(self, model_url: str, dest_path: str) -> int:
        """Stream a model file (.nam or .wav IR) to disk. Returns bytes
        written.

        The URL comes from a Model's `model_url` field. Empirically
        tone3000's download endpoint still requires the same Bearer
        token used for the JSON API (verified against the production
        service — model_url returns 401 without auth), so we re-send
        it. If a future API revision moves downloads to signed CDN
        URLs that reject the header, we'd see a different status code
        and can revisit; until then this is the only path that works.

        Idempotent: if dest_path already exists, skip the download and
        return the on-disk size. Batches across the user's library
        often hit the same tone3000 model many times (e.g. JCM800 used
        by 30 songs); skipping saves bandwidth and stays well below
        the 100 req/min rate limit even on huge libraries.
        """
        import os
        if os.path.exists(dest_path):
            return os.path.getsize(dest_path)

        # Retry policy for HTTP 429 (tone3000 enforces ~100 req/min).
        # Exponential backoff with jitter so a parallel client hitting
        # the same limit doesn't synchronise their retries on the same
        # tick. Honour the `Retry-After` header when present — that's
        # the API's own hint about when it's safe to come back. We cap
        # retries to 4 attempts so a sustained outage (auth revoked
        # mid-batch, server down) still surfaces as a real exception
        # instead of looping forever.
        max_attempts = 4
        last_exc = None
        for attempt in range(max_attempts):
            self._ensure_fresh_token()
            req = urllib.request.Request(model_url)
            req.add_header("User-Agent", _USER_AGENT)
            bearer = self._bearer()
            if bearer:
                req.add_header("Authorization", f"Bearer {bearer}")
            tmp_path = dest_path + ".part"
            total = 0
            try:
                with urllib.request.urlopen(req, timeout=120.0) as resp, open(tmp_path, "wb") as out:
                    while True:
                        chunk = resp.read(64 * 1024)
                        if not chunk:
                            break
                        out.write(chunk)
                        total += len(chunk)
                os.replace(tmp_path, dest_path)
                return total
            except urllib.error.HTTPError as e:
                last_exc = e
                try:
                    os.remove(tmp_path)
                except FileNotFoundError:
                    pass
                if e.code != 429 or attempt == max_attempts - 1:
                    raise
                # Backoff: prefer Retry-After if the server hinted one,
                # otherwise 2^attempt + jitter so successive retries
                # spread out. tone3000's 100-req/min budget refills
                # every ~0.6s, so even a single 8s pause clears the
                # window comfortably.
                retry_after = 0
                try:
                    retry_after = int(e.headers.get("Retry-After", "0"))
                except (TypeError, ValueError):
                    retry_after = 0
                sleep_s = retry_after if retry_after > 0 else (2 ** attempt + 0.5 * attempt)
                time.sleep(sleep_s)
            except Exception:
                try:
                    os.remove(tmp_path)
                except FileNotFoundError:
                    pass
                raise
        if last_exc:
            raise last_exc
        return total

    @staticmethod
    def build_search_url(query: str, gears: str | None = None, platform: str = "nam") -> str:
        """Build a deep-link URL into tone3000's public search page.

        Used when we don't have API access: clicking opens tone3000 in
        the browser with the right filters preselected. The user picks
        a capture, downloads the .nam, then drops it into the plugin's
        upload zone.
        """
        params: dict[str, str] = {"query": query, "platform": platform}
        if gears:
            params["gears"] = gears
        return f"{WEB_BASE_URL}/search?{urllib.parse.urlencode(params)}"

    @staticmethod
    def tone_page_url(tone_id: int | str) -> str:
        """Public web page for a single tone.

        The search API's per-tone `url` field is broken — it returns a
        slug path with a stray double slash (`…com//badcat-lynx-30122`)
        that 404s, and even the de-duplicated slug 404s. The canonical,
        working page is `/tones/{id}` (verified 200 against production),
        so we build the link from the id instead of trusting `url`.
        """
        return f"{WEB_BASE_URL}/tones/{tone_id}"


# Tone3000 publishes captures in several sizes. Bigger = closer to the
# original amp at the cost of disk and CPU. We pick by user preference
# but fall back through the list — some tones only have one size
# available.
#
# A1 (legacy) sizes: standard, lite, feather, nano. A2 (the architecture
# Tone3000 made the default in June 2026) leads with a "full" size. "full"
# sits just after "standard" in the *fallback* order: pick_best_model still
# honours the caller's preferred_size first (default "standard"), so a tone
# offering both a standard and a full capture keeps picking standard. Placing
# "full" after "standard" (rather than at the front) also means a caller who
# asked for a smaller size (lite/feather/nano) still falls back to standard
# before the heavier full. An A2-only tone — which has no "standard" — matches
# its flagship "full" capture (its only size) instead of dropping to the
# models[0] fallback. A tone exposing none of these sizes is unaffected.
_SIZE_PREFERENCE = ("standard", "full", "lite", "feather", "nano", "custom")


def pick_best_model(models_payload: dict, preferred_size: str = "standard") -> dict | None:
    """Choose one Model from a list_models() response.

    `models_payload` is the dict returned by `Tone3000Client.list_models`,
    carrying a `data` array of Model objects (id, model_url, size, …).
    Walks the size preference list starting at `preferred_size` and
    returns the first model that matches.
    """
    if not isinstance(models_payload, dict):
        return None
    models = models_payload.get("data") or []
    if not models:
        return None

    # Build the search order — user's preferred size first, then the
    # remaining sizes in fallback order. Anything in `models` whose
    # `size` field doesn't match any of these is considered last.
    order = [preferred_size] + [s for s in _SIZE_PREFERENCE if s != preferred_size]
    by_size: dict[str, dict] = {}
    for m in models:
        size = (m.get("size") or "").lower().strip()
        # First occurrence per size wins. The list_models response is
        # sorted by the tone owner; we don't have a deeper signal.
        by_size.setdefault(size, m)
    for size in order:
        if size in by_size:
            return by_size[size]
    # Nothing matched the canonical sizes — return whatever's first.
    return models[0]


# ── Policy: filter raw search results into auto-mode candidates ──────

# The conservative policy mirrors what the design doc agreed: only
# permissive licenses, a minimum download count to weed out broken/
# experimental uploads, and a preference for full-size captures. The
# "aggressive" version drops the floor and license filter.
_PERMISSIVE_LICENSES = {"cc0", "cc-by", "cc by", "public domain"}


def pick_top_candidate(
    search_result: dict,
    *,
    aggressive: bool = False,
    min_downloads: int = 50,
) -> dict | None:
    """Return the best candidate from a tone3000 search payload, or None
    if nothing passes the policy.

    `search_result` is the dict returned by `Tone3000Client.search_tones`
    — it carries a `data` list of Tone objects (see tone3000 API docs).
    """
    tones = search_result.get("data") if isinstance(search_result, dict) else None
    if not tones:
        return None

    for tone in tones:
        if not aggressive:
            license_str = (tone.get("license") or "").strip().lower()
            if license_str and license_str not in _PERMISSIVE_LICENSES:
                continue
            if (tone.get("downloads_count") or 0) < min_downloads:
                continue
        return tone

    return None
