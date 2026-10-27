#!/usr/bin/env python3
"""Independent oracle for lambda.procgen's exact tier (Lambda_Pkg_Proc_Gen.md PG2, PG7.3).

A Python port of the keyed RNG and noise kernels, written from the same published
algorithms. Python floats are IEEE binary64, so every exact-tier value must agree
with Lambda bit for bit. Running this script recomputes the exact values printed by
rng.txt, noise.txt, sample.txt, grid.txt, lsystem.txt, text.txt, wfc.txt and terrain.txt and fails on any difference:

    python3 -I test/lambda/procgen/oracle.py
"""
import json, math, pathlib, sys

M = 2**64
G = 0x9e3779b97f4a7c15

def mix(z):
    z = ((z ^ (z >> 30)) * 0xbf58476d1ce4e5b9) % M
    z = ((z ^ (z >> 27)) * 0x94d049bb133111eb) % M
    return z ^ (z >> 31)

def key(seed): return mix((seed % M + G) % M)
def fnv(s):
    h = 0xcbf29ce484222325
    for c in s: h = ((h ^ ord(c)) * 0x100000001b3) % M
    return h
def part(p): return fnv(p) if isinstance(p, str) else p % M
def step(k, p, i): return mix(((k ^ part(p)) + G * (i + 1)) % M)
def derive(k, path):
    if isinstance(path, list):
        for i, p in enumerate(path): k = step(k, p, i)
        return k
    return step(k, path, 0)
def unit(k): return (mix(k) >> 11) * 1.1102230246251565e-16
def at(k, i): return step(k, i, 0)
def perm(k, n): return [x for _, x in sorted(((unit(at(k, i)), i) for i in range(n)))]
def table(k):
    p = perm(k, 256); return p + p

def fade(t): return t * t * t * (t * (t * 6.0 - 15.0) + 10.0)
def lerp(a, b, t): return a + t * (b - a)
def lat(v): return math.floor(v)
def hv2(p, x, y): return p[p[x] + y] / 127.5 - 1.0
def hv3(p, x, y, z): return p[p[p[x] + y] + z] / 127.5 - 1.0

def value2(p, x, y):
    xi, yi = lat(x), lat(y); a, b = xi & 255, yi & 255
    u, v = fade(x - xi), fade(y - yi)
    return lerp(lerp(hv2(p, a, b), hv2(p, a + 1, b), u), lerp(hv2(p, a, b + 1), hv2(p, a + 1, b + 1), u), v)

def value3(p, x, y, z):
    xi, yi, zi = lat(x), lat(y), lat(z); a, b, c = xi & 255, yi & 255, zi & 255
    u, v, w = fade(x - xi), fade(y - yi), fade(z - zi)
    return lerp(lerp(lerp(hv3(p, a, b, c), hv3(p, a + 1, b, c), u), lerp(hv3(p, a, b + 1, c), hv3(p, a + 1, b + 1, c), u), v),
                lerp(lerp(hv3(p, a, b, c + 1), hv3(p, a + 1, b, c + 1), u), lerp(hv3(p, a, b + 1, c + 1), hv3(p, a + 1, b + 1, c + 1), u), v), w)

S = 0.7071067811865476
def grad2(h, x, y):
    g = h & 7
    return [x, -x, y, -y, (x + y) * S, (y - x) * S, (x - y) * S, -(x + y) * S][g]

def grad3(h, x, y, z):
    g = h & 15
    u = x if g < 8 else y
    v = y if g < 4 else (x if g in (12, 14) else z)
    return (u if g & 1 == 0 else -u) + (v if g & 2 == 0 else -v)

def perlin2(p, x, y):
    xi, yi = lat(x), lat(y); a, b = xi & 255, yi & 255
    xf, yf = x - xi, y - yi; u, v = fade(xf), fade(yf)
    return 1.4142135623730951 * lerp(lerp(grad2(p[p[a] + b], xf, yf), grad2(p[p[a + 1] + b], xf - 1.0, yf), u),
                                     lerp(grad2(p[p[a] + b + 1], xf, yf - 1.0), grad2(p[p[a + 1] + b + 1], xf - 1.0, yf - 1.0), u), v)

def perlin3(p, x, y, z):
    xi, yi, zi = lat(x), lat(y), lat(z)
    xf, yf, zf = x - xi, y - yi, z - zi
    u, v, w = fade(xf), fade(yf), fade(zf)
    A = p[xi & 255] + (yi & 255); AA = p[A] + (zi & 255); AB = p[A + 1] + (zi & 255)
    B = p[(xi & 255) + 1] + (yi & 255); BA = p[B] + (zi & 255); BB = p[B + 1] + (zi & 255)
    return lerp(lerp(lerp(grad3(p[AA], xf, yf, zf), grad3(p[BA], xf - 1.0, yf, zf), u),
                     lerp(grad3(p[AB], xf, yf - 1.0, zf), grad3(p[BB], xf - 1.0, yf - 1.0, zf), u), v),
                lerp(lerp(grad3(p[AA + 1], xf, yf, zf - 1.0), grad3(p[BA + 1], xf - 1.0, yf, zf - 1.0), u),
                     lerp(grad3(p[AB + 1], xf, yf - 1.0, zf - 1.0), grad3(p[BB + 1], xf - 1.0, yf - 1.0, zf - 1.0), u), v), w)

SG = [[1, 1, 0], [-1, 1, 0], [1, -1, 0], [-1, -1, 0], [1, 0, 1], [-1, 0, 1], [1, 0, -1], [-1, 0, -1],
      [0, 1, 1], [0, -1, 1], [0, 1, -1], [0, -1, -1]]
def corner2(g, x, y):
    t = 0.5 - x * x - y * y
    if t < 0.0: return 0.0
    d = SG[g % 12]; return t * t * t * t * (d[0] * x + d[1] * y)
def corner3(g, x, y, z):
    t = 0.6 - x * x - y * y - z * z
    if t < 0.0: return 0.0
    d = SG[g % 12]; return t * t * t * t * (d[0] * x + d[1] * y + d[2] * z)

G2 = 0.21132486540518713
def simplex2(p, x, y):
    s = (x + y) * 0.3660254037844386
    i, j = lat(x + s), lat(y + s)
    t = float(i + j) * G2
    x0, y0 = x - (i - t), y - (j - t)
    i1 = 1 if x0 > y0 else 0; j1 = 1 - i1
    ii, jj = i & 255, j & 255
    return 70.0 * (corner2(p[ii + p[jj]], x0, y0) +
                   corner2(p[ii + i1 + p[jj + j1]], x0 - i1 + G2, y0 - j1 + G2) +
                   corner2(p[ii + 1 + p[jj + 1]], x0 - 1.0 + 2.0 * G2, y0 - 1.0 + 2.0 * G2))

def order3(x, y, z):
    if x >= y:
        return [1, 0, 0, 1, 1, 0] if y >= z else ([1, 0, 0, 1, 0, 1] if x >= z else [0, 0, 1, 1, 0, 1])
    return [0, 0, 1, 0, 1, 1] if y < z else ([0, 1, 0, 0, 1, 1] if x < z else [0, 1, 0, 1, 1, 0])

def simplex3(p, x, y, z):
    s = (x + y + z) * 0.3333333333333333
    i, j, k = lat(x + s), lat(y + s), lat(z + s)
    t = float(i + j + k) * 0.16666666666666666
    x0, y0, z0 = x - (i - t), y - (j - t), z - (k - t)
    o = order3(x0, y0, z0)
    ii, jj, kk = i & 255, j & 255, k & 255
    g1 = 0.16666666666666666; g2 = 2.0 * g1; g3 = 3.0 * g1
    return 32.0 * (corner3(p[ii + p[jj + p[kk]]], x0, y0, z0) +
                   corner3(p[ii + o[0] + p[jj + o[1] + p[kk + o[2]]]], x0 - o[0] + g1, y0 - o[1] + g1, z0 - o[2] + g1) +
                   corner3(p[ii + o[3] + p[jj + o[4] + p[kk + o[5]]]], x0 - o[3] + g2, y0 - o[4] + g2, z0 - o[5] + g2) +
                   corner3(p[ii + 1 + p[jj + 1 + p[kk + 1]]], x0 - 1.0 + g3, y0 - 1.0 + g3, z0 - 1.0 + g3))

def jit(p, h, off, j): return 0.5 + ((p[h + off] + 0.5) / 256.0 - 0.5) * j
def cellular2j(p, x, y, j=1.0, metric='euclidean'):
    xi, yi = lat(x), lat(y); ds = []
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):
            cx, cy = xi + dx, yi + dy; h = p[p[cx & 255] + (cy & 255)]
            ex = cx + jit(p, h, 0, j) - x; ey = cy + jit(p, h, 101, j) - y
            ds.append(dist(metric, ex, ey, 0.0))
    ds.sort(); return ds[:2]
def cellular3j(p, x, y, z, j=1.0, metric='euclidean'):
    xi, yi, zi = lat(x), lat(y), lat(z); ds = []
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):
            for dz in (-1, 0, 1):
                cx, cy, cz = xi + dx, yi + dy, zi + dz; h = p[p[p[cx & 255] + (cy & 255)] + (cz & 255)]
                ds.append(dist(metric, cx + jit(p, h, 0, j) - x, cy + jit(p, h, 101, j) - y, cz + jit(p, h, 211, j) - z))
    ds.sort(); return ds[:2]
def dist(m, dx, dy, dz):
    if m == 'manhattan': return abs(dx) + abs(dy) + abs(dz)
    if m == 'chebyshev': return max(abs(dx), max(abs(dy), abs(dz)))
    return math.sqrt(dx * dx + dy * dy + dz * dz)
def cellular2(p, x, y): return cellular2j(p, x, y)
def cellular3(p, x, y, z): return cellular3j(p, x, y, z)
def white2(p, x, y): return hv2(p, lat(x) & 255, lat(y) & 255)
def white3(p, x, y, z): return hv3(p, lat(x) & 255, lat(y) & 255, lat(z) & 255)
def running(first, factor, n):
    out = [first]
    for _ in range(n - 1): out.append(out[-1] * factor)
    return out
def make(k, spec):
    kind = spec.get('kind', 'simplex')
    fractal = spec.get('fractal', 'none' if 'octaves' not in spec else 'fbm')
    octaves = 1 if fractal == 'none' else spec.get('octaves', 4)
    amps = running(1.0, spec.get('gain', 0.5), octaves)
    total = 0.0
    for a in amps: total += a
    w = spec.get('warp')
    return dict(kind=kind, fractal=fractal, octaves=octaves, freqs=running(spec.get('frequency', 0.03125), spec.get('lacunarity', 2.0), octaves),
                amps=amps, total=total, jitter=spec.get('jitter', 1.0), metric=spec.get('metric', 'euclidean'), cell=spec.get('cell', 'f1'),
                tables=[table(derive(k, ['octave', o])) for o in range(octaves)],
                warp=None if w is None else dict(amp=w['amplitude'], freq=w['frequency'],
                     tables=[table(derive(k, 'warp-x')), table(derive(k, 'warp-y')), table(derive(k, 'warp-z'))]))
def cellv(g, pr): return pr[1] if g['cell'] == 'f2' else (pr[1] - pr[0] if g['cell'] == 'f2-f1' else pr[0])
def base2(g, p, x, y):
    k = g['kind']
    if k == 'simplex': return simplex2(p, x, y)
    if k == 'perlin': return perlin2(p, x, y)
    if k == 'value': return value2(p, x, y)
    if k == 'white': return white2(p, x, y)
    return cellv(g, cellular2j(p, x, y, g['jitter'], g['metric']))
def base3(g, p, x, y, z):
    k = g['kind']
    if k == 'simplex': return simplex3(p, x, y, z)
    if k == 'perlin': return perlin3(p, x, y, z)
    if k == 'value': return value3(p, x, y, z)
    if k == 'white': return white3(p, x, y, z)
    return cellv(g, cellular3j(p, x, y, z, g['jitter'], g['metric']))
def fold(fr, n): return 1.0 - 2.0 * abs(n) if fr == 'ridged' else (2.0 * abs(n) - 1.0 if fr == 'billow' else n)
def oct2(g, x, y):
    if g['fractal'] == 'none': f = g['freqs'][0]; return base2(g, g['tables'][0], x * f, y * f)
    s = 0.0
    for o in range(g['octaves']):
        f = g['freqs'][o]; s += g['amps'][o] * fold(g['fractal'], base2(g, g['tables'][o], x * f, y * f))
    return s / g['total']
def oct3(g, x, y, z):
    if g['fractal'] == 'none': f = g['freqs'][0]; return base3(g, g['tables'][0], x * f, y * f, z * f)
    s = 0.0
    for o in range(g['octaves']):
        f = g['freqs'][o]; s += g['amps'][o] * fold(g['fractal'], base3(g, g['tables'][o], x * f, y * f, z * f))
    return s / g['total']
def sample(g, x, y):
    w = g['warp']
    if w is None: return oct2(g, x, y)
    wx, wy = x * w['freq'], y * w['freq']
    return oct2(g, x + w['amp'] * simplex2(w['tables'][0], wx, wy), y + w['amp'] * simplex2(w['tables'][1], wx, wy))
def sample3(g, x, y, z):
    w = g['warp']
    if w is None: return oct3(g, x, y, z)
    wx, wy, wz = x * w['freq'], y * w['freq'], z * w['freq']
    return oct3(g, x + w['amp'] * simplex3(w['tables'][0], wx, wy, wz), y + w['amp'] * simplex3(w['tables'][1], wx, wy, wz),
                z + w['amp'] * simplex3(w['tables'][2], wx, wy, wz))


# --- procgen.sample ports (same operation order as sample.ls) ---
def frac(x): return x - math.floor(x)

def poisson(k, width, height, r, attempts=20):
    s = r * 0.7071067811865476
    nx, ny = math.ceil(width / s), math.ceil(height / s)
    r2 = r * r
    grid = [None] * (nx * ny)
    def conflicts(g, ci, cj, x, y):
        for dj in range(-2, 3):
            for di in range(-2, 3):
                i, j = ci + di, cj + dj
                if (abs(di) == 2 and abs(dj) == 2) or i < 0 or j < 0 or i >= nx or j >= ny: continue
                q = g[j * nx + i]
                if q is not None and (q[0] - x) * (q[0] - x) + (q[1] - y) * (q[1] - y) < r2: return True
        return False
    for step in range(attempts * 9):
        kr, ph = at(k, step // 9), step % 9
        new = []
        for c, q in enumerate(grid):
            if q is not None or (c % nx) % 3 + 3 * ((c // nx) % 3) != ph: new.append(q); continue
            ci, cj = c % nx, c // nx
            x = (ci + unit(at(kr, 2 * c))) * s
            y = (cj + unit(at(kr, 2 * c + 1))) * s
            new.append(None if x >= width or y >= height or conflicts(grid, ci, cj, x, y) else [x, y])
        grid = new
    return [q for q in grid if q is not None]

def jitter(k, width, height, size, amount=1.0):
    nx, ny = math.ceil(width / size), math.ceil(height / size); out = []
    for j in range(ny):
        for i in range(nx):
            c = j * nx + i
            x = (i + 0.5 + (unit(at(k, 2 * c)) - 0.5) * amount) * size
            y = (j + 0.5 + (unit(at(k, 2 * c + 1)) - 0.5) * amount) * size
            if x < width and y < height: out.append([x, y])
    return out

def radical_inverse(i, base):
    num, den = 0, 1
    while i: num, den, i = num * base + i % base, den * base, i // base
    return num / den

def halton(k, n, width, height):
    u, v = unit(at(k, 0)), unit(at(k, 1))
    return [[frac(radical_inverse(i, 2) + u) * width, frac(radical_inverse(i, 3) + v) * height] for i in range(1, n + 1)]

def r2seq(k, n, width, height):
    u, v = unit(at(k, 0)), unit(at(k, 1))
    return [[frac(u + i * 0.7548776662466927) * width, frac(v + i * 0.5698402909980532) * height] for i in range(1, n + 1)]

def best_candidate(k, width, height, count, m=10):
    pts = []
    for i in range(count):
        tries = []
        for t in range(m):
            kt = at(at(k, i), t)
            x, y = unit(at(kt, 0)) * width, unit(at(kt, 1)) * height
            near = min(((q[0] - x) * (q[0] - x) + (q[1] - y) * (q[1] - y) for q in pts), default=1e300)
            tries.append([near, x, y])
        far = max(c[0] for c in tries)
        pick = [c for c in tries if c[0] == far][0]
        pts.append([pick[1], pick[2]])
    return pts

def weighted(k, field, n):
    w = len(field[0]); values = [float(v) for row in field for v in row]
    cum, acc = [], 0.0
    for v in values: acc += v; cum.append(acc)
    total = cum[-1]; out = []
    for i in range(n):
        target = unit(at(k, 3 * i)) * total
        lo, hi = 0, len(values) - 1
        while lo < hi:
            mid = (lo + hi) // 2
            if cum[mid] > target: hi = mid
            else: lo = mid + 1
        out.append([lo % w + unit(at(k, 3 * i + 1)), lo // w + unit(at(k, 3 * i + 2))])
    return out


# --- procgen.grid ports (same operation order as grid.ls) ---
def show(flat, w):
    return ["".join("#" if v == 1 else "." for v in flat[y * w:(y + 1) * w]) for y in range(len(flat) // w)]

def random_fill(k, w, h, fill=0.45):
    return [1 if unit(at(k, c)) < fill else 0 for c in range(w * h)]

def evolve(flat, w, h, births=(5, 6, 7, 8), survives=(4, 5, 6, 7, 8), steps=5, edge=1):
    for _ in range(steps):
        nxt = []
        for c, v in enumerate(flat):
            x, y, n = c % w, c // w, 0
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    if dx == 0 and dy == 0: continue
                    nx, ny = x + dx, y + dy
                    n += edge if nx < 0 or ny < 0 or nx >= w or ny >= h else flat[ny * w + nx]
            nxt.append((1 if n in survives else 0) if v == 1 else (1 if n in births else 0))
        flat = nxt
    return flat

def fold_into(u, n):
    m = u % (2 * n)
    return m if m < n else 2 * n - 1 - m

def walk(k, w, h, density=0.4):
    iw, ih = w - 2, h - 2
    n = 20 * iw * ih
    dirs = [int(unit(at(k, i)) * 4) for i in range(n)]
    xs, ys = [iw // 2], [ih // 2]
    for d in dirs:
        xs.append(xs[-1] + (1 if d == 0 else -1 if d == 1 else 0))
        ys.append(ys[-1] + (1 if d == 2 else -1 if d == 3 else 0))
    first = {}
    for i in range(n + 1):
        cell = (fold_into(ys[i], ih) + 1) * w + fold_into(xs[i], iw) + 1
        first.setdefault(cell, i)
    order = sorted([i, cell] for cell, i in first.items())
    carved = {cell for _, cell in order[:math.ceil(density * float(iw * ih))]}
    return [0 if c in carved else 1 for c in range(w * h)]


# --- procgen.lsystem and procgen.text ports ---
def rng_weighted(k, items, weights):
    cum, acc = [], 0.0
    for w in weights: acc += w; cum.append(acc)
    target = unit(k) * float(cum[-1])
    return items[[i for i, c in enumerate(cum) if c > target][0]]

def lsystem_expand(axiom, rules, order, k):
    s = axiom
    for g in range(order):
        out = []
        for i, ch in enumerate(s):
            body = rules.get(ch)
            if body is None: out.append(ch)
            elif isinstance(body, list): out.append(rng_weighted(at(at(k, g), i), [b for _, b in body], [w for w, _ in body]))
            else: out.append(body)
        s = "".join(out)
    return s

START, STOP = chr(2), chr(3)

def markov(samples, order):
    table = {}
    for s in samples:
        seq = [START] * order + list(s) + [STOP]
        for i in range(order, len(seq)):
            ctx = "".join(seq[i - order:i]); table.setdefault(ctx, {}).setdefault(seq[i], 0); table[ctx][seq[i]] += 1
    return {ctx: sorted(nexts.items()) for ctx, nexts in table.items()}

def markov_name(k, chain, order, samples, lo=3, hi=12, attempts=100, novel=True):
    for a in range(attempts):
        ka, ctx, out = at(k, a), START * order, ""
        for i in range(hi + 2):
            opts = chain[ctx]
            nxt = rng_weighted(at(ka, i), [c for c, _ in opts], [n for _, n in opts])
            if nxt == STOP: break
            ctx = (ctx + nxt)[1:order + 1]; out += nxt
        if lo <= len(out) <= hi and not (novel and out in samples): return out
    return None


# --- procgen.wfc port: same models and observation order; propagation is plain AC-3, whose
# fixpoint (the maximal arc-consistent domains) is unique, so it must equal the swept counts ---
DX, DY = [1, 0, -1, 0], [0, 1, 0, -1]

def wfc_tiled(tiles, adjacency, weights=None):
    idx = {str(t): i for i, t in enumerate(tiles)}; t = len(tiles)
    masks = [[[0] * t for _ in range(t)] for _ in range(4)]
    for a, b, axis in adjacency:
        d = 0 if axis == "x" else 1
        masks[d][idx[str(a)]][idx[str(b)]] = 1; masks[(d + 2) % 4][idx[str(b)]][idx[str(a)]] = 1
    return dict(values=list(tiles), weights=weights or [1] * t, masks=masks, count=t)

def wfc_from_sample(sample):
    h, w = len(sample), len(sample[0])
    tiles = list(dict.fromkeys(v for row in sample for v in row)); idx = {v: i for i, v in enumerate(tiles)}; t = len(tiles)
    masks = [[[0] * t for _ in range(t)] for _ in range(4)]
    for y in range(h):
        for x in range(w):
            for d in range(4):
                nx, ny = x + DX[d], y + DY[d]
                if 0 <= nx < w and 0 <= ny < h: masks[d][idx[sample[y][x]]][idx[sample[ny][nx]]] = 1
    return dict(values=tiles, weights=[sum(v == tl for row in sample for v in row) for tl in tiles], masks=masks, count=t)

def wfc_overlapping(sample, n, symmetry):
    h, w = len(sample), len(sample[0])
    colors = list(dict.fromkeys(v for row in sample for v in row)); cid = {c: i for i, c in enumerate(colors)}
    rot = lambda p: [p[(n - 1 - x) * n + y] for y in range(n) for x in range(n)]
    ref = lambda p: [p[y * n + n - 1 - x] for y in range(n) for x in range(n)]
    order, counts = [], {}
    for y in range(h):
        for x in range(w):
            p = [cid[sample[(y + j) % h][(x + i) % w]] for j in range(n) for i in range(n)]
            r1 = rot(p); r2 = rot(r1); r3 = rot(r2)
            for v in [p, ref(p), r1, ref(r1), r2, ref(r2), r3, ref(r3)][:symmetry]:
                key = tuple(v)
                if key not in counts: order.append(key); counts[key] = 0
                counts[key] += 1
    def agrees(p, q, ox, oy):
        return all(p[y * n + x] == q[(y - oy) * n + x - ox] for y in range(max(0, oy), min(n, n + oy)) for x in range(max(0, ox), min(n, n + ox)))
    masks = [[[1 if agrees(p, q, DX[d], DY[d]) else 0 for q in order] for p in order] for d in range(4)]
    return dict(values=[colors[p[0]] for p in order], weights=[counts[p] for p in order], masks=masks, count=len(order))

def wfc_neighbour(c, d, w, h, wrap):
    x, y = c % w + DX[d], c // w + DY[d]
    if wrap: return ((y + h) % h) * w + (x + w) % w
    return -1 if x < 0 or y < 0 or x >= w or y >= h else y * w + x

def wfc_settle(model, dom, w, h, wrap):
    changed = True
    while changed:
        changed = False
        for c in range(w * h):
            keep = [p for p in dom[c] if all(any(model["masks"][(d + 2) % 4][q][p] for q in dom[m])
                    for d in range(4) for m in [wfc_neighbour(c, d, w, h, wrap)] if m >= 0)]
            if len(keep) < len(dom[c]):
                if not keep: return None
                dom[c] = keep; changed = True
    return dom

def wfc_run(k, model, w, h, wrap=False, retries=10):
    for a in range(retries):
        ka = at(k, a)
        dom = wfc_settle(model, [list(range(model["count"])) for _ in range(w * h)], w, h, wrap)
        i = 0
        while dom is not None:
            pending = [c for c in range(w * h) if len(dom[c]) > 1]
            if not pending: break
            ks = at(ka, i); i += 1
            least = min(len(dom[c]) for c in pending)
            ties = [c for c in pending if len(dom[c]) == least]
            c = ties[int(unit(at(ks, 0)) * len(ties))]
            dom[c] = [rng_weighted(at(ks, 1), dom[c], [model["weights"][q] for q in dom[c]])]
            dom = wfc_settle(model, dom, w, h, wrap)
        if dom is not None: return [[dom[y * w + x][0] for x in range(w)] for y in range(h)]
    return None

def wfc_show(model, cells):
    return ["".join(str(model["values"][p]) for p in row) for row in cells]


# --- procgen.terrain ports (same operation order as terrain.ls) ---
def heightmap(k, w, h, island=0.0):
    scale = float(min(w, h))
    g = make(derive(k, "height"), dict(kind="simplex", octaves=6, frequency=2.5 / scale,
                                       warp=dict(amplitude=scale * 0.06, frequency=1.2 / scale)))
    vals = [[sample(g, float(x), float(y)) for x in range(w)] for y in range(h)]
    lo = min(min(r) for r in vals); hi = max(max(r) for r in vals)
    base = [[(v - lo) / (hi - lo) for v in r] for r in vals]
    if island <= 0.0: return base
    cx, cy = float(w - 1) / 2.0, float(h - 1) / 2.0
    def dome(x, y):
        dx = (float(x) - cx) / cx; dy = (float(y) - cy) / cy
        r = math.sqrt(dx * dx + dy * dy); return 1.0 - r * r
    return [[max(0.0, min(1.0, base[y][x] * (1.0 - island) + dome(x, y) * island)) for x in range(w)] for y in range(h)]

def thermal_step(vals, w, h, talus, rate):
    out = []
    for c, v in enumerate(vals):
        x, y = c % w, c // w
        drops = [v - vals[c + 1] if x < w - 1 else 0.0, v - vals[c + w] if y < h - 1 else 0.0,
                 v - vals[c - 1] if x > 0 else 0.0, v - vals[c - w] if y > 0 else 0.0]
        excess = [max(0.0, d - talus) for d in drops]
        total = 0.0
        for e in excess: total += e
        if total <= 0.0: out.append([0.0] * 4)
        else:
            moved = rate * max(excess); out.append([moved * e / total for e in excess])
    nxt = []
    for c, v in enumerate(vals):
        x, y = c % w, c // w
        gone = 0.0
        for e in out[c]: gone += e
        nxt.append(v - gone + (out[c - 1][0] if x > 0 else 0.0) + (out[c - w][1] if y > 0 else 0.0) +
                   (out[c + 1][2] if x < w - 1 else 0.0) + (out[c + w][3] if y < h - 1 else 0.0))
    return nxt

def thermal(f, iterations, talus, rate=0.5):
    h, w = len(f), len(f[0]); vals = [float(v) for r in f for v in r]
    for _ in range(iterations): vals = thermal_step(vals, w, h, talus, rate)
    return [vals[y * w:(y + 1) * w] for y in range(h)]

def fill(f, sea=-1.0, eps=1e-6):
    h, w = len(f), len(f[0]); vals = [float(v) for r in f for v in r]
    out = lambda c, v: c % w == 0 or c % w == w - 1 or c // w == 0 or c // w == h - 1 or v <= sea
    water = [v if out(c, v) else 1e300 for c, v in enumerate(vals)]
    while True:
        nxt = []
        for c, cur in enumerate(water):
            if cur == vals[c]: nxt.append(cur); continue
            x, y = c % w, c // w
            near = [water[ny * w + nx] for dy in (-1, 0, 1) for dx in (-1, 0, 1)
                    for nx, ny in [(x + dx, y + dy)] if (dx or dy) and 0 <= nx < w and 0 <= ny < h]
            nxt.append(max(vals[c], min(cur, min(near) + eps)))
        if nxt == water: break
        water = nxt
    return [water[y * w:(y + 1) * w] for y in range(h)]


def lambda_line(text):
    # Lambda prints symbols single-quoted; everything else on these lines is JSON.
    return json.loads(text.replace("'", '"'))


def check(label, got, want):
    if got != want:
        print(f"MISMATCH {label}:\n  golden {got}\n  oracle {want}")
        return 1
    print(f"ok {label}")
    return 0


def main():
    here = pathlib.Path(__file__).resolve().parent
    rng_lines = (here / "rng.txt").read_text().splitlines()
    noise_lines = (here / "noise.txt").read_text().splitlines()
    k = key(42)
    bad = check("rng keys", lambda_line(rng_lines[1]),
                [k, mix((fnv("world") + G) % M), derive(k, "trees"), derive(k, ["chunk", 12, -3]), derive(k, "1.5")])
    bad += check("rng draws", lambda_line(rng_lines[6])[:3],
                 [unit(k), int(unit(k) * 6), -1.0 + unit(k) * (1.0 - -1.0)])
    bad += check("rng units", lambda_line(rng_lines[7]), [unit(at(k, i)) for i in range(4)])
    bad += check("rng perm", lambda_line(rng_lines[8]), perm(k, 8))
    p = table(key(1))
    pts = [[0.3, 0.7, 1.1], [-5.25, 3.5, 0.125], [100.9, -42.1, 7.7], [255.5, 256.5, -0.5]]
    bad += check("noise kernels", lambda_line(noise_lines[1]),
                 [[value2(p, q[0], q[1]), perlin2(p, q[0], q[1]), simplex2(p, q[0], q[1]), value3(p, *q), perlin3(p, *q),
                   simplex3(p, *q), *cellular2(p, q[0], q[1]), *cellular3(p, *q)] for q in pts])
    bad += check("cellular metrics", lambda_line(noise_lines[7]),
                 [cellular2j(p, 0.3, 0.7, 0.5, m) for m in ["manhattan", "chebyshev"]])
    k2 = key(2)
    bad += check("generator kinds", lambda_line(noise_lines[11]),
                 [[kd, sample(make(k2, dict(kind=kd, frequency=0.1)), 3.0, 4.0), sample3(make(k2, dict(kind=kd, frequency=0.1)), 3.0, 4.0, 5.0)]
                  for kd in ["simplex", "perlin", "value", "white", "cellular"]])
    bad += check("cell variants", lambda_line(noise_lines[12]),
                 [sample(make(k2, dict(kind="cellular", cell=c, frequency=0.1)), 3.0, 4.0) for c in ["f1", "f2", "f2-f1"]])
    spec = dict(kind="simplex", octaves=4, frequency=0.05)
    bad += check("warped sample3", lambda_line(noise_lines[13])[1],
                 sample3(make(k2, dict(spec, warp=dict(amplitude=6.0, frequency=0.03))), 3.0, 4.0, 5.0))
    sample_lines = (here / "sample.txt").read_text().splitlines()
    k7 = key(7)
    bad += check("poisson", lambda_line(sample_lines[1]), poisson(k7, 30.0, 20.0, 6.0))
    at7 = sample_lines.index('"7 coverage certificates preserve keyed samples"')
    bad += check("poisson coverage", lambda_line(sample_lines[at7 + 1]),
                 [poisson(key(seed), w, h, r, attempts) for seed in [0, 17]
                  for w, h, r in [(4.5, 3.0, 1.0), (0.3, 7.0, 1.0), (6.0, 4.0, 0.75)] for attempts in [1, 20]])
    bad += check("jitter", lambda_line(sample_lines[2]), jitter(k7, 3.0, 2.0, 1.0, 0.5))
    bad += check("halton", lambda_line(sample_lines[3]), halton(k7, 3, 1.0, 1.0))
    bad += check("r2", lambda_line(sample_lines[4]), r2seq(k7, 3, 1.0, 1.0))
    bad += check("best candidate", lambda_line(sample_lines[5]), best_candidate(k7, 10.0, 10.0, 4))
    bad += check("weighted", lambda_line(sample_lines[6]), weighted(k7, [[0.0, 1.0], [0.0, 3.0]], 4))
    bad += check("radical inverse", lambda_line(sample_lines[8]) + lambda_line(sample_lines[9]),
                 [radical_inverse(i, 2) for i in range(1, 9)] + [radical_inverse(i, 3) for i in range(1, 6)])
    grid_lines = (here / "grid.txt").read_text().splitlines()
    k5 = key(5)
    bad += check("random fill", lambda_line(grid_lines[1]), show(random_fill(k5, 12, 4, 0.5), 12))
    bad += check("cave automaton", lambda_line(grid_lines[2]), show(evolve(random_fill(k5, 24, 8), 24, 8), 24))
    bad += check("walk", lambda_line(grid_lines[3]), show(walk(k5, 16, 7, 0.5), 16))
    lsys_lines = (here / "lsystem.txt").read_text().splitlines()
    bad += check("stochastic l-system", lambda_line(lsys_lines[6]), [lsystem_expand("F", {"F": [[1, "F[+F]"], [2, "F[-F]F"]]}, 2, key(1))])
    text_lines = (here / "text.txt").read_text().splitlines()
    at6 = text_lines.index('"6 Markov names"')
    samples = ["aldric", "beatrix", "cedric", "dorian", "elowen", "fenwick", "gareth", "helena", "isolde", "jareth", "kestrel", "lorien"]
    chain = markov(samples, 2)
    bad += check("markov names", lambda_line(text_lines[at6 + 2]), [markov_name(at(key(8), i), chain, 2, samples) for i in range(8)])
    bad += check("markov bounded name", lambda_line(text_lines[at6 + 3])[1], markov_name(key(9), chain, 2, samples, 5, 6))
    wfc_lines = (here / "wfc.txt").read_text().splitlines()
    at2 = wfc_lines.index('"2 exact grids"')
    checker = wfc_from_sample([[0, 1], [1, 0]])
    coast = wfc_tiled(["~", "c", "#"], [["~", "~", "x"], ["~", "~", "y"], ["~", "c", "x"], ["c", "~", "x"], ["~", "c", "y"], ["c", "~", "y"],
        ["c", "c", "x"], ["c", "c", "y"], ["c", "#", "x"], ["#", "c", "x"], ["c", "#", "y"], ["#", "c", "y"], ["#", "#", "x"], ["#", "#", "y"]], [4, 1, 3])
    stripes = [[0, 0, 1, 0, 0, 1], [0, 0, 1, 0, 0, 1], [1, 1, 1, 1, 1, 1], [0, 0, 1, 0, 0, 1], [0, 0, 1, 0, 0, 1], [1, 1, 1, 1, 1, 1]]
    ov = wfc_overlapping(stripes, 3, 1)
    bad += check("wfc checkerboard", lambda_line(wfc_lines[at2 + 1]), wfc_show(checker, wfc_run(key(1), checker, 8, 4)))
    bad += check("wfc coast", lambda_line(wfc_lines[at2 + 2]), wfc_show(coast, wfc_run(key(2), coast, 16, 6)))
    bad += check("wfc overlapping", lambda_line(wfc_lines[at2 + 3]), wfc_show(ov, wfc_run(key(3), ov, 12, 6, True)))
    bad += check("wfc indices", lambda_line(wfc_lines[at2 + 4]), wfc_run(key(3), ov, 6, 3, True))
    terrain_lines = (here / "terrain.txt").read_text().splitlines()
    small = heightmap(key(1), 6, 4, 0.5)
    bad += check("heightmap", lambda_line(terrain_lines[1]), small)
    bad += check("thermal", lambda_line(terrain_lines[2]), thermal(small, 2, 0.05))
    bad += check("fill", lambda_line(terrain_lines[3]), fill([[3.0, 3.0, 3.0, 3.0], [3.0, 0.0, 1.0, 3.0], [3.0, 2.0, 0.5, 2.5], [3.0, 3.0, 3.0, 3.0]], eps=0.25))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
