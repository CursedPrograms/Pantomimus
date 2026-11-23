# Bakes a MakeHuman rig's rest joints (head/tail per bone, in metres, Y up, facing +Z) from MakeHuman's base mesh:
#   python tools/bake_skeleton.py <makehuman data folder> game_engine data/skeletons/game_engine.skel
# Each joint is the mean of its vertex group on the base mesh (base.obj, decimetres), as MakeHuman places them.
import json, sys
data, rig, out = sys.argv[1], sys.argv[2], sys.argv[3]
V = []
for line in open(f"{data}/3dobjs/base.obj"):
    if line.startswith("v "):
        V.append([float(x) for x in line.split()[1:4]])
d = json.load(open(f"{data}/rigs/{rig}.mhskel"))
def joint(n):
    vs = d["joints"][n]
    return [sum(V[i][c] for i in vs) / len(vs) * 0.1 for c in range(3)]
order, seen = [], set()
def visit(n):
    if n in seen: return
    p = d["bones"][n]["parent"]
    if p: visit(p)
    seen.add(n); order.append(n)
for n in sorted(d["bones"]): visit(n)
foot = min(joint(d["bones"][n]["tail"])[1] for n in d["bones"])
with open(out, "w", newline="\n") as f:
    f.write(f"# {d['name']}: {d['description']}. Rest joints measured on MakeHuman's base mesh (CC0), in metres, Y up, the\n")
    f.write(f"# lowest joint at 0. The bones and joint groups come from MakeHuman's rig: {d['license']}, {d['copyright']}.\n")
    f.write("# name parent head.xyz tail.xyz\n")
    for n in order:
        b = d["bones"][n]; h = joint(b["head"]); t = joint(b["tail"])
        h[1] -= foot; t[1] -= foot
        f.write(f"{n} {b['parent'] or '-'} " + " ".join(f"{x:.5f}" for x in h + t) + "\n")
print(len(order), "bones")
