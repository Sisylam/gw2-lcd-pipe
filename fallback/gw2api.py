import json
import os
from pathlib import Path
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime, timedelta

ENV_PATH = Path(__file__).resolve().parent.parent / ".env"   # repo root
CACHE_DIR = Path(__file__).resolve().parent.parent / "cache"  # repo root


def _load_env():
    out = {}
    if ENV_PATH.exists():
        try:
            for line in ENV_PATH.read_text(encoding="utf-8").splitlines():
                line = line.strip()
                if not line or line.startswith("#") or "=" not in line:
                    continue
                k, _, v = line.partition("=")
                out[k.strip()] = v.strip().strip('"').strip("'")
        except Exception:
            pass
    return out


def _cache_path(key: str) -> Path:
    CACHE_DIR.mkdir(exist_ok=True)
    return CACHE_DIR / f"{key}.json"


def _cache_get(key: str, ttl_min: int = 15) -> dict | None:
    p = _cache_path(key)
    if not p.exists():
        return None
    try:
        with p.open("r", encoding="utf-8") as f:
            obj = json.load(f)
        ts = datetime.fromisoformat(obj.get("_ts", ""))
        if datetime.now() - ts > timedelta(minutes=ttl_min):
            return None
        data = obj.get("data", obj)
        return data if isinstance(data, dict) or isinstance(data, list) else None
    except Exception:
        return None


def _cache_set(key: str, data):
    p = _cache_path(key)
    try:
        with p.open("w", encoding="utf-8") as f:
            json.dump({"_ts": datetime.now().isoformat(), "data": data}, f)
    except Exception:
        pass


ENV = _load_env()
API_KEY = ENV.get("GW2_API_KEY")


def _request(path: str, authenticated: bool = False):
    url = "https://api.guildwars2.com" + path
    headers = {}
    if authenticated and API_KEY:
        headers["Authorization"] = "Bearer " + API_KEY
    req = urllib.request.Request(url, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=25) as r:
            return json.load(r)
    except urllib.error.HTTPError as e:
        try:
            body = e.read().decode("utf-8", errors="replace")
        except Exception:
            body = ""
        return {"ERR": f"HTTP {e.code} {e.reason}", "body": body}
    except Exception as e:
        return {"ERR": str(e)}


def account():
    if not API_KEY:
        return None
    key = "account"
    c = _cache_get(key, ttl_min=60)
    if c is not None:
        return c
    r = _request("/v2/account", authenticated=True)
    if isinstance(r, dict) and "ERR" in r:
        return None
    _cache_set(key, r)
    return r


def characters():
    if not API_KEY:
        return []
    key = "characters"
    c = _cache_get(key, ttl_min=10)
    if c is not None:
        return c if isinstance(c, list) else []
    r = _request("/v2/characters", authenticated=True)
    if isinstance(r, list):
        _cache_set(key, r)
        return r
    return []


def character(name: str):
    if not API_KEY or not name:
        return None
    key = f"character:{name}"
    c = _cache_get(key, ttl_min=5)
    if c is not None:
        return c
    q = urllib.parse.quote(name)
    r = _request(f"/v2/characters/{q}", authenticated=True)
    if isinstance(r, dict) and "ERR" in r:
        return None
    _cache_set(key, r)
    return r


def professions():
    key = "professions"
    c = _cache_get(key, ttl_min=720)
    if c is not None:
        return c if isinstance(c, list) else []
    r = _request("/v2/professions?ids=all")
    if isinstance(r, list):
        _cache_set(key, r)
        return r
    return []


def races():
    key = "races"
    c = _cache_get(key, ttl_min=720)
    if c is not None:
        return c if isinstance(c, list) else []
    r = _request("/v2/races?ids=all")
    if isinstance(r, list):
        _cache_set(key, r)
        return r
    return []


def worlds():
    key = "worlds"
    c = _cache_get(key, ttl_min=720)
    if c is not None:
        return c if isinstance(c, list) else []
    r = _request("/v2/worlds?ids=all")
    if isinstance(r, list):
        _cache_set(key, r)
        return r
    return []


def maps():
    key = "maps_all"
    c = _cache_get(key, ttl_min=720)
    if c is not None:
        return c if isinstance(c, list) else []
    r = _request("/v2/maps?ids=all")
    if isinstance(r, list):
        _cache_set(key, r)
        return r
    return []
