import json
import urllib.request

BASE = "https://api.guildwars2.com"


def get(path):
    try:
        with urllib.request.urlopen(BASE + path, timeout=20) as r:
            return json.load(r)
    except Exception as e:
        return {"ERR": str(e)}


reg = get("/v2/continents/1/floors/1/regions?ids=all")
if isinstance(reg, list) and reg and isinstance(reg[0], dict):
    print("regions count:", len(reg))
    print("  keys:", sorted(reg[0].keys()))
    print("  sample:", {k: reg[0][k] for k in sorted(reg[0])[:6]})
else:
    print("regions", str(reg)[:300])

m = get("/v2/maps/15")
if "ERR" in m:
    print("maps/15", m)
else:
    print("maps/15 keys:", sorted(m.keys()))
    for k in ("points", "vista"):
        v = m.get(k)
        print("  ", k, type(v).__name__, len(v) if hasattr(v, "__len__") else v)

w = get("/v2/worlds")
print("worlds:", str(w)[:200])

ch = get("/v2/character")
print("character (no auth):", str(ch)[:200])
