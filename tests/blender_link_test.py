# Tests blender/pantomimus_link.py without Pantomimus: a fake live link sends a three-bone arm (Pantomimus' format,
# Y up) with the upper bone turned 90 degrees about Z, and the add-on must pose an armature to match.
#   blender -b --factory-startup --python tests/blender_link_test.py
import json
import math
import os
import socket
import sys
import threading
import time

import bpy

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "blender"))
import pantomimus_link as pl  # noqa: E402

PORT = 47899
SKEL = {"type": "skeleton", "name": "Arm", "bones": [
    {"name": "root", "parent": -1, "head": [0, 1.0, 0], "tail": [0, 1.1, 0]},
    {"name": "upper", "parent": 0, "head": [0, 1.1, 0], "tail": [0, 1.6, 0]},
    {"name": "lower", "parent": 1, "head": [0, 1.6, 0], "tail": [0, 2.0, 0]},
]}
c, s = math.cos(math.pi / 4), math.sin(math.pi / 4)      # 90 degrees about Z (Y up): w, x, y, z
POSE = {"type": "pose", "time": 0.0, "contact": [1, 1], "bones": [
    [1, 0, 0, 0, 0, 1.0, 0],
    [c, 0, 0, s, 0, 1.1, 0],
    [c, 0, 0, s, -0.5, 1.1, 0],
]}


def serve():
    srv = socket.socket()
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", PORT))
    srv.listen(1)
    conn, _ = srv.accept()
    conn.sendall((json.dumps(SKEL) + "\n").encode())
    time.sleep(0.05)
    conn.sendall((json.dumps(POSE) + "\n").encode())
    time.sleep(0.5)
    conn.close()
    srv.close()


fails = 0


def check(what, ok, detail=""):
    global fails
    print(("ok    " if ok else "FAIL  ") + what + (": " + detail if detail else ""))
    fails += 0 if ok else 1


pl.register()
threading.Thread(target=serve, daemon=True).start()
time.sleep(0.2)
check("connects", pl._Stream.connect("127.0.0.1", PORT), pl._Stream.status)
deadline = time.time() + 3
while (pl._Stream.skeleton is None or pl._Stream.pose is None) and time.time() < deadline:
    pl._Stream.read()
    time.sleep(0.02)
check("reads the skeleton and a pose", pl._Stream.skeleton is not None and pl._Stream.pose is not None)

ctx = bpy.context
ctx.scene.cursor.location = (0, 0, 0)
arm = pl.build_armature(ctx, pl._Stream.skeleton, 1.0)
check("builds the armature", len(arm.data.bones) == 3, f"{len(arm.data.bones)} bones")
b = arm.data.bones["upper"]
check("Y up becomes Z up", (b.head_local - pl.Vector((0, 0, 1.1))).length < 1e-5, str(tuple(b.head_local)))

pl.apply_pose(arm, pl._Stream.skeleton, pl._Stream.pose, 1.0, True)
ctx.view_layer.update()
lower = arm.pose.bones["lower"]
want = pl.Vector((-0.5, 0, 1.1))         # (0, 1.6, 0) turned 90 degrees about Z at (0, 1.1, 0), then Z up
check("the turned bone carries its child", (lower.head - want).length < 1e-4, str(tuple(round(v, 4) for v in lower.head)))
tail_want = pl.Vector((-0.9, 0, 1.1))
check("the child points along the turn", (lower.tail - tail_want).length < 1e-4, str(tuple(round(v, 4) for v in lower.tail)))

pl.key_pose(arm, 1)
check("records keys", arm.animation_data is not None and arm.animation_data.action is not None)
pl._Stream.close()
pl.unregister()
print("All passed" if not fails else f"{fails} failed")
sys.exit(1 if fails else 0)
