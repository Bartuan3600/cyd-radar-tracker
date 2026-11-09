#!/usr/bin/env python3
"""Prueft die vom Host-Test geschriebenen JSON-Schnappschuesse mit einem echten JSON-Parser und
die Szenario-Erwartungen (laufend -> stehend/gehalten -> weiter -> weg). Aufruf: validate_json.py <dir>"""
import json, sys, glob, math
d = sys.argv[1] if len(sys.argv) > 1 else "/tmp/cyd_json"
files = sorted(glob.glob(d + "/state_*.json"))
assert len(files) == 80, len(files)
S = {int(f[-10:-5]): json.load(open(f)) for f in files}
ok = 0
def chk(c, msg):
    global ok
    if not c: print("FAIL", msg); sys.exit(1)
    ok += 1
for t, j in S.items():
    for k in ("t", "sim", "n", "nc", "ns", "fd", "s", "r450", "r410", "tr", "or", "ol", "pf", "cf"): chk(k in j, f"{t}: Key {k}")
    chk(len(j["s"]) == 2 and j["ns"] == 2 and "r420" not in j and len(j["r450"]) == 3, f"{t}: Laengen")
    chk(j["sim"] == 1 and j["n"] == len(j["tr"]) and j["nc"] == sum(x["c"] for x in j["tr"]), f"{t}: Summen")
    chk(all(s["st"] in (0, 1, 2) for s in j["s"]), f"{t}: Linkstatus")
chk(all(S[3000]["s"][i]["st"] == 1 for i in range(2)), "3 s: beide Sensoren gruen")
a = S[3000]["tr"]; chk(len(a) == 1 and a[0]["s"] == 0 and a[0]["c"] == 1, "3 s: 1 Ziel bewegt, bestaetigt")
h = [t for t in S[11000]["tr"] if t["id"] == a[0]["id"]]
chk(len(h) == 1 and h[0]["s"] == 2 and h[0]["c"] == 0 and h[0]["by"] == [0, 1, 0], "11 s: gehalten durch LD2410C, nicht bestaetigt (nur 1 Sensor)")
chk(abs(h[0]["x"] - 600) < 120 and abs(h[0]["y"] - 2500) < 120, "11 s: Position gehalten")
chk(S[11000]["r450"][0]["v"] == 0, "11 s: LD2450 sieht niemanden")
chk(all(j["fd"] == 0 for j in S.values()), "Simulation: Frontfilter verwirft nichts")
chk(all(t["f"] == 1 for t in S[3000]["r450"] if t["v"]), "3 s: Rohziele im Frontbereich")
chk(len(S[16000]["tr"]) == 2, "16 s: 2 Ziele")
chk(any(t["id"] == a[0]["id"] and t["s"] == 0 for t in S[24500]["tr"]), "24,5 s: gleiche ID wieder bewegt")
chk(S[36000]["n"] == 0, "36 s: keine Ziele")
print(f"JSON-Validierung: {len(files)} Schnappschuesse, {ok} Pruefungen OK")
