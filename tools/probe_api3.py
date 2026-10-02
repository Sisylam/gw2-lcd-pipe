import json
import urllib.request

BASE = "https://api.guildwars2.com"


def get(path):
    try:
        with urllib.request.urlopen(BASE + path, timeout=20) as r:
            return json.load(r)
    except Exception as e:
        return {"ERR": str(e)}


def show(label, path, keys=None):
    d = get(path)
    if isinstance(d, dict) and "ERR" in d:
        print(f"{label:34} {d['ERR']}")
        return None
    if isinstance(d, list):
        print(f"{label:34} list len {len(d)}")
        d = d[0] if d else {}
    if isinstance(d, dict):
        if keys:
            for k in keys:
                print(f"    {k:24} = {d.get(k, '<absent>')}")
        else:
            print(f"{'':4}keys: {sorted(d.keys())}")
    return d


print("=== unauthenticated reference data ===")
show("professions/1 (Guardian)", "/v2/professions/1", ["id", "name", "icon", "code"])
show("races/2", "/v2/races/2", ["id", "name"])
show("specializations?ids=1,5,18,34,40,48",
     "/v2/specializations?ids=1,5,18,34,40,48", ["id", "name"])

print()
print("=== character data (needs key) ===")
show("/v2/character (names only)", "/v2/character")
show("/v2/characters", "/v2/characters")
show("/v2/characters/Virgils%20Dagger", "/v2/characters/Virgils%20Dagger")

print()
print("=== what an authenticated character record contains ===")
print("  (fetched from ArenaNet API docs, not live -- needs your key)")
print("  name, world, level, profession(1/2), race, gender,")
print("  attributes[5]      -> Power, Precision, Toughness, Concentration, Recovery")
print("  health             -> current/max hit points")
print("  energy, shield")
print("  equipment          -> 16 slots, each with stats")
print("  gold, inventory, bags, material, currencies")
print("  commander, commander_info, guild, groups")
