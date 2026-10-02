import json
import os
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

ENV_PATH = Path(__file__).resolve().parent.parent / ".env"   # repo root


def load_env(path=ENV_PATH):
    """Minimal .env reader. Returns dict. Ignores comments and blank lines."""
    if not path.exists():
        return {}
    out = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, _, val = line.partition("=")
        out[key.strip()] = val.strip().strip('"').strip("'")
    return out


def api_get(path, key):
    req = urllib.request.Request(
        "https://api.guildwars2.com" + path,
        headers={"Authorization": "Bearer " + key} if key else {},
    )
    try:
        with urllib.request.urlopen(req, timeout=25) as r:
            return json.load(r)
    except urllib.error.HTTPError as e:
        return {"ERR": f"HTTP {e.code} {e.reason}"}
    except Exception as e:
        return {"ERR": str(e)}


env = load_env()
key = env.get("GW2_API_KEY")
has_key = bool(key)

# Never log the key itself - only whether one is present.
print(f".env found      : {ENV_PATH.exists()}")
print(f".env path       : {ENV_PATH}")
print(f"GW2_API_KEY     : {'set' if has_key else 'not set'}")
print()

if not key:
    print("No key found. Create .env in this folder with:")
    print("  GW2_API_KEY=your-key-here")
    raise SystemExit(1)

# 1. confirm the key authenticates at all
acct = api_get("/v2/account", key)
if isinstance(acct, dict) and "ERR" in acct:
    print(f"authentication  : FAILED -> {acct['ERR']}")
    print()
    print("Common causes:")
    print("  - key is incomplete / has trailing whitespace in the quotes")
    print("  - key was revoked or regenerated on the account page")
    print("  - key lacks the 'characters' scope")
    raise SystemExit(1)

print(f"authentication  : OK")
print(f"account id      : {acct.get('id')}")
print(f"account name    : {acct.get('name')}")
print()

# 2. list characters on the key
chars = api_get("/v2/characters", key)
if isinstance(chars, dict) and "ERR" in chars:
    print(f"characters      : FAILED -> {chars['ERR']}")
    raise SystemExit(1)

print(f"characters raw type: {type(chars).__name__}")
print(f"characters raw (first 300): {str(chars)[:300]}")
print()

if isinstance(chars, dict):
    names = chars.get("characters", [])
    char_id = chars.get("id")
else:
    # /v2/characters returns a flat list of character names
    names = [c for c in chars if isinstance(c, str)]
    char_id = None

print(f"characters ({len(names)}) : {', '.join(n for n in names if n)}")
print(f"character id     : {char_id}")
print()

# 3. full record for the first character, to confirm attribute data
target = "Virgils Dagger" if "Virgils Dagger" in names else next(n for n in names if n)
print(f"=== full record: {target} ===")
rec = api_get(f"/v2/characters/{urllib.parse.quote(target)}", key)
if isinstance(rec, dict) and "ERR" in rec:
    print(f"  FAILED -> {rec['ERR']}")
    raise SystemExit(1)

print(f"  all keys returned: {sorted(rec.keys())}")
print()
for k in sorted(rec.keys()):
    v = rec[k]
    s = str(v)
    print(f"  {k:16} = {s[:120]}")
print()

# Now request the extended data explicitly
print("=== with ?stats supported sets ===")
for variant in (
    "/v2/characters/{n}?stats=attributes,health,energy,shield".format(n=urllib.parse.quote(target)),
    "/v2/characters/{n}?stats=all".format(n=urllib.parse.quote(target)),
    "/v2/characters/{n}?language=en".format(n=urllib.parse.quote(target)),
):
    r2 = api_get(variant, key)
    if isinstance(r2, dict) and "ERR" in r2:
        print(f"  {variant.split('?')[1]:40} -> {r2['ERR']}")
    else:
        print(f"  {variant.split('?')[1]:40} -> keys: {sorted(r2.keys())}")
