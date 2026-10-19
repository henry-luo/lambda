"""Build the Chengdu city model used by cityscape.ls from OpenStreetMap data.

Standard library only. Typical use from the repository root:

    python3 -I test/demo/cityscape/_generate.py --fetch --osm temp/chengdu-osm

--fetch downloads any missing OSM extracts through the Overpass API (about 37 MB).
The script writes assets/city.glb (buildings, water, roads) and
assets/city.json (outlines, landmarks and statistics for the story).

Units are kilometres. x points east, y up, z south; the origin is Tianfu Square.
OSM tags heights for only a few buildings; the rest are estimated
deterministically from footprint shape and building type (see estimate_height).
"""
import argparse, glob, hashlib, json, math, os, struct, sys, time, urllib.parse, urllib.request

LAT0, LON0 = 30.6574, 104.0658                   # Tianfu Square
KX, KZ = 111.320 * math.cos(math.radians(LAT0)), 110.574   # km per degree
HALF = math.sqrt(319.0) / 2                       # a 319 km² square slab, as in the film
BBOX = (30.575, 103.965, 30.755, 104.165)         # south, west, north, east; covers the slab
SAMPLES = 240                                     # polar samples per outline
FLOW_BUCKETS = 8                                  # water slices along the north-west → south-east axis
BANDS_PER_ZONE = 3
ZONES = ["core", "ring1", "ring2", "ring3", "outer"]
OVERPASS = ["https://overpass-api.de/api/interpreter", "https://overpass.private.coffee/api/interpreter"]

def project(lat, lon):
    return ((lon - LON0) * KX, -(lat - LAT0) * KZ)

# ---------------------------------------------------------------- download

def overpass(query, path):
    body = urllib.parse.urlencode({"data": query}).encode()
    for attempt in range(6):
        for endpoint in OVERPASS:
            request = urllib.request.Request(endpoint, body, {"User-Agent": "LambdaScriptDemo/1.0 (chengdu scene3d demo)"})
            try:
                with urllib.request.urlopen(request, timeout=400) as response:
                    data = response.read()
                json.loads(data)
                with open(path, "wb") as out:
                    out.write(data)
                print("fetched", path, len(data), "bytes")
                return
            except Exception as failure:  # busy servers answer with HTML or time out
                print("retry", path, endpoint, str(failure)[:80])
        time.sleep(15 * (attempt + 1))
    sys.exit("overpass: giving up on " + path)

def fetch(osm):
    os.makedirs(osm, exist_ok=True)
    s, w, n, e = BBOX
    mid_lat, mid_lon = (s + n) / 2, (w + e) / 2
    tiles = [(s, w, mid_lat, mid_lon), (s, mid_lon, mid_lat, e), (mid_lat, w, n, mid_lon), (mid_lat, mid_lon, n, e)]
    # whole-area building queries overload public servers, so buildings come in quadrants.
    for i, tile in enumerate(tiles, 1):
        path = os.path.join(osm, "buildings_%d.json" % i)
        if not os.path.exists(path):
            box = "%.4f,%.4f,%.4f,%.4f" % tile
            overpass('[out:json][timeout:300];(way["building"](%s);relation["building"](%s););out tags geom;' % (box, box), path)
    box = "%.4f,%.4f,%.4f,%.4f" % BBOX
    if not os.path.exists(os.path.join(osm, "water.json")):
        overpass('[out:json][timeout:240];(way["waterway"~"^(river|canal|stream)$"](%s);way["natural"="water"](%s);'
                 'relation["natural"="water"](%s););out tags geom;' % (box, box, box), os.path.join(osm, "water.json"))
    if not os.path.exists(os.path.join(osm, "roads.json")):
        overpass('[out:json][timeout:240];(way["highway"~"^(motorway|trunk|primary|secondary|motorway_link|trunk_link)$"](%s););'
                 'out tags geom;' % box, os.path.join(osm, "roads.json"))

# ---------------------------------------------------------------- geometry helpers

def close(a, b, eps=1e-6):
    return abs(a[0] - b[0]) < eps and abs(a[1] - b[1]) < eps

def line(geometry):
    return [project(p["lat"], p["lon"]) for p in geometry if p]

def assemble(parts):
    """Join open member ways into closed rings (multipolygon outers are often split)."""
    rings, pending = [], []
    for part in parts:
        if len(part) >= 4 and close(part[0], part[-1]):
            rings.append(part)
        elif len(part) >= 2:
            pending.append(list(part))
    while pending:
        current = pending.pop()
        joined = True
        while joined and not close(current[0], current[-1]):
            joined = False
            for i, other in enumerate(pending):
                if close(current[-1], other[0]): current = current + other[1:]
                elif close(current[-1], other[-1]): current = current + other[-2::-1]
                elif close(current[0], other[-1]): current = other[:-1] + current
                elif close(current[0], other[0]): current = other[:0:-1] + current
                else: continue
                pending.pop(i)
                joined = True
                break
        if len(current) >= 4 and close(current[0], current[-1]):
            rings.append(current)
    return rings

def outer_rings(element):
    if element["type"] == "way":
        return assemble([line(element.get("geometry", []))])
    return assemble([line(m["geometry"]) for m in element.get("members", [])
                     if m.get("role") == "outer" and m.get("geometry")])

def signed_area(points):
    return 0.5 * sum(points[i - 1][0] * points[i][1] - points[i][0] * points[i - 1][1] for i in range(len(points)))

def centroid(points):
    return (sum(p[0] for p in points) / len(points), sum(p[1] for p in points) / len(points))

def segment_distance(p, a, b):
    dx, dz = b[0] - a[0], b[1] - a[1]
    length = dx * dx + dz * dz
    t = 0.0 if length == 0 else max(0.0, min(1.0, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dz) / length))
    return math.hypot(p[0] - a[0] - t * dx, p[1] - a[1] - t * dz)

def douglas_peucker(points, tolerance):
    if len(points) < 3:
        return points
    keep, stack = [False] * len(points), [(0, len(points) - 1)]
    keep[0] = keep[-1] = True
    while stack:
        start, end = stack.pop()
        best, index = 0.0, -1
        for i in range(start + 1, end):
            d = segment_distance(points[i], points[start], points[end])
            if d > best: best, index = d, i
        if index >= 0 and best > tolerance:
            keep[index] = True
            stack += [(start, index), (index, end)]
    return [p for p, k in zip(points, keep) if k]

def clean_ring(ring, tolerance=0.0006, min_area=0.00002):
    """Closed ring → counter-clockwise (positive shoelace) simple polygon without the closing point."""
    points = ring[:-1] if close(ring[0], ring[-1]) else list(ring)
    deduped = []
    for p in points:
        if not deduped or not close(p, deduped[-1], 1e-5): deduped.append(p)
    while len(deduped) > 1 and close(deduped[0], deduped[-1], 1e-5): deduped.pop()
    if len(deduped) < 3:
        return None
    # split at the farthest vertex so the closed ring simplifies as two open chains.
    far = max(range(len(deduped)), key=lambda i: (deduped[i][0] - deduped[0][0]) ** 2 + (deduped[i][1] - deduped[0][1]) ** 2)
    simplified = douglas_peucker(deduped[:far + 1], tolerance)[:-1] + douglas_peucker(deduped[far:] + [deduped[0]], tolerance)[:-1]
    if len(simplified) < 3:
        return None
    area = signed_area(simplified)
    if abs(area) < min_area:
        return None
    return simplified if area > 0 else simplified[::-1]

def clip_polygon(points, half):
    """Sutherland–Hodgman against the square slab."""
    for axis, sign in ((0, 1), (0, -1), (1, 1), (1, -1)):
        if not points: break
        inside = lambda p: sign * p[axis] <= half
        clipped = []
        for i, current in enumerate(points):
            previous = points[i - 1]
            if inside(current) != inside(previous):
                t = (half - sign * previous[axis]) / (sign * current[axis] - sign * previous[axis])
                clipped.append((previous[0] + t * (current[0] - previous[0]), previous[1] + t * (current[1] - previous[1])))
            if inside(current): clipped.append(current)
        points = clipped
    return points

def clip_segment(a, b, half):
    """Liang–Barsky against the square slab."""
    t0, t1 = 0.0, 1.0
    dx, dz = b[0] - a[0], b[1] - a[1]
    for p, q in ((-dx, a[0] + half), (dx, half - a[0]), (-dz, a[1] + half), (dz, half - a[1])):
        if p == 0:
            if q < 0: return None
        else:
            t = q / p
            if p < 0: t0 = max(t0, t)
            else: t1 = min(t1, t)
    if t0 > t1: return None
    return (a[0] + t0 * dx, a[1] + t0 * dz), (a[0] + t1 * dx, a[1] + t1 * dz)

def cross2(a, b, c):
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])

def in_triangle(p, a, b, c):
    return cross2(a, b, p) >= 0 and cross2(b, c, p) >= 0 and cross2(c, a, p) >= 0

def triangulate(points):
    """Ear clipping for a counter-clockwise polygon; returns index triples."""
    indices, result = list(range(len(points))), []
    guard = 0
    while len(indices) > 3 and guard < 4 * len(points):
        guard += 1
        n, clipped = len(indices), False
        for k in range(n):
            i, j, l = indices[k - 1], indices[k], indices[(k + 1) % n]
            a, b, c = points[i], points[j], points[l]
            if cross2(a, b, c) <= 1e-12: continue
            if any(in_triangle(points[m], a, b, c) for m in indices if m not in (i, j, l)): continue
            result.append((i, j, l))
            indices.pop(k)
            clipped = True
            break
        if not clipped:  # self-touching OSM rings: fall back to a fan for the remainder
            break
    for k in range(1, len(indices) - 1):
        result.append((indices[0], indices[k], indices[k + 1]))
    return result

# ---------------------------------------------------------------- mesh building

class Mesh:
    def __init__(self):
        self.positions, self.normals, self.indices = [], [], []

    def vertex(self, p, n):
        self.positions += p
        self.normals += n
        return len(self.positions) // 3 - 1

    def triangle(self, a, b, c, normal):
        pa, pb, pc = (self.positions[i * 3:i * 3 + 3] for i in (a, b, c))
        u = [pb[k] - pa[k] for k in range(3)]
        v = [pc[k] - pa[k] for k in range(3)]
        face = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
        # glTF front faces wind counter-clockwise around the outward normal.
        if face[0] * normal[0] + face[1] * normal[1] + face[2] * normal[2] < 0: b, c = c, b
        self.indices += [a, b, c]

    def flat(self, polygon, y):
        base = [self.vertex([p[0], y, p[1]], [0.0, 1.0, 0.0]) for p in polygon]
        for i, j, k in triangulate(polygon):
            self.triangle(base[i], base[j], base[k], (0.0, 1.0, 0.0))

    def prism(self, polygon, height):
        self.flat(polygon, height)
        for i in range(len(polygon)):
            a, b = polygon[i], polygon[(i + 1) % len(polygon)]
            dx, dz = b[0] - a[0], b[1] - a[1]
            length = math.hypot(dx, dz)
            if length < 1e-6: continue
            normal = (dz / length, 0.0, -dx / length)    # outward for counter-clockwise rings
            n = list(normal)
            v = [self.vertex([a[0], 0.0, a[1]], n), self.vertex([b[0], 0.0, b[1]], n),
                 self.vertex([b[0], height, b[1]], n), self.vertex([a[0], height, a[1]], n)]
            self.triangle(v[0], v[1], v[2], normal)
            self.triangle(v[0], v[2], v[3], normal)

    def ribbon(self, a, b, width, y):
        dx, dz = b[0] - a[0], b[1] - a[1]
        length = math.hypot(dx, dz)
        if length < 1e-6: return
        ux, uz = dx / length, dz / length
        # square caps overlap neighbouring segments so polylines show no joints.
        a = (a[0] - ux * width / 2, a[1] - uz * width / 2)
        b = (b[0] + ux * width / 2, b[1] + uz * width / 2)
        px, pz = -uz * width / 2, ux * width / 2
        up = [0.0, 1.0, 0.0]
        v = [self.vertex([a[0] + px, y, a[1] + pz], up), self.vertex([b[0] + px, y, b[1] + pz], up),
             self.vertex([b[0] - px, y, b[1] - pz], up), self.vertex([a[0] - px, y, a[1] - pz], up)]
        self.triangle(v[0], v[1], v[2], (0.0, 1.0, 0.0))
        self.triangle(v[0], v[2], v[3], (0.0, 1.0, 0.0))

    def polyline(self, points, width, y):
        for a, b in zip(points, points[1:]):
            clipped = clip_segment(a, b, HALF)
            if clipped: self.ribbon(clipped[0], clipped[1], width, y)

# ---------------------------------------------------------------- heights

def hash01(key, salt=""):
    return int(hashlib.md5((salt + key).encode()).hexdigest()[:8], 16) / 0x100000000

def tagged_height(tags):
    for key in ("height", "building:height"):
        text = tags.get(key, "").replace("m", "").replace(",", ".").strip()
        try:
            value = float(text)
            if 2 <= value <= 700: return value
        except ValueError:
            pass
    try:
        levels = float(tags.get("building:levels", ""))
        if 1 <= levels <= 150: return levels * 3.0 + 1.0
    except ValueError:
        pass
    return None

def footprint_shape(polygon):
    """Area (m²), aspect ratio and short side (m) of the minimum-area edge-aligned rectangle."""
    best = None
    for i in range(len(polygon)):
        a, b = polygon[i], polygon[(i + 1) % len(polygon)]
        length = math.hypot(b[0] - a[0], b[1] - a[1])
        if length < 1e-6: continue
        ux, uz = (b[0] - a[0]) / length, (b[1] - a[1]) / length
        us = [p[0] * ux + p[1] * uz for p in polygon]
        vs = [-p[0] * uz + p[1] * ux for p in polygon]
        w, h = max(us) - min(us), max(vs) - min(vs)
        if best is None or w * h < best[0] * best[1]: best = (w, h)
    w, h = best
    return signed_area(polygon) * 1e6, max(w, h) / max(min(w, h), 1e-6), min(w, h) * 1000

def estimate_height(key, tags, polygon, zone):
    """Deterministic stand-in for missing heights (metres); see README for the rules."""
    kind = tags.get("building", "yes")
    area, aspect, short = footprint_shape(polygon)
    u, v = hash01(key), hash01(key, "floors")
    low = ("industrial", "warehouse", "shed", "garage", "garages", "roof", "greenhouse", "service", "hut",
           "kiosk", "toilets", "construction", "carport", "transformer_tower")
    if kind in low: floors = 1 + int(u * 2)
    elif kind in ("house", "detached", "terrace", "bungalow", "temple", "church", "shrine", "pagoda"): floors = 1 + int(u * 3)
    elif kind in ("kindergarten",): floors = 2 + int(u * 2)
    elif kind in ("school", "university", "college"): floors = 3 + int(u * 4)
    elif kind in ("hospital",): floors = 5 + int(u * 10)
    elif kind in ("stadium", "train_station", "transportation", "sports_centre"): floors = 5
    elif area < 120: floors = 1 + int(u * 3)
    elif area > 8000: floors = 3 + int(u * 4)          # podiums, malls and factory halls
    else:
        compact = aspect < 2.3 and 250 <= area <= 2600
        commercial = kind in ("commercial", "office", "retail", "hotel")
        if compact and commercial: floors = 15 + int(v * 30)
        elif compact:
            # towers dominate the newer rings; the old core keeps more mid-rise blocks.
            tall = (0.3, 0.45, 0.6, 0.75, 0.7)[zone]
            floors = 18 + int(v * 16) if u < tall else 7 + int(v * 8)
        elif aspect >= 2.3 and short <= 20: floors = 5 + int(v * 3) if u < 0.7 else 11 + int(v * 8)
        else: floors = 4 + int(v * 6)
    return floors * 3.0 + 1.0

# ---------------------------------------------------------------- polar outlines

def ray_hits(segments, angle):
    c, s = math.cos(angle), math.sin(angle)
    hits = []
    for a, b in segments:
        ex, ez = b[0] - a[0], b[1] - a[1]
        denom = c * ez - s * ex
        if abs(denom) < 1e-12: continue
        r = (a[0] * ez - a[1] * ex) / denom
        t = (a[0] * s - a[1] * c) / denom
        if r > 0 and 0 <= t <= 1: hits.append(r)
    return hits

def polar_outline(segments, pick, limit):
    radii = []
    for j in range(SAMPLES):
        hits = [r for r in ray_hits(segments, 2 * math.pi * j / SAMPLES) if r < limit]
        radii.append(pick(hits) if hits else None)
    known = [j for j, r in enumerate(radii) if r is not None]
    if not known: sys.exit("outline has no samples")
    for j in range(SAMPLES):   # bridge gaps (culverts, unnamed segments) by angular interpolation
        if radii[j] is None:
            before = max([k for k in known if k < j], default=known[-1] - SAMPLES)
            after = min([k for k in known if k > j], default=known[0] + SAMPLES)
            t = (j - before) / (after - before)
            radii[j] = radii[before % SAMPLES] * (1 - t) + radii[after % SAMPLES] * t
    # a circular median then mean filter removes stray hits from ramps and side branches.
    median = [sorted(radii[(j + k) % SAMPLES] for k in range(-4, 5))[4] for j in range(SAMPLES)]
    return [sum(median[(j + k) % SAMPLES] for k in range(-2, 3)) / 5 for j in range(SAMPLES)]

def radius_at(outline, angle):
    f = (angle % (2 * math.pi)) / (2 * math.pi) * SAMPLES
    j = int(f) % SAMPLES
    t = f - int(f)
    return outline[j] * (1 - t) + outline[(j + 1) % SAMPLES] * t

def edge_radius(angle):
    c, s = abs(math.cos(angle)), abs(math.sin(angle))
    return HALF / max(c, s)

def ring_position(point, outlines):
    """0..5 radial coordinate: 0–1 core, 1–2 to the First Ring, … 4–5 beyond the Third Ring."""
    r = math.hypot(point[0], point[1])
    angle = math.atan2(point[1], point[0])
    bounds = [0.0] + [radius_at(o, angle) for o in outlines] + [edge_radius(angle)]
    for k in range(5):
        if r < bounds[k + 1] or k == 4:
            return k + min(1.0, max(0.0, (r - bounds[k]) / max(bounds[k + 1] - bounds[k], 1e-6)))

def outline_points(outline):
    return [[round(r * math.cos(2 * math.pi * j / SAMPLES), 5), round(r * math.sin(2 * math.pi * j / SAMPLES), 5)]
            for j, r in enumerate(outline)]

def perimeter(points):
    return sum(math.hypot(points[i][0] - points[i - 1][0], points[i][1] - points[i - 1][1]) for i in range(len(points)))

# ---------------------------------------------------------------- glTF

def write_glb(path, nodes, materials):
    """nodes: [(name, Mesh, material index)]; materials: [(name, rgba)]."""
    binary, views, accessors, meshes, gltf_nodes = bytearray(), [], [], [], []
    def view(data, target):
        while len(binary) % 4: binary.append(0)
        views.append({"buffer": 0, "byteOffset": len(binary), "byteLength": len(data), "target": target})
        binary.extend(data)
        return len(views) - 1
    for name, mesh, material in nodes:
        if not mesh.indices: continue
        count = len(mesh.positions) // 3
        xs, ys, zs = mesh.positions[0::3], mesh.positions[1::3], mesh.positions[2::3]
        position = view(struct.pack("<%df" % len(mesh.positions), *mesh.positions), 34962)
        normal = view(struct.pack("<%df" % len(mesh.normals), *mesh.normals), 34962)
        index = view(struct.pack("<%dI" % len(mesh.indices), *mesh.indices), 34963)
        accessors += [
            {"bufferView": position, "componentType": 5126, "count": count, "type": "VEC3",
             "min": [min(xs), min(ys), min(zs)], "max": [max(xs), max(ys), max(zs)]},
            {"bufferView": normal, "componentType": 5126, "count": count, "type": "VEC3"},
            {"bufferView": index, "componentType": 5125, "count": len(mesh.indices), "type": "SCALAR"}]
        base = len(accessors) - 3
        meshes.append({"name": name, "primitives": [{"attributes": {"POSITION": base, "NORMAL": base + 1},
                                                     "indices": base + 2, "material": material, "mode": 4}]})
        gltf_nodes.append({"name": name, "mesh": len(meshes) - 1})
    while len(binary) % 4: binary.append(0)
    document = {
        "asset": {"version": "2.0", "generator": "lambda test/demo/cityscape/_generate.py",
                  "copyright": "Map data © OpenStreetMap contributors, ODbL 1.0"},
        "scene": 0, "scenes": [{"nodes": list(range(len(gltf_nodes)))}], "nodes": gltf_nodes, "meshes": meshes,
        "materials": [{"name": name, "pbrMetallicRoughness": {"baseColorFactor": rgba, "metallicFactor": 0.0, "roughnessFactor": 1.0}}
                      for name, rgba in materials],
        "accessors": accessors, "bufferViews": views, "buffers": [{"byteLength": len(binary)}]}
    text = json.dumps(document, separators=(",", ":")).encode()
    text += b" " * (-len(text) % 4)
    with open(path, "wb") as out:
        out.write(struct.pack("<4sII", b"glTF", 2, 12 + 8 + len(text) + 8 + len(binary)))
        out.write(struct.pack("<I4s", len(text), b"JSON") + text)
        out.write(struct.pack("<I4s", len(binary), b"BIN\0") + binary)
    return len(binary)

# ---------------------------------------------------------------- main

def load(path):
    with open(path, encoding="utf-8") as source:
        return json.load(source)["elements"]

def named_segments(elements, test):
    segments = []
    for e in elements:
        if not test(e.get("tags", {})): continue
        parts = [line(e["geometry"])] if e["type"] == "way" and e.get("geometry") else \
                [line(m["geometry"]) for m in e.get("members", []) if m.get("geometry")]
        for points in parts:
            segments += list(zip(points, points[1:]))
    return segments

def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--osm", default="temp/chengdu-osm", help="directory of Overpass JSON extracts")
    parser.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "assets"))
    parser.add_argument("--fetch", action="store_true", help="download missing extracts from Overpass")
    parser.add_argument("--thin", type=int, default=1, help="keep one building in N (quick previews)")
    args = parser.parse_args()
    if args.fetch: fetch(args.osm)
    os.makedirs(args.out, exist_ok=True)

    water = load(os.path.join(args.osm, "water.json"))
    roads = load(os.path.join(args.osm, "roads.json"))
    buildings = {}
    for path in sorted(glob.glob(os.path.join(args.osm, "buildings_*.json"))):
        for e in load(path): buildings[(e["type"], e["id"])] = e   # tiles overlap at their edges

    # outlines: the moat loop of the two rivers (and the western Xijiao canal) bounds the old city;
    # the ring roads come from their OSM names.
    moat = named_segments(water, lambda t: t.get("name") in ("府河", "锦江", "南河", "西郊河"))
    road_name = lambda t: t.get("name", "")
    core = [r * 0.97 for r in polar_outline(moat, min, 3.2)]          # walls stood just inside the moat
    ring1 = polar_outline(named_segments(roads, lambda t: "一环路" in road_name(t)), lambda h: sorted(h)[len(h) // 2], 6)
    ring2 = polar_outline(named_segments(roads, lambda t: "二环" in road_name(t) and "环路" in road_name(t)
                                         or "二环高架" in road_name(t)), lambda h: sorted(h)[len(h) // 2], 8)
    ring3 = polar_outline(named_segments(roads, lambda t: "三环路" in road_name(t)), lambda h: sorted(h)[len(h) // 2], 12)
    outlines = [core, ring1, ring2, ring3]

    zone_bands = [[Mesh() for _ in range(BANDS_PER_ZONE)] for _ in ZONES]
    counts = [0] * len(ZONES)
    heights, tallest, landmarks = [], None, {}
    tagged = 0
    for (kind, ident), e in sorted(buildings.items()):
        tags = e.get("tags", {})
        for ring in outer_rings(e):
            polygon = clean_ring(ring)
            if not polygon: continue
            c = centroid(polygon)
            if abs(c[0]) > HALF or abs(c[1]) > HALF: continue
            polygon = clip_polygon(polygon, HALF)
            if len(polygon) < 3 or signed_area(polygon) < 0.00002: continue
            name = tags.get("name", "")
            if name.startswith("蜀都大厦"):
                landmarks["shudu"] = {"position": [round(c[0], 4), round(c[1], 4)]}
            # previews keep landmarks but only one building in `thin`.
            if args.thin > 1 and int(hash01("%s/%d" % (kind, ident), "thin") * args.thin) != 0: continue
            s = ring_position(c, outlines)
            zone = min(4, int(s))
            band = min(BANDS_PER_ZONE - 1, int((s - zone) * BANDS_PER_ZONE))
            height = tagged_height(tags)
            if height is not None: tagged += 1
            else: height = estimate_height("%s/%d" % (kind, ident), tags, polygon, zone)
            zone_bands[zone][band].prism(polygon, height / 1000.0)
            counts[zone] += 1
            heights.append(height)
            if tags.get("height") and (tallest is None or height > tallest[0]):
                tallest = (height, name, c)

    # water: surfaces from polygons and ribbons from centre lines, sliced along the flow direction.
    flow = [Mesh() for _ in range(FLOW_BUCKETS)]
    def bucket(point):
        f = (point[0] + point[1]) / math.sqrt(2)       # increases from north-west to south-east
        return max(0, min(FLOW_BUCKETS - 1, int((f / (HALF * math.sqrt(2)) + 1) / 2 * FLOW_BUCKETS)))
    for e in water:
        tags = e.get("tags", {})
        if tags.get("natural") == "water":
            for ring in outer_rings(e):
                polygon = clean_ring(ring, 0.002, 0.00005)
                if polygon:
                    polygon = clip_polygon(polygon, HALF)
                    if len(polygon) >= 3 and signed_area(polygon) > 0.00005:
                        flow[bucket(centroid(polygon))].flat(polygon, 0.004)
        elif e["type"] == "way" and e.get("geometry"):
            points = line(e["geometry"])
            width = {"river": 0.03, "canal": 0.012}.get(tags.get("waterway"), 0.006)
            for a, b in zip(points, points[1:]):
                clipped = clip_segment(a, b, HALF)
                if clipped: flow[bucket(((a[0] + b[0]) / 2, (a[1] + b[1]) / 2))].ribbon(clipped[0], clipped[1], width, 0.005)

    road_mesh, ring_meshes = Mesh(), [Mesh(), Mesh(), Mesh()]
    widths = {"motorway": 0.03, "trunk": 0.024, "primary": 0.018, "secondary": 0.012}
    for e in roads:
        if not e.get("geometry"): continue
        name, points = e.get("tags", {}).get("name", ""), line(e["geometry"])
        ring = 0 if "一环路" in name else 1 if ("二环" in name and "环路" in name) or "二环高架" in name else 2 if "三环路" in name else -1
        if ring >= 0: ring_meshes[ring].polyline(points, 0.04, 0.006)
        else: road_mesh.polyline(points, widths.get(e["tags"].get("highway"), 0.008), 0.003)

    materials = [("building-" + z, [1.0, 1.0, 1.0, 1.0]) for z in ZONES]
    materials += [("water-%d" % k, [0.42, 0.45, 0.5, 1.0]) for k in range(FLOW_BUCKETS)]
    materials += [("road", [0.78, 0.78, 0.77, 1.0])] + [("ring%d" % k, [0.7, 0.7, 0.7, 1.0]) for k in (1, 2, 3)]
    nodes = [("band-%d" % (zone * BANDS_PER_ZONE + band), zone_bands[zone][band], zone)
             for zone in range(len(ZONES)) for band in range(BANDS_PER_ZONE)]
    nodes += [("water-%d" % k, flow[k], len(ZONES) + k) for k in range(FLOW_BUCKETS)]
    nodes += [("roads", road_mesh, len(ZONES) + FLOW_BUCKETS)]
    nodes += [("ring%d" % (k + 1), ring_meshes[k], len(ZONES) + FLOW_BUCKETS + 1 + k) for k in range(3)]
    size = write_glb(os.path.join(args.out, "city.glb"), nodes, materials)

    # historical outlines are schematic: the Qin city shrinks the moat loop, the Imperial and Lesser
    # Cities follow the old street names (御河, 东城根街) that still mark their walls.
    core_points = outline_points(core)
    west, north, south = project(LAT0, 104.0572)[0], project(30.6712, LON0)[1], project(30.6556, LON0)[1]
    lesser = core_points
    for test, cut in ((lambda p: p[0] <= west, lambda a, b: (west - a[0]) / (b[0] - a[0])),
                      (lambda p: p[1] >= north, lambda a, b: (north - a[1]) / (b[1] - a[1])),
                      (lambda p: p[1] <= south, lambda a, b: (south - a[1]) / (b[1] - a[1]))):
        clipped = []
        for i, current in enumerate(lesser):
            previous = lesser[i - 1]
            if test(current) != test(previous):
                t = cut(previous, current)
                clipped.append([round(previous[0] + t * (current[0] - previous[0]), 5), round(previous[1] + t * (current[1] - previous[1]), 5)])
            if test(current): clipped.append(current)
        lesser = clipped
    imperial = [list(project(lat, lon)) for lat, lon in ((30.6590, 104.0612), (30.6590, 104.0703), (30.6706, 104.0703), (30.6706, 104.0612))]
    qin = [[round(p[0] * 0.8 - 0.15, 5), round(p[1] * 0.8 - 0.2, 5)] for p in core_points]
    rings = {"core": core_points, "ring1": outline_points(ring1), "ring2": outline_points(ring2), "ring3": outline_points(ring3)}
    story = {
        "half": round(HALF, 5), "origin": [LAT0, LON0], "samples": SAMPLES, "bands_per_zone": BANDS_PER_ZONE,
        "zones": ZONES, "flow_buckets": FLOW_BUCKETS,
        "outlines": {**rings, "qin": qin, "imperial": [[round(x, 5), round(z, 5)] for x, z in imperial], "lesser": lesser},
        "stats": {
            "buildings": sum(counts), "zone_buildings": counts, "tagged_heights": tagged,
            "area_km2": {k: round(signed_area(v), 1) for k, v in rings.items()},
            "perimeter_km": {k: round(perimeter(v), 1) for k, v in rings.items()},
            "slab_km2": round((2 * HALF) ** 2, 1), "median_height_m": sorted(heights)[len(heights) // 2],
            "tallest_tagged": {"height": tallest[0], "name": tallest[1], "position": [round(tallest[2][0], 4), round(tallest[2][1], 4)]}},
        "landmarks": {
            "tianfu": [0.0, 0.0], "jinsha": [round(v, 4) for v in project(30.6834, 104.0130)],
            "brocade": [round(v, 4) for v in project(30.6560, 104.0420)], **landmarks}}
    # file-wide area values must be positive: outlines are emitted counter-clockwise in (x, z).
    with open(os.path.join(args.out, "city.json"), "w", encoding="utf-8") as out:
        json.dump(story, out, ensure_ascii=False, separators=(",", ":"))
    vertices = sum(len(m.positions) // 3 for _, m, _ in nodes)
    triangles = sum(len(m.indices) // 3 for _, m, _ in nodes)
    print("buildings %d (tagged heights %d) per zone %s" % (sum(counts), tagged, counts))
    print("vertices %d triangles %d binary %.1f MB" % (vertices, triangles, size / 1e6))
    print("areas", story["stats"]["area_km2"], "perimeters", story["stats"]["perimeter_km"])
    for name, mesh, _ in nodes: print("  %-8s %7d vertices" % (name, len(mesh.positions) // 3))

if __name__ == "__main__":
    main()
