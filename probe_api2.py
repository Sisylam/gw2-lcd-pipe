import json
import urllib.request

BASE = "https://api.guildwars2.com"


def get(path):
    try:
        with urllib.request.urlopen(BASE + path, timeout=20) as r:
            return json.load(r)
    except Exception as e:
        return {"ERR": str(e)}


def show(label, path):
    d = get(path)
    if isinstance(d, dict) and "ERR" in d:
        print(label, d["ERR"])
        return None
    if isinstance(d, list):
        print(label, "list len", len(d), "first:", str(d[0])[:180] if d else "-")
    else:
        print(label, "dict keys:", sorted(d.keys()))
    return d


show("poi (no ids)", "/v2/maps/15/poi")
show("vista (no ids)", "/v2/maps/15/vista")
show("poi?ids=all", "/v2/maps/15/poi?ids=all")
show("vista?ids=all", "/v2/maps/15/vista?ids=all")
show("continents", "/v2/continents")
show("continents/1", "/v2/continents/1")
show("character", "/v2/characters")
show("account", "/v2/account")
