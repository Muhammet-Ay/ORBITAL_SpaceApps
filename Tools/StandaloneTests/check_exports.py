# Checks exported assembly JSON against reference_builds and against the rules that
# ReadOrbitalSpacecraft / ReadOrbitalLauncher (OrbitalCatalogLoader.cpp) enforce.
import json, math, sys
cat = json.load(open(sys.argv[1])); ex = json.load(open(sys.argv[2]))
ref = cat["reference_builds"]; fails = 0
def fail(m):
    global fails; fails += 1; print("  FAIL", m)
SC_SINGLE = ["bus", "engine", "antenna", "computer"]; SC_LIST = ["tanks", "adcs", "solar", "battery", "instruments"]
LV_SINGLE = ["s1_tank", "s1_engine", "s2_tank", "s2_engine", "fairing"]
def reader_ok(name, o):
    is_sc = "bus" in o
    keys = (SC_SINGLE + SC_LIST + ["orbit", "ground", "prop_kg", "years"]) if is_sc else (LV_SINGLE + ["s1_count"])
    ref_keys = list(ref["SC_PROSPECTOR" if is_sc else "LV_LIGHT"].keys())
    if list(o.keys()) != ref_keys: fail(f"{name}: key order/set {list(o.keys())} != {ref_keys}")
    for k in keys:
        if k not in o: fail(f"{name}: missing {k}")
    for k in (SC_SINGLE if is_sc else LV_SINGLE):
        v = o[k]
        if not (v is None or (isinstance(v, str) and v)): fail(f"{name}.{k}: must be null or non-empty string")
        if isinstance(v, str) and v not in {p["id"] for p in cat["parts"]}: fail(f"{name}.{k}: unknown id {v}")
    if is_sc:
        for k in SC_LIST:
            if not isinstance(o[k], list) or any(not isinstance(x, str) or not x for x in o[k]): fail(f"{name}.{k}: list of ids")
        for k in ("orbit", "ground"):
            if not isinstance(o[k], str) or not o[k]: fail(f"{name}.{k}: string required")
        for k in ("prop_kg", "years"):
            v = o[k]
            if isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v) or v < 0: fail(f"{name}.{k}: number >= 0")
    else:
        v = o["s1_count"]
        if not isinstance(v, int) or isinstance(v, bool) or v < 0: fail(f"{name}.s1_count: integer")
for name, o in ex.items():
    reader_ok(name, o)
    if name in ref:
        if o != ref[name]: fail(f"{name}: differs from reference_builds\n    got {o}\n    ref {ref[name]}")
        elif json.dumps(o) != json.dumps(ref[name]): fail(f"{name}: same values, different serialization")
        else: print(f"  {name}: identical to reference_builds")
print(f"{len(ex)} exported objects checked, {fails} failures"); sys.exit(1 if fails else 0)
