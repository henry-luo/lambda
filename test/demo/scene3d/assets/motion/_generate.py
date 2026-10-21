"""Regenerate the CC0 motion fixtures with Python's standard library.

One source description -- a small CMU-named humanoid and a one-second jump --
is written as BVH (and, from milestone M2 on, as FBX), and its world joint
positions are computed here by an independent forward-kinematics pass, so the
converters can be checked against ground truth and against each other
(vibe/Lambda_Pkg_Motion.md §7.4, §11).
"""
import json, math, pathlib, struct, zlib

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


# ---- FBX ------------------------------------------------------------------
# One record tree is written three ways: binary 7.4 with raw arrays, binary 7.5
# (64-bit record headers) with zlib arrays, and ASCII 7.4. A record is
# (name, [properties], [children]); a property is (type code, value), where
# "N" marks an object name, spelled `Class::Name` in ASCII and
# `Name\x00\x01Class` in binary.

KTIME = 46186158000  # ticks per second
FBX_ROTATION_ORDER = {"XYZ": 0, "XZY": 1, "YZX": 2, "YXZ": 3, "ZXY": 4, "ZYX": 5}


def f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def fbx_order(bvh_order):
    # BVH lists rotations outermost first; FBX names them in application order
    return FBX_ROTATION_ORDER[bvh_order[::-1]]


def fbx_scene(version):
    ids = iter(range(1000001, 2000000))
    objects, connections = [], []
    stop = (FRAMES - 1) * KTIME // int(FPS)
    times = [frame * KTIME // int(FPS) for frame in range(FRAMES)]
    poses = [pose(frame) for frame in range(FRAMES)]

    def prop(name, kind, label, flags, *values):
        return ("P", [("S", name), ("S", kind), ("S", label), ("S", flags)] + list(values), [])

    def ktime(name, value):
        return prop(name, "KTime", "Time", "", ("L", value))

    stack, layer = next(ids), next(ids)
    objects.append(("AnimationStack", [("L", stack), ("N", "AnimStack::jump"), ("S", "")], [
        ("Properties70", [], [ktime("LocalStart", 0), ktime("LocalStop", stop),
                              ktime("ReferenceStart", 0), ktime("ReferenceStop", stop)])]))
    objects.append(("AnimationLayer", [("L", layer), ("N", "AnimLayer::BaseLayer"), ("S", "")], []))
    connections.append(("C", [("S", "OO"), ("L", layer), ("L", stack)], []))

    def curve_node(model, kind, label, values):
        node = next(ids)
        defaults = [prop("d|" + axis, "Number", "", "A", ("D", values[0][i])) for i, axis in enumerate("XYZ")]
        objects.append(("AnimationCurveNode", [("L", node), ("N", "AnimCurveNode::" + kind), ("S", "")],
                        [("Properties70", [], defaults)]))
        connections.append(("C", [("S", "OO"), ("L", node), ("L", layer)], []))
        connections.append(("C", [("S", "OP"), ("L", node), ("L", model), ("S", label)], []))
        for i, axis in enumerate("XYZ"):
            curve = next(ids)
            keys = [f32(v[i]) for v in values]
            objects.append(("AnimationCurve", [("L", curve), ("N", "AnimCurve::"), ("S", "")], [
                ("Default", [("D", values[0][i])], []),
                ("KeyVer", [("I", 4009)], []),
                ("KeyTime", [("l", times)], []),
                ("KeyValueFloat", [("f", keys)], []),
                ("KeyAttrFlags", [("i", [4])], []),  # linear interpolation
                ("KeyAttrDataFloat", [("f", [0.0, 0.0, 0.0, 0.0])], []),
                ("KeyAttrRefCount", [("i", [FRAMES])], [])]))
            connections.append(("C", [("S", "OP"), ("L", curve), ("L", node), ("S", "d|" + axis)], []))

    models = {}
    for name, parent, offset, order, _tip in SKELETON:
        model, attribute = next(ids), next(ids)
        models[name] = model
        objects.append(("NodeAttribute", [("L", attribute), ("N", "NodeAttribute::" + name), ("S", "LimbNode")],
                        [("TypeFlags", [("S", "Skeleton")], [])]))
        objects.append(("Model", [("L", model), ("N", "Model::" + name), ("S", "LimbNode")], [
            ("Version", [("I", 232)], []),
            ("Properties70", [], [
                prop("RotationOrder", "enum", "", "", ("I", fbx_order(order))),
                prop("RotationActive", "bool", "", "", ("I", 1)),
                prop("InheritType", "enum", "", "", ("I", 1)),
                prop("Lcl Translation", "Lcl Translation", "", "A", *[("D", v) for v in offset]),
                prop("Lcl Rotation", "Lcl Rotation", "", "A", ("D", 0.0), ("D", 0.0), ("D", 0.0)),
                prop("Lcl Scaling", "Lcl Scaling", "", "A", ("D", 1.0), ("D", 1.0), ("D", 1.0))]),
            ("Culling", [("S", "CullingOff")], [])]))
        connections.append(("C", [("S", "OO"), ("L", attribute), ("L", model)], []))
        connections.append(("C", [("S", "OO"), ("L", model), ("L", models[parent] if parent else 0)], []))
        if parent is None:
            curve_node(model, "T", "Lcl Translation", [p[name][0] for p in poses])
        curve_node(model, "R", "Lcl Rotation", [[p[name][1].get(axis, 0.0) for axis in "XYZ"] for p in poses])

    axis = lambda name, value: prop(name, "int", "Integer", "", ("I", value))
    return [
        ("FBXHeaderExtension", [], [
            ("FBXHeaderVersion", [("I", 1003)], []),
            ("FBXVersion", [("I", version)], []),
            ("Creator", [("S", "lambda motion fixture generator (CC0)")], [])]),
        ("GlobalSettings", [], [
            ("Version", [("I", 1000)], []),
            ("Properties70", [], [
                axis("UpAxis", 1), axis("UpAxisSign", 1), axis("FrontAxis", 2), axis("FrontAxisSign", 1),
                axis("CoordAxis", 0), axis("CoordAxisSign", 1),
                prop("UnitScaleFactor", "double", "Number", "", ("D", 1.0)),
                prop("TimeMode", "enum", "", "", ("I", 6)),  # 30 frames per second
                ktime("TimeSpanStart", 0), ktime("TimeSpanStop", stop),
                prop("CustomFrameRate", "double", "Number", "", ("D", -1.0))])]),
        ("References", [], []),
        ("Objects", [], objects),
        ("Connections", [], connections),
        ("Takes", [], [
            ("Current", [("S", "jump")], []),
            ("Take", [("S", "jump")], [
                ("FileName", [("S", "jump.tak")], []),
                ("LocalTime", [("L", 0), ("L", stop)], []),
                ("ReferenceTime", [("L", 0), ("L", stop)], [])])]),
    ]


ARRAY_FORMAT = {"f": "f", "d": "d", "i": "i", "l": "q"}


def fbx_property(code, value, compress):
    if code in "ILDF":
        return code.encode() + struct.pack("<" + {"I": "i", "L": "q", "D": "d", "F": "f"}[code], value)
    if code in "SN":
        if code == "N":
            kind, _, name = value.partition("::")
            data = name.encode() + b"\x00\x01" + kind.encode()
        else:
            data = value.encode()
        return b"S" + struct.pack("<I", len(data)) + data
    data = struct.pack("<%d%s" % (len(value), ARRAY_FORMAT[code]), *value)
    stored = zlib.compress(data) if compress else data
    return code.encode() + struct.pack("<III", len(value), 1 if compress else 0, len(stored)) + stored


def fbx_record(out, record, wide, compress):
    name, properties, children = record
    header = 25 if wide else 13
    start = len(out)
    out += bytes(header) + name.encode()
    body = len(out)
    for code, value in properties:
        out += fbx_property(code, value, compress)
    length = len(out) - body
    for child in children:
        fbx_record(out, child, wide, compress)
    if children or not properties:  # nested lists and empty records end with a null record
        out += bytes(header)
    fields = (len(out), len(properties), length, len(name))
    out[start:start + header] = struct.pack("<QQQB" if wide else "<IIIB", *fields)


def write_fbx_binary(path, version, compress):
    wide = version >= 7500
    out = bytearray(b"Kaydara FBX Binary  \x00\x1a\x00" + struct.pack("<I", version))
    for record in fbx_scene(version):
        fbx_record(out, record, wide, compress)
    out += bytes(25 if wide else 13)
    # Blender's footer: an id, padding to 16 bytes, the version, then a magic
    out += b"\xfa\xbc\xab\x09\xd0\xc8\xd4\x66\xb1\x76\xfb\x83\x1c\xf7\x26\x7e" + bytes(4)
    out += bytes((16 - len(out) % 16) % 16) + struct.pack("<I", version) + bytes(120)
    out += b"\xf8\x5a\x8c\x6a\xde\xf5\xd9\x7e\xec\xe9\x0c\xe3\x75\x8f\x29\x0b"
    path.write_bytes(bytes(out))


def fbx_text(code, value):
    if code in "IL": return "%d" % value
    if code == "D": return repr(float(value))
    if code in "SN": return '"%s"' % value
    raise ValueError(code)


def write_fbx_ascii(path, version):
    lines = ["; FBX %d.%d.0 project file" % (version // 1000, version // 100 % 10),
             "; Generated by test/demo/scene3d/assets/motion/_generate.py (CC0)",
             "; ----------------------------------------------------", ""]

    def emit(record, depth):
        name, properties, children = record
        pad = "\t" * depth
        if len(properties) == 1 and properties[0][0] in ARRAY_FORMAT:
            code, values = properties[0]
            text = [repr(v) if code in "fd" else "%d" % v for v in values]
            rows = [",".join(text[i:i + 12]) for i in range(0, len(text), 12)]
            lines.append("%s%s: *%d {" % (pad, name, len(values)))
            lines.append("%s\ta: %s" % (pad, (",\n" + pad + "\t").join(rows)))
            lines.append(pad + "} ")
            return
        head = "%s%s: %s" % (pad, name, ", ".join(fbx_text(c, v) for c, v in properties))
        if not children and properties:
            lines.append(head)
            return
        lines.append(head.rstrip() + " {")
        for child in children:
            emit(child, depth + 1)
        lines.append(pad + "}")

    for record in fbx_scene(version):
        emit(record, 0)
    path.write_text("\n".join(lines) + "\n")


write_bvh(root / "jump.bvh")
write_fbx_binary(root / "jump.fbx", 7400, compress=False)
write_fbx_binary(root / "jump-7500.fbx", 7500, compress=True)
write_fbx_ascii(root / "jump-ascii.fbx", 7400)
expected = {"frames": FRAMES, "fps": FPS, "units": "cm",
            "positions": {str(f): world_positions(f) for f in (0, 8, 15, 30)}}
(root / "expected.json").write_text(json.dumps(expected, indent=1, sort_keys=True) + "\n")
