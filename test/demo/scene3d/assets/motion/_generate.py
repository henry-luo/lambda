"""Regenerate the CC0 motion fixtures with Python's standard library.

One source description -- a small CMU-named humanoid and a one-second jump --
is written as BVH (and, from milestone M2 on, as FBX), and its world joint
positions are computed here by an independent forward-kinematics pass, so the
converters can be checked against ground truth and against each other
(vibe/Lambda_Pkg_Motion.md §7.4, §11).
"""
import json, math, pathlib

root = pathlib.Path(__file__).parent

# (name, parent, offset in centimetres, rotation channel order, tip or None);
# the root adds three position channels in front of its rotations
SKELETON = [
    ("Hips", None, (0.0, 0.0, 0.0), "ZXY", None),
    ("LeftUpLeg", "Hips", (9.0, -8.0, 0.0), "ZXY", None),
    ("LeftLeg", "LeftUpLeg", (0.0, -42.0, 0.0), "ZXY", None),
    ("LeftFoot", "LeftLeg", (0.0, -41.0, 0.0), "ZXY", None),
    ("LeftToeBase", "LeftFoot", (0.0, -5.0, 12.0), "ZXY", (0.0, 0.0, 6.0)),
    ("RightUpLeg", "Hips", (-9.0, -8.0, 0.0), "ZXY", None),
    ("RightLeg", "RightUpLeg", (0.0, -42.0, 0.0), "ZXY", None),
    ("RightFoot", "RightLeg", (0.0, -41.0, 0.0), "ZXY", None),
    ("RightToeBase", "RightFoot", (0.0, -5.0, 12.0), "ZXY", (0.0, 0.0, 6.0)),
    ("LowerBack", "Hips", (0.0, 10.0, 0.0), "ZXY", None),
    ("Spine", "LowerBack", (0.0, 10.0, 0.0), "ZXY", None),
    ("Spine1", "Spine", (0.0, 12.0, 0.0), "ZXY", None),
    ("Neck", "Spine1", (0.0, 12.0, 0.0), "ZXY", None),
    ("Head", "Neck", (0.0, 10.0, 0.0), "ZXY", (0.0, 15.0, 0.0)),
    ("LeftShoulder", "Spine1", (4.0, 9.0, 0.0), "ZXY", None),
    ("LeftArm", "LeftShoulder", (12.0, 0.0, 0.0), "ZXY", None),
    ("LeftForeArm", "LeftArm", (27.0, 0.0, 0.0), "ZXY", None),
    ("LeftHand", "LeftForeArm", (24.0, 0.0, 0.0), "ZXY", (12.0, 0.0, 0.0)),
    ("RightShoulder", "Spine1", (-4.0, 9.0, 0.0), "ZXY", None),
    ("RightArm", "RightShoulder", (-12.0, 0.0, 0.0), "ZXY", None),
    ("RightForeArm", "RightArm", (-27.0, 0.0, 0.0), "ZXY", None),
    ("RightHand", "RightForeArm", (-24.0, 0.0, 0.0), "ZXY", (-12.0, 0.0, 0.0)),
]
FRAMES, FPS = 31, 30.0
HIP_HEIGHT = 93.0


def smooth(a, b, t):
    x = min(1.0, max(0.0, (t - a) / (b - a)))
    return x * x * (3 - 2 * x)


def pose(frame):
    """Per joint: (root position or None, {axis: degrees})."""
    t = frame / (FRAMES - 1)
    crouch = smooth(0.0, 0.25, t) * (1 - smooth(0.25, 0.4, t)) + smooth(0.75, 0.9, t) * (1 - smooth(0.9, 1.0, t))
    lift = 30.0 * math.sin(math.pi * min(1.0, max(0.0, (t - 0.3) / 0.45)))
    arms = smooth(0.2, 0.5, t) * (1 - smooth(0.7, 1.0, t))
    out = {name: (None, {}) for name, *_ in SKELETON}
    out["Hips"] = ((0.0, HIP_HEIGHT - 15.0 * crouch + lift, 20.0 * t), {"X": 8.0 * crouch})
    for side in ("Left", "Right"):
        out[side + "UpLeg"] = (None, {"X": -40.0 * crouch})
        out[side + "Leg"] = (None, {"X": 70.0 * crouch})
        out[side + "Foot"] = (None, {"X": -25.0 * crouch})
    out["Spine1"] = (None, {"X": -6.0 * crouch})
    out["Head"] = (None, {"Y": 10.0 * math.sin(2 * math.pi * t)})
    out["LeftArm"] = (None, {"Z": -70.0 + 140.0 * arms})
    out["RightArm"] = (None, {"Z": 70.0 - 140.0 * arms})
    out["LeftForeArm"] = (None, {"Y": 30.0 * arms})
    out["RightForeArm"] = (None, {"Y": -30.0 * arms})
    return out


# ---- the independent forward-kinematics reference ---------------------------

def axis_matrix(axis, degrees):
    c, s = math.cos(math.radians(degrees)), math.sin(math.radians(degrees))
    if axis == "X": return [[1, 0, 0], [0, c, -s], [0, s, c]]
    if axis == "Y": return [[c, 0, s], [0, 1, 0], [-s, 0, c]]
    return [[c, -s, 0], [s, c, 0], [0, 0, 1]]


def mat_mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]


def world_positions(frame):
    p = pose(frame)
    world = {}
    for name, parent, offset, order, _tip in SKELETON:
        position, angles = p[name]
        local_t = position if position is not None else offset
        rotation = [[1, 0, 0], [0, 1, 0], [0, 0, 1]]
        for axis in order:  # the first listed rotation is outermost
            rotation = mat_mul(rotation, axis_matrix(axis, angles.get(axis, 0.0)))
        if parent is None:
            world[name] = (rotation, list(local_t))
        else:
            pr, pt = world[parent]
            t = [pt[i] + sum(pr[i][k] * local_t[k] for k in range(3)) for i in range(3)]
            world[name] = (mat_mul(pr, rotation), t)
    return {name: [round(v, 9) for v in world[name][1]] for name in world}


# ---- BVH ------------------------------------------------------------------

def write_bvh(path):
    lines, columns = ["HIERARCHY"], []
    children = {name: [n for n, parent, *_ in SKELETON if parent == name] for name, *_ in SKELETON}
    joints = {name: (offset, order, tip) for name, _parent, offset, order, tip in SKELETON}

    def emit(name, depth):
        pad = "  " * depth
        offset, order, tip = joints[name]
        lines.append(pad + ("ROOT " if depth == 0 else "JOINT ") + name)
        lines.append(pad + "{")
        lines.append(pad + "  OFFSET %.4f %.4f %.4f" % offset)
        channels = ["%srotation" % axis for axis in order]
        if depth == 0: channels = ["Xposition", "Yposition", "Zposition"] + channels
        lines.append(pad + "  CHANNELS %d %s" % (len(channels), " ".join(channels)))
        columns.append((name, channels))
        for child in children[name]: emit(child, depth + 1)
        if tip is not None:
            lines.extend([pad + "  End Site", pad + "  {", pad + "    OFFSET %.4f %.4f %.4f" % tip, pad + "  }"])
        lines.append(pad + "}")

    emit("Hips", 0)
    lines += ["MOTION", "Frames: %d" % FRAMES, "Frame Time: %.8f" % (1.0 / FPS)]
    for frame in range(FRAMES):
        p, row = pose(frame), []
        for name, channels in columns:
            position, angles = p[name]
            for channel in channels:
                row.append(position["XYZ".index(channel[0])] if channel.endswith("position") else angles.get(channel[0], 0.0))
        lines.append(" ".join("%.6f" % v for v in row))
    path.write_text("\n".join(lines) + "\n")


write_bvh(root / "jump.bvh")
expected = {"frames": FRAMES, "fps": FPS, "units": "cm",
            "positions": {str(f): world_positions(f) for f in (0, 8, 15, 30)}}
(root / "expected.json").write_text(json.dumps(expected, indent=1, sort_keys=True) + "\n")
