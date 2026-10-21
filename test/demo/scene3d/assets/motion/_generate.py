"""Regenerate the CC0 motion fixtures with Python's standard library.

One source description -- a small CMU-named humanoid and a one-second jump --
is written as BVH (and, from milestone M2 on, as FBX), and its world joint
positions are computed here by an independent forward-kinematics pass, so the
converters can be checked against ground truth and against each other
(vibe/Lambda_Pkg_Motion.md §7.4, §11).
"""
import base64, json, math, pathlib, struct, zlib

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
# Each scene is one record tree written as binary (raw or zlib arrays, 32- or
# 64-bit record headers) and as ASCII. A record is (name, [properties],
# [children]); a property is (type code, value), where "N" marks an object
# name, spelled `Class::Name` in ASCII and `Name\x00\x01Class` in binary, and
# "R" raw bytes, which ASCII spells as base64 after a leading comma.

KTIME = 46186158000  # ticks per second
FBX_ROTATION_ORDER = {"XYZ": 0, "XZY": 1, "YZX": 2, "YXZ": 3, "ZXY": 4, "ZYX": 5}
LINEAR, CUBIC, CONSTANT = 0x4, 0x8, 0x2


def f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def fbx_order(bvh_order):
    # BVH lists rotations outermost first; FBX names them in application order
    return FBX_ROTATION_ORDER[bvh_order[::-1]]


def P(name, kind, label, flags, *values):
    return ("P", [("S", name), ("S", kind), ("S", label), ("S", flags)] + list(values), [])


def vec(name, values, kind="Vector3D", label="Vector", flags=""):
    return P(name, kind, label, flags, *[("D", float(v)) for v in values])


def ktime(name, value):
    return P(name, "KTime", "Time", "", ("L", value))


class FbxScene:
    """Objects and connections of one FBX document."""

    def __init__(self, settings):
        self.ids = iter(range(1000001, 9000000))
        self.objects, self.connections, self.settings = [], [], settings

    def add(self, kind, name, subclass, children):
        object_id = next(self.ids)
        self.objects.append((kind, [("L", object_id), ("N", name), ("S", subclass)], children))
        return object_id

    def oo(self, child, parent):
        self.connections.append(("C", [("S", "OO"), ("L", child), ("L", parent)], []))

    def op(self, child, parent, prop):
        self.connections.append(("C", [("S", "OP"), ("L", child), ("L", parent), ("S", prop)], []))

    def model(self, name, subclass, parent, props):
        model = self.add("Model", "Model::" + name, subclass, [
            ("Version", [("I", 232)], []), ("Properties70", [], props), ("Culling", [("S", "CullingOff")], [])])
        self.oo(model, parent)
        return model

    def curve(self, node, axis, times, values, flags, slopes=None):
        # one key attribute group per key keeps per-key modes and slopes exact
        slopes = slopes or [(0.0, 0.0)] * len(times)
        curve = self.add("AnimationCurve", "AnimCurve::", "", [
            ("Default", [("D", values[0])], []),
            ("KeyVer", [("I", 4009)], []),
            ("KeyTime", [("l", times)], []),
            ("KeyValueFloat", [("f", [f32(v) for v in values])], []),
            ("KeyAttrFlags", [("i", flags)], []),
            ("KeyAttrDataFloat", [("f", [f32(x) for right, left in slopes for x in (right, left, 0.0, 0.0)])], []),
            ("KeyAttrRefCount", [("i", [1] * len(times))], [])])
        self.op(curve, node, "d|" + axis)

    def curve_node(self, layer, model, kind, prop, defaults):
        node = self.add("AnimationCurveNode", "AnimCurveNode::" + kind, "", [
            ("Properties70", [], [P("d|" + a, "Number", "", "A", ("D", defaults[i])) for i, a in enumerate("XYZ")])])
        self.oo(node, layer)
        self.op(node, model, prop)
        return node

    def stack(self, name, start, stop):
        stack = self.add("AnimationStack", "AnimStack::" + name, "", [
            ("Properties70", [], [ktime("LocalStart", start), ktime("LocalStop", stop),
                                  ktime("ReferenceStart", start), ktime("ReferenceStop", stop)])])
        layer = self.add("AnimationLayer", "AnimLayer::BaseLayer", "", [])
        self.oo(layer, stack)
        return layer

    def records(self, version):
        s = self.settings

        def axis(name, value):
            return P(name, "int", "Integer", "", ("I", value))

        return [
            ("FBXHeaderExtension", [], [
                ("FBXHeaderVersion", [("I", 1003)], []),
                ("FBXVersion", [("I", version)], []),
                ("Creator", [("S", "lambda motion fixture generator (CC0)")], [])]),
            ("GlobalSettings", [], [
                ("Version", [("I", 1000)], []),
                ("Properties70", [], [
                    axis("UpAxis", s["up"][0]), axis("UpAxisSign", s["up"][1]),
                    axis("FrontAxis", s["front"][0]), axis("FrontAxisSign", s["front"][1]),
                    axis("CoordAxis", s["coord"][0]), axis("CoordAxisSign", s["coord"][1]),
                    P("UnitScaleFactor", "double", "Number", "", ("D", s["unit"])),
                    P("TimeMode", "enum", "", "", ("I", 6)),  # 30 frames per second
                    P("CustomFrameRate", "double", "Number", "", ("D", -1.0))])]),
            ("References", [], []),
            ("Objects", [], self.objects),
            ("Connections", [], self.connections),
        ]


Y_UP_CM = {"up": (1, 1), "front": (2, 1), "coord": (0, 1), "unit": 1.0}
Z_UP_INCH = {"up": (2, 1), "front": (1, -1), "coord": (0, 1), "unit": 2.54}


def to_character(settings, p):
    """A file-space point in character metres: x = right, y = up, z = front."""
    s = settings["unit"] / 100
    return [settings[k][1] * p[settings[k][0]] * s for k in ("coord", "up", "front")]


# ---- 4x4 row-major matrices for the FBX oracle ------------------------------

def m4(rotation=None, translation=(0.0, 0.0, 0.0)):
    r = rotation or [[1, 0, 0], [0, 1, 0], [0, 0, 1]]
    return [list(r[0]) + [translation[0]], list(r[1]) + [translation[1]], list(r[2]) + [translation[2]], [0, 0, 0, 1]]


def m4_mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def m4_chain(*matrices):
    out = m4()
    for m in matrices:
        out = m4_mul(out, m)
    return out


def m4_inverse(a):
    det = (a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0])
           + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]))
    cof = [[(a[(j + 1) % 3][(i + 1) % 3] * a[(j + 2) % 3][(i + 2) % 3] - a[(j + 1) % 3][(i + 2) % 3] * a[(j + 2) % 3][(i + 1) % 3]) / det
            for j in range(3)] for i in range(3)]
    t = [-sum(cof[i][k] * a[k][3] for k in range(3)) for i in range(3)]
    return m4(cof, t)


def apply(m, p):
    return [sum(m[i][k] * p[k] for k in range(3)) + m[i][3] for i in range(3)]


def m4_translate(v, sign=1.0):
    return m4(None, [sign * x for x in v])


def m4_scale(v):
    return m4([[v[0], 0, 0], [0, v[1], 0], [0, 0, v[2]]])


def m4_euler(degrees, order):
    rotation = [[1, 0, 0], [0, 1, 0], [0, 0, 1]]
    for axis in order:  # application order: later axes multiply on the left
        rotation = mat_mul(axis_matrix(axis, degrees["XYZ".index(axis)]), rotation)
    return m4(rotation)


def fbx_local(node, t, r, s):
    """T · Roff · Rp · Rpre · R · Rpost⁻¹ · Rp⁻¹ · Soff · Sp · S · Sp⁻¹"""
    post = m4_euler(node.get("post", (0, 0, 0)), "XYZ")
    post_inverse = m4([[post[j][i] for j in range(3)] for i in range(3)])
    rp, sp = node.get("rp", (0, 0, 0)), node.get("sp", (0, 0, 0))
    return m4_chain(m4_translate(t), m4_translate(node.get("roff", (0, 0, 0))), m4_translate(rp),
                    m4_euler(node.get("pre", (0, 0, 0)), "XYZ"), m4_euler(r, node.get("order", "XYZ")), post_inverse,
                    m4_translate(rp, -1), m4_translate(node.get("soff", (0, 0, 0))), m4_translate(sp), m4_scale(s),
                    m4_translate(sp, -1))


def hermite(keys, t):
    """keys: [(tick, value, mode, right slope, left slope of the next key)]"""
    if t <= keys[0][0]: return keys[0][1]
    if t >= keys[-1][0]: return keys[-1][1]
    for (t0, v0, mode, right, left), (t1, v1, *_rest) in zip(keys, keys[1:]):
        if t0 <= t < t1:
            u = (t - t0) / (t1 - t0)
            if mode == CONSTANT: return v0
            if mode == LINEAR: return v0 + (v1 - v0) * u
            span = (t1 - t0) / KTIME
            m0, m1 = right * span, left * span
            return (2 * u ** 3 - 3 * u ** 2 + 1) * v0 + (u ** 3 - 2 * u ** 2 + u) * m0 + (-2 * u ** 3 + 3 * u ** 2) * v1 + (u ** 3 - u ** 2) * m1


# ---- writers ----------------------------------------------------------------

ARRAY_FORMAT = {"f": "f", "d": "d", "i": "i", "l": "q"}


def fbx_property(code, value, compress):
    if code in "ILDF":
        return code.encode() + struct.pack("<" + {"I": "i", "L": "q", "D": "d", "F": "f"}[code], value)
    if code in "SNR":
        if code == "N":
            kind, _, name = value.partition("::")
            data = name.encode() + b"\x00\x01" + kind.encode()
        else:
            data = value if code == "R" else value.encode()
        return (b"R" if code == "R" else b"S") + struct.pack("<I", len(data)) + data
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


def write_fbx_binary(path, scene, version, compress):
    wide = version >= 7500
    out = bytearray(b"Kaydara FBX Binary  \x00\x1a\x00" + struct.pack("<I", version))
    for record in scene.records(version):
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


def write_fbx_ascii(path, scene, version):
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
        if len(properties) == 1 and properties[0][0] == "R":
            lines.append('%s%s: , "%s"' % (pad, name, base64.b64encode(properties[0][1]).decode()))
            return
        head = "%s%s: %s" % (pad, name, ", ".join(fbx_text(c, v) for c, v in properties))
        if not children and properties:
            lines.append(head)
            return
        lines.append(head.rstrip() + " {")
        for child in children:
            emit(child, depth + 1)
        lines.append(pad + "}")

    for record in scene.records(version):
        emit(record, 0)
    path.write_text("\n".join(lines) + "\n")


def png(width, height, pixel):
    """A small RGB PNG; `pixel(x, y)` gives each texel, rows from the top."""
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    raw = b"".join(b"\x00" + bytes(c for x in range(width) for c in pixel(x, y)) for y in range(height))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def box(lo, hi):
    """Eight corners (bit 0 x, bit 1 y, bit 2 z) and six outward quads."""
    corners = [[(hi if (c >> axis) & 1 else lo)[axis] for axis in range(3)] for c in range(8)]
    quads = [(0, 4, 6, 2), (1, 3, 7, 5), (0, 1, 5, 4), (2, 6, 7, 3), (0, 2, 3, 1), (4, 5, 7, 6)]
    normals = [(-1, 0, 0), (1, 0, 0), (0, -1, 0), (0, 1, 0), (0, 0, -1), (0, 0, 1)]
    return corners, quads, normals


def mesh_geometry(scene, name, points, quads, normals=None, uvs=False, materials=None):
    polygon_index = [i for q in quads for i in list(q[:-1]) + [-q[-1] - 1]]
    layers = []
    if normals is not None:
        layers.append(("LayerElementNormal", [("I", 0)], [
            ("Version", [("I", 102)], []), ("Name", [("S", "")], []),
            ("MappingInformationType", [("S", "ByPolygonVertex")], []),
            ("ReferenceInformationType", [("S", "Direct")], []),
            ("Normals", [("d", [float(c) for n in normals for _ in range(4) for c in n])], [])]))
    if uvs:
        layers.append(("LayerElementUV", [("I", 0)], [
            ("Version", [("I", 101)], []), ("Name", [("S", "map1")], []),
            ("MappingInformationType", [("S", "ByPolygonVertex")], []),
            ("ReferenceInformationType", [("S", "IndexToDirect")], []),
            ("UV", [("d", [0.0, 0.0, 1.0, 0.0, 1.0, 1.0, 0.0, 1.0])], []),
            ("UVIndex", [("i", [0, 1, 2, 3] * len(quads))], [])]))
    if materials is not None:
        layers.append(("LayerElementMaterial", [("I", 0)], [
            ("Version", [("I", 101)], []), ("Name", [("S", "")], []),
            ("MappingInformationType", [("S", "ByPolygon")], []),
            ("ReferenceInformationType", [("S", "IndexToDirect")], []),
            ("Materials", [("i", materials)], [])]))
    return scene.add("Geometry", "Geometry::" + name, "Mesh", [
        ("Vertices", [("d", [float(c) for p in points for c in p])], []),
        ("PolygonVertexIndex", [("i", polygon_index)], []),
        ("GeometryVersion", [("I", 124)], [])] + layers)


def material(scene, name, color):
    return scene.add("Material", "Material::" + name, "", [
        ("Version", [("I", 102)], []), ("ShadingModel", [("S", "lambert")], []),
        ("Properties70", [], [vec("DiffuseColor", color, "Color", "", "A")])])


def skin(scene, geometry, mesh_model, bind_world, influences, joint_ids, joint_bind):
    """influences: {joint name: {point: weight}}; binds are row-major 4x4."""
    skin_id = scene.add("Deformer", "Deformer::Skin", "Skin", [("Version", [("I", 101)], [])])
    scene.oo(skin_id, geometry)
    column = lambda m: [float(m[r][c]) for c in range(4) for r in range(4)]  # FBX stores columns
    for name, weights in influences.items():
        cluster = scene.add("Deformer", "SubDeformer::" + name, "Cluster", [
            ("Version", [("I", 100)], []),
            ("Indexes", [("i", sorted(weights))], []),
            ("Weights", [("d", [float(weights[i]) for i in sorted(weights)])], []),
            ("Transform", [("d", column(bind_world))], []),
            ("TransformLink", [("d", column(joint_bind[name]))], [])])
        scene.oo(cluster, skin_id)
        scene.oo(joint_ids[name], cluster)


# ---- the jump: skeleton, body and clip --------------------------------------

def jump_scene():
    scene = FbxScene(Y_UP_CM)
    stop = (FRAMES - 1) * KTIME // int(FPS)
    times = [frame * KTIME // int(FPS) for frame in range(FRAMES)]
    poses = [pose(frame) for frame in range(FRAMES)]
    layer = scene.stack("jump", 0, stop)
    models, rest = {}, {}
    for name, parent, offset, order, _tip in SKELETON:
        rest[name] = [rest[parent][i] + offset[i] for i in range(3)] if parent else list(offset)
        model = scene.model(name, "LimbNode", models[parent] if parent else 0, [
            P("RotationOrder", "enum", "", "", ("I", fbx_order(order))),
            P("RotationActive", "bool", "", "", ("I", 1)),
            P("InheritType", "enum", "", "", ("I", 1)),
            vec("Lcl Translation", offset, "Lcl Translation", "", "A"),
            vec("Lcl Rotation", (0, 0, 0), "Lcl Rotation", "", "A"),
            vec("Lcl Scaling", (1, 1, 1), "Lcl Scaling", "", "A")])
        models[name] = model
        attribute = scene.add("NodeAttribute", "NodeAttribute::" + name, "LimbNode", [("TypeFlags", [("S", "Skeleton")], [])])
        scene.oo(attribute, model)
        tracks = [("T", "Lcl Translation", [p[name][0] for p in poses])] if parent is None else []
        tracks.append(("R", "Lcl Rotation", [[p[name][1].get(a, 0.0) for a in "XYZ"] for p in poses]))
        for kind, prop, values in tracks:
            node = scene.curve_node(layer, model, kind, prop, values[0])
            for i, axis in enumerate("XYZ"):
                scene.curve(node, axis, times, [v[i] for v in values], [LINEAR] * FRAMES)

    # one box per bone segment, the near end weighted to the bone and the far
    # end shared with the child; the head's box takes the textured material
    points, quads, normals, materials, influences = [], [], [], [], {}
    for name, parent, offset, order, tip in SKELETON:
        ends = [(child, rest[child]) for child, p, *_ in SKELETON if p == name]
        if tip:
            ends.append((None, [rest[name][i] + tip[i] for i in range(3)]))
        for child, tail in ends:
            head = rest[name]
            a = [head[i] + 0.1 * (tail[i] - head[i]) for i in range(3)]
            b = [head[i] + 0.9 * (tail[i] - head[i]) for i in range(3)]
            r = 6.0 if name == "Head" and child is None else 3.0
            corners, faces, face_normals = box([min(a[i], b[i]) - r for i in range(3)], [max(a[i], b[i]) + r for i in range(3)])
            base = len(points)
            d = [tail[i] - head[i] for i in range(3)]
            for corner in corners:
                along = sum((corner[i] - head[i]) * d[i] for i in range(3)) / sum(x * x for x in d)
                weights = {name: 0.5, child: 0.5} if child and along > 0.5 else {name: 1.0}
                for joint, w in weights.items():
                    influences.setdefault(joint, {})[len(points)] = w
                points.append([round(c, 6) for c in corner])
            quads += [tuple(base + i for i in q) for q in faces]
            normals += face_normals
            materials += [1 if name == "Head" and child is None else 0] * 6
    body = scene.model("Body", "Mesh", 0, [])
    geometry = mesh_geometry(scene, "Body", points, quads, normals, uvs=True, materials=materials)
    scene.oo(geometry, body)
    skin_material = material(scene, "Skin", (0.8, 0.6, 0.5))
    face = material(scene, "Face", (1.0, 1.0, 1.0))
    scene.oo(skin_material, body)
    scene.oo(face, body)
    # an 8x8 texture, red above blue, embedded in its Video
    image = png(8, 8, lambda x, y: (220, 40, 40) if y < 4 else (40, 60, 220))
    texture = scene.add("Texture", "Texture::face", "", [
        ("Type", [("S", "TextureVideoClip")], []), ("FileName", [("S", "face.png")], []),
        ("RelativeFilename", [("S", "face.png")], [])])
    video = scene.add("Video", "Video::face", "Clip", [
        ("Type", [("S", "Clip")], []), ("RelativeFilename", [("S", "face.png")], []),
        ("Content", [("R", image)], [])])
    scene.oo(video, texture)
    scene.op(texture, face, "DiffuseColor")
    joint_bind = {name: m4(None, rest[name]) for name in rest}
    skin(scene, geometry, body, m4(), influences, models, joint_bind)
    return scene, points, influences, rest


def jump_skinned(points, influences, rest, frame):
    world = {}
    p = pose(frame)
    for name, parent, offset, order, _tip in SKELETON:
        position, angles = p[name]
        local = m4_mul(m4_translate(position if position is not None else offset), m4_euler([angles.get(a, 0.0) for a in "XYZ"], order[::-1]))
        world[name] = m4_mul(world[parent], local) if parent else local
    out = {}
    for index, point in enumerate(points):
        total = [0.0, 0.0, 0.0]
        for joint, weights in influences.items():
            if index in weights:
                q = apply(m4_mul(world[joint], m4_translate(rest[joint], -1)), point)
                total = [total[i] + weights[index] * q[i] for i in range(3)]
        out[key_mm(to_character(Y_UP_CM, point))] = [round(c, 9) for c in to_character(Y_UP_CM, total)]
    assert len(out) == len(points), "rest positions must be unique keys"
    return out


def key_mm(p):
    return "%d,%d,%d" % tuple(round(c * 1000) for c in p)


# ---- the rig: transform-chain, binding and baking cases ----------------------

RIG = [  # name, parent, model type, chain properties
    ("Root", None, "Null", {"t": (0, 0, 0), "r": (0, 0, 30)}),
    ("Upper", "Root", "LimbNode", {"t": (0, 0, 10), "r": (5, 10, 20), "s": (1, 1.5, 1), "order": "YZX",
                                   "roff": (1, 0, 0), "rp": (0, 0.5, 0), "pre": (10, 0, 0), "post": (0, 0, 15),
                                   "soff": (0.5, 0, 0), "sp": (0, 0, 1)}),
    ("Lower", "Upper", "LimbNode", {"t": (0, 0, 8), "order": "ZYX"}),
    ("Hand", "Lower", "LimbNode", {"t": (0, 0, 6)}),
    ("Finger1", "Hand", "LimbNode", {"t": (1, 0, 2)}),
    ("Finger2", "Hand", "LimbNode", {"t": (-1, 0, 2)}),
]
RIG_SECOND = KTIME
RIG_CURVES = {  # stack: {(joint, axis): keys [(tick, value, mode, right, left)]}
    "curl": {("Lower", "X"): [(0, 0.0, CUBIC, 90.0, 0.0), (RIG_SECOND, 60.0, CUBIC, 0.0, -45.0), (2 * RIG_SECOND, 0.0, CUBIC, 0.0, 0.0)]},
    "wave": {("Finger1", "Y"): [(0, 0.0, LINEAR, 0, 0), (RIG_SECOND, 350.0, LINEAR, 0, 0), (2 * RIG_SECOND, 350.0, LINEAR, 0, 0)],
             ("Hand", "Z"): [(0, 0.0, CONSTANT, 0, 0), (RIG_SECOND, 45.0, CONSTANT, 0, 0), (2 * RIG_SECOND, 45.0, CONSTANT, 0, 0)]},
}
RIG_BIND_BEND = 30.0  # Lower is bound bent about X; its rest pose is straight


def rig_world(stack=None, tick=0, bind=False):
    world = {}
    for name, parent, _kind, node in RIG:
        r = list(node.get("r", (0, 0, 0)))
        if bind and name == "Lower":
            r[0] = RIG_BIND_BEND
        for (joint, axis), keys in RIG_CURVES.get(stack, {}).items():
            if joint == name:
                r["XYZ".index(axis)] = hermite(keys, tick)
        local = fbx_local(node, node["t"], r, node.get("s", (1, 1, 1)))
        world[name] = m4_mul(world[parent], local) if parent else local
    return world


def rig_scene():
    scene = FbxScene(Z_UP_INCH)
    models = {}
    for name, parent, kind, node in RIG:
        props = [P("RotationOrder", "enum", "", "", ("I", FBX_ROTATION_ORDER[node.get("order", "XYZ")])),
                 P("RotationActive", "bool", "", "", ("I", 1)), P("InheritType", "enum", "", "", ("I", 1)),
                 vec("Lcl Translation", node["t"], "Lcl Translation", "", "A"),
                 vec("Lcl Rotation", node.get("r", (0, 0, 0)), "Lcl Rotation", "", "A"),
                 vec("Lcl Scaling", node.get("s", (1, 1, 1)), "Lcl Scaling", "", "A")]
        for key, label in (("roff", "RotationOffset"), ("rp", "RotationPivot"), ("pre", "PreRotation"),
                           ("post", "PostRotation"), ("soff", "ScalingOffset"), ("sp", "ScalingPivot")):
            if key in node:
                props.append(vec(label, node[key]))
        models[name] = scene.model(name, kind, models[parent] if parent else 0, props)
    # a camera is outside the profile and is omitted with a warning
    camera = scene.model("Camera", "Camera", 0, [vec("Lcl Translation", (0, -40, 10), "Lcl Translation", "", "A")])
    scene.oo(scene.add("NodeAttribute", "NodeAttribute::Camera", "Camera", []), camera)

    # the skinned body, bound with Lower bent, offset by a geometric translation
    bind = rig_world(bind=True)
    mesh_world = m4_translate((0, 0, 2))
    geometric = (0, 0, 1)
    points = [(x, y, z) for z in (3, 9, 15, 21) for x in (-2, 2) for y in (-1, 1)]
    quads = [(0, 1, 3, 2), (4, 6, 7, 5), (8, 9, 11, 10), (12, 14, 15, 13), (0, 4, 5, 1), (8, 12, 13, 9)]
    materials = [0, 0, 1, 1, 0, 1]
    influences = {"Upper": {}, "Lower": {}, "Hand": {}, "Finger1": {}, "Finger2": {}}
    for i, (x, y, z) in enumerate(points):
        if z < 10: influences["Upper"][i] = 1.0
        elif z < 18: influences["Upper"][i], influences["Lower"][i] = 0.4, 0.6
        else: influences["Lower"][i], influences["Hand"][i] = 0.7, 0.3
    # one point with five influences keeps its four largest, renormalized
    for joint, w in zip(("Upper", "Lower", "Hand", "Finger1", "Finger2"), (0.3, 0.25, 0.2, 0.15, 0.1)):
        influences[joint][15] = w
    body = scene.model("Body", "Mesh", 0, [vec("Lcl Translation", (0, 0, 2), "Lcl Translation", "", "A"),
                                           vec("GeometricTranslation", geometric)])
    geometry = mesh_geometry(scene, "Body", points, quads, materials=materials)
    scene.oo(geometry, body)
    for name, color in (("Cloth", (0.2, 0.4, 0.8)), ("Trim", (0.9, 0.8, 0.1))):
        scene.oo(material(scene, name, color), body)
    skin(scene, geometry, body, mesh_world, influences, models, {name: bind[name] for name in influences})

    # a rigid prop under the hand, with no skin of its own
    prop = scene.model("Prop", "Mesh", models["Hand"], [vec("Lcl Translation", (0, 1, 1), "Lcl Translation", "", "A")])
    prop_points = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)]
    prop_geometry = mesh_geometry(scene, "Prop", prop_points, [(0, 1, 2, 3)])
    scene.oo(prop_geometry, prop)

    for stack, curves in (("curl", RIG_CURVES["curl"]), ("wave", RIG_CURVES["wave"]), ("wave", {})):
        layer = scene.stack(stack, 0, 2 * RIG_SECOND)
        by_joint = {}
        for (joint, axis), keys in curves.items():
            by_joint.setdefault(joint, []).append((axis, keys))
        for joint, axes in by_joint.items():
            node = next(n for nm, _p, _k, n in RIG if nm == joint)
            rest_r = node.get("r", (0, 0, 0))
            curve_node = scene.curve_node(layer, models[joint], "R", "Lcl Rotation", rest_r)
            for axis, keys in axes:
                scene.curve(curve_node, axis, [k[0] for k in keys], [k[1] for k in keys], [k[2] for k in keys],
                            [(k[3], k[4]) for k in keys])

    # expectations in character metres
    def positions(world):
        return {name: [round(c, 9) for c in to_character(Z_UP_INCH, apply(m, (0, 0, 0)))] for name, m in world.items()}

    def skinned(world):
        out = {}
        for index, point in enumerate(points):
            placed = apply(m4_mul(mesh_world, m4_translate(geometric)), point)
            ranked = sorted(((w[index], j) for j, w in influences.items() if index in w), reverse=True)[:4]
            total_weight = sum(w for w, _j in ranked)
            total = [0.0, 0.0, 0.0]
            for w, joint in ranked:
                q = apply(m4_mul(world[joint], m4_inverse(bind[joint])), placed)
                total = [total[i] + w / total_weight * q[i] for i in range(3)]
            out[key_mm(to_character(Z_UP_INCH, placed))] = [round(c, 9) for c in to_character(Z_UP_INCH, total)]
        assert len(out) == len(points), "rest positions must be unique keys"
        return out

    # the prop rides the hand rigidly from its rest placement
    prop_rest = m4_mul(rig_world()["Hand"], m4_translate((0, 1, 1)))

    def prop_points_at(world):
        moved = m4_mul(m4_mul(world["Hand"], m4_inverse(rig_world()["Hand"])), prop_rest)
        return [[round(c, 9) for c in to_character(Z_UP_INCH, apply(moved, p))] for p in prop_points]

    half = RIG_SECOND // 2
    expected = {
        "rest": positions(rig_world()),
        "curl_half": positions(rig_world("curl", half)),
        "wave_half": positions(rig_world("wave", half)),
        "curl_half_skinned": skinned(rig_world("curl", half)),
        "rest_skinned": skinned(rig_world()),
        "prop_rest": prop_points_at(rig_world()),
        "prop_curl_half": prop_points_at(rig_world("curl", half)),
    }
    return scene, expected


write_bvh(root / "jump.bvh")
jump, jump_points, jump_influences, jump_rest = jump_scene()
write_fbx_binary(root / "jump.fbx", jump, 7400, compress=False)
write_fbx_binary(root / "jump-7500.fbx", jump, 7500, compress=True)
write_fbx_ascii(root / "jump-ascii.fbx", jump, 7400)
rig, rig_expected = rig_scene()
write_fbx_binary(root / "rig.fbx", rig, 7400, compress=True)
expected = {"frames": FRAMES, "fps": FPS, "units": "cm",
            "positions": {str(f): world_positions(f) for f in (0, 8, 15, 30)}}
(root / "expected.json").write_text(json.dumps(expected, indent=1, sort_keys=True) + "\n")
expected_fbx = {"jump_skinned": {str(f): jump_skinned(jump_points, jump_influences, jump_rest, f) for f in (0, 15)},
                "rig": rig_expected}
(root / "expected-fbx.json").write_text(json.dumps(expected_fbx, indent=1, sort_keys=True) + "\n")
