from __future__ import annotations
import time
from dataclasses import dataclass, field
from typing import Any, Optional

try:
    import mumble  # Windows only
except Exception:
    mumble = None

import gw2api


RACE_IDS = {0: "Asura", 1: "Charr", 2: "Human", 3: "Norn", 4: "Sylvari"}
PROF_IDS = {
    0: "Unknown",
    1: "Guardian",
    2: "Warrior",
    3: "Engineer",
    4: "Ranger",
    5: "Thief",
    6: "Elementalist",
    7: "Mesmer",
    8: "Necromancer",
    9: "Revenant",
}


@dataclass
class CharData:
    name: str = ""
    level: int = 0
    profession: str = ""
    race: str = ""
    gender: str = ""
    world: str = ""
    map_name: str = ""
    guild: str = ""
    deaths: int = 0
    combat: bool = False
    in_world: bool = False
    tick: int = 0
    ts: float = 0.0


@dataclass
class AppState:
    char: CharData = field(default_factory=CharData)
    has_mumble: bool = False
    has_api: bool = bool(gw2api.API_KEY)
    last_mumble: float = 0.0
    last_api: float = 0.0
    _world_cache: dict = field(default_factory=dict)
    _map_cache: dict = field(default_factory=dict)


_STATE = AppState()


def _build_caches():
    if not _STATE._world_cache:
        for w in gw2api.worlds():
            if isinstance(w, dict):
                _STATE._world_cache[w.get("id")] = w.get("name", "Unknown")
    if not _STATE._map_cache:
        for m in gw2api.maps():
            if isinstance(m, dict):
                _STATE._map_cache[m.get("id")] = m.get("name", "Unknown")


def _resolve_world(i):
    _build_caches()
    return _STATE._world_cache.get(i) or str(i)


def _resolve_map(i):
    _build_caches()
    return _STATE._map_cache.get(i) or str(i)


def refresh_api():
    if not _STATE.has_api:
        return
    _build_caches()
    try:
        chs = gw2api.characters()
        if not chs:
            return
        # MumbleLink names the logged-in character; otherwise fall back to the
        # first character on the account.
        c = None
        for nm in chs:
            if nm == _STATE.char.name:
                c = gw2api.character(nm)
                break
        if c is None:
            c = gw2api.character(chs[0])
        if c:
            cd = _STATE.char
            cd.name = c.get("name") or cd.name
            cd.level = c.get("level", cd.level)
            cd.gender = c.get("gender", "") or ""
            cd.guild = c.get("guild") or ""
            cd.deaths = c.get("deaths", cd.deaths)
            # The character endpoint already expands these to name strings.
            if c.get("race"):
                cd.race = c["race"]
            if c.get("profession"):
                cd.profession = c["profession"]
        acct = gw2api.account()
        if acct and acct.get("world") is not None:
            _STATE.char.world = _resolve_world(acct["world"])
        _STATE.last_api = time.time()
    except Exception:
        pass


def refresh_mumble():
    if mumble is None:
        _STATE.has_mumble = False
        return
    try:
        md = mumble.read()
        _STATE.has_mumble = md.valid
        if not md.valid:
            return
        cd = _STATE.char
        cd.in_world = mumble.is_in_world(md)
        cd.tick = md.ui_tick
        cd.combat = bool(md.ui_state & 64)
        ij = md.identity_json or {}
        if ij.get("name"):
            cd.name = ij["name"]
        if ij.get("profession") and not cd.profession:
            cd.profession = PROF_IDS.get(ij["profession"], "Unknown")
        if ij.get("race") and not cd.race:
            cd.race = RACE_IDS.get(ij["race"], "Unknown")
        if md.map_id:
            cd.map_name = _resolve_map(md.map_id)
        _STATE.last_mumble = time.time()
        _STATE.char.ts = time.time()
    except Exception:
        _STATE.has_mumble = False


def refresh():
    refresh_mumble()
    if time.time() - _STATE.last_api > 300:
        refresh_api()


def get_state() -> AppState:
    return _STATE
