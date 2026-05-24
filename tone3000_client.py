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

import json
import os
import sqlite3
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from typing import Any

BASE_URL = "https://www.tone3000.com/api/v1"
WEB_BASE_URL = "https://www.tone3000.com"

# Cache TTL: the tone catalog grows daily, but for our purposes a week
# is plenty — captures don't change once uploaded, only new ones appear.
_CACHE_TTL_SECONDS = 7 * 24 * 3600


class Tone3000Client:
    """Minimal client for tone3000's read-only tone search & model APIs.

    `cache_db_path` is created if missing; it stores serialized search
    responses keyed by the request URL so that repeating the same
    library-wide batch hits the network at most once per unique gear.
    """

    def __init__(self, cache_db_path: str, api_key: str | None = None, timeout: float = 15.0):
        self.api_key = api_key or os.environ.get("SLOPSMITH_TONE3000_KEY")
        self.timeout = timeout
        # A 401 from the search endpoint flips this flag. The UI uses
        # it to switch from auto-mode to deep-link mode rather than
        # making the user paste/edit a config file.
        self.has_api_access = bool(self.api_key)
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

    def _http_get(self, url: str) -> Any:
        req = urllib.request.Request(url)
        req.add_header("Accept", "application/json")
        req.add_header("User-Agent", "slopsmith-tone-bridge/0.0.1")
        if self.api_key:
            req.add_header("Authorization", f"Bearer {self.api_key}")
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as resp:
                return json.loads(resp.read().decode("utf-8"))
        except urllib.error.HTTPError as e:
            if e.code == 401:
                self.has_api_access = False
                # A 401 isn't a programming error — it just means the
                # user hasn't supplied a key. The UI handles the
                # fallback flow; callers see an empty payload.
                return None
            raise

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
        """List downloadable .nam models attached to a tone."""
        return self._cached_get(f"{BASE_URL}/models?tone_id={tone_id}")

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
        req = urllib.request.Request(model_url)
        req.add_header("User-Agent", "slopsmith-tone-bridge/0.0.1")
        if self.api_key:
            req.add_header("Authorization", f"Bearer {self.api_key}")
        # Stream to a temp path first; only rename into the final
        # name on success so a partial download doesn't leave a
        # corrupted file the dedup check would later accept.
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
        except Exception:
            try:
                os.remove(tmp_path)
            except FileNotFoundError:
                pass
            raise
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
_SIZE_PREFERENCE = ("standard", "lite", "feather", "nano", "custom")


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
