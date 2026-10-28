// Terrain (Lambda_Pkg_Proc_Art.md §4 procart.terrain): heightmap recipes, thermal and
// hydraulic erosion, depression filling, flow and rivers, moisture and biomes. Heights
// are float[][] fields in [0, 1] (PG5). Only basic arithmetic and sqrt are used, so all
// of it is exact-tier (PG2).
import rng: .rng
import noise: .noise
import field: .field
import grid: .grid
import core: ._field
import contour: lambda.chart.mesh_contour

fn flat(f) array => [for (row in f) for (v in row) float(v)]
fn rows(values, w: int, h: int) array => [for (y in 0 to (h - 1)) [for (x in 0 to (w - 1)) values[y * w + x]]]

// one element replaced by native slices, for the sequential flow fold
fn set_at(values, i: int, v) array => slice(values, 0, i) ++ [v] ++ slice(values, i + 1, len(values))

// --- heightmaps ---
// distance from the centre scaled per axis: 1.0 at the middle of every edge, so an island fills a rectangle.
fn oval(w: int, h: int) array {
    let cx = float(w - 1) / 2.0
    let cy = float(h - 1) / 2.0;
    field.make(w, h, (x, y) => (let dx = (float(x) - cx) / cx, let dy = (float(y) - cy) / cy, math.sqrt(dx * dx + dy * dy)))
}

// fBm simplex noise in [0, 1], optionally warped, blended toward an island dome by `island` (0 to 1).
pub fn heightmap(k: u64, options) array^ {
    let w = options.width
    let h = options.height;
    if (not (w is int) or not (h is int) or w < 2 or h < 2 or w * h > 250000)
        raise error("procart.terrain.heightmap: width and height must be ints of at least 2, at most 250,000 cells")
    else {
        let scale = float(min(w, h))
        let spec = {kind: 'simplex', octaves: if (options.octaves == null) 6 else options.octaves,
            frequency: if (options.frequency == null) 2.5 / scale else options.frequency,
            warp: if (options.warp == false) null else {amplitude: scale * 0.06, frequency: 1.2 / scale}}
        let base = field.normalize(noise.field(noise.make(rng.derive(k, "height"), spec)^, w, h))
        let island: float = if (options.island == null) 0.0 else float(options.island);
        if (island <= 0.0) base
        // blend toward a dome, 1 - r^2, and clamp: renormalizing would lift the coast back up (Red Blob Games)
        else field.clamp(base * (1.0 - island) + field.transform(oval(w, h), (r) => 1.0 - r * r) * island, 0.0, 1.0)
    }
}

// --- thermal erosion: material slides off slopes steeper than `talus` ---
// Each cell moves rate x (largest excess) to its lower 4-neighbours, in proportion to their
// excess slope (Olsen 2004). Mass is conserved; nothing leaves the map.
fn shares(values, w: int, h: int, talus: float, rate: float) array =>
    [for (c, v in values) (let x = c % w, let y = c div w,
        let drops = [if (x < w - 1) v - values[c + 1] else 0.0, if (y < h - 1) v - values[c + w] else 0.0,
                     if (x > 0) v - values[c - 1] else 0.0, if (y > 0) v - values[c - w] else 0.0],
        let excess = [for (d in drops) max(0.0, d - talus)],
        let total = sum(excess),
        if (total <= 0.0) [0.0, 0.0, 0.0, 0.0] else (let moved = rate * max(excess), [for (e in excess) moved * e / total]))]

fn thermal_step(values, w: int, h: int, talus: float, rate: float) array {
    let out = shares(values, w, h, talus, rate);
    [for (c, v in values) (let x = c % w, let y = c div w,
        v - sum(out[c]) + (if (x > 0) out[c - 1][0] else 0.0) + (if (y > 0) out[c - w][1] else 0.0) +
            (if (x < w - 1) out[c + 1][2] else 0.0) + (if (y < h - 1) out[c + w][3] else 0.0))]
}

pub fn thermal(height, options = {}) array^ {
    let h = len(height)
    let w = if (h == 0) 0 else len(height[0])
    let iterations = if (options.iterations == null) 10 else options.iterations
    let talus: float = if (options.talus == null) 4.0 / float(max(w, h)) else float(options.talus)
    let rate: float = if (options.rate == null) 0.5 else float(options.rate);
    if (not (iterations is int) or iterations < 0 or iterations > 1000) raise error("procart.terrain.thermal: iterations must be an int in 0..1000")
    else rows(reduce([flat(height), *[for (i in 1 to iterations) i]], (vs, i) => thermal_step(vs, w, h, talus, rate)), w, h)
}

// --- hydraulic erosion: droplets pick up sediment downhill and fall it uphill or when slowed ---
// After Beyer (2015). Droplets in a batch run on the same snapshot and their changes are summed
// and applied together: one sequential droplet at a time would update the whole map per step.
// Speed grows downhill (v^2 - dh * gravity); erosion and deposition use the 4 cell corners.
fn corners(values, w: int, x: float, y: float) array {
    let ix = int(floor(x))
    let iy = int(floor(y))
    let c = iy * w + ix;
    [c, x - float(ix), y - float(iy), values[c], values[c + 1], values[c + w], values[c + w + 1]]
}

fn surface(p) array {
    let u = p[1]
    let v = p[2];
    [p[3] * (1.0 - u) * (1.0 - v) + p[4] * u * (1.0 - v) + p[5] * (1.0 - u) * v + p[6] * u * v,
     (p[4] - p[3]) * (1.0 - v) + (p[6] - p[5]) * v, (p[5] - p[3]) * (1.0 - u) + (p[6] - p[4]) * u]
}

fn spread(p, w: int, amount: float) array {
    let u = p[1]
    let v = p[2];
    [[p[0], amount * (1.0 - u) * (1.0 - v)], [p[0] + 1, amount * u * (1.0 - v)],
     [p[0] + w, amount * (1.0 - u) * v], [p[0] + w + 1, amount * u * v]]
}

// one droplet's height changes: [cell, delta] pairs.
fn droplet(values, w: int, h: int, s, p, x: float, y: float, dx: float, dy: float, speed: float, water: float, sediment: float, life: int, out) array {
    let here = surface(corners(values, w, x, y))
    let ndx = dx * s.inertia - here[1] * (1.0 - s.inertia)
    let ndy = dy * s.inertia - here[2] * (1.0 - s.inertia)
    let length = math.sqrt(ndx * ndx + ndy * ndy);
    if (life <= 0 or length == 0.0) out
    else (let ux = ndx / length, let uy = ndy / length, let nx = x + ux, let ny = y + uy,
        if (nx < 0.0 or ny < 0.0 or nx >= float(w - 1) or ny >= float(h - 1)) out
        else (let fall = surface(corners(values, w, nx, ny))[0] - here[0], let pt = corners(values, w, x, y),
            let capacity = max(0.0 - fall * speed * water * s.capacity, s.min_capacity),
            let change = if (sediment > capacity or fall > 0.0)
                    (if (fall > 0.0) min(fall, sediment) else (sediment - capacity) * s.deposit)
                else 0.0 - min((capacity - sediment) * s.erode, 0.0 - fall),
            droplet(values, w, h, s, p, nx, ny, ux, uy, math.sqrt(max(0.0, speed * speed - fall * s.gravity)), water * (1.0 - s.evaporate),
                sediment - change, life - 1, out ++ spread(pt, w, change))))
}

fn apply_batch(values, w: int, h: int, s, k: u64, first: int, stop: int) array {
    let deltas = [for (i in first to (stop - 1), let kd = rng.at(k, i))
        for (d in droplet(values, w, h, s, null, rng.unit(rng.at(kd, 0)) * float(w - 1), rng.unit(rng.at(kd, 1)) * float(h - 1),
            0.0, 0.0, 1.0, 1.0, 0.0, s.steps, [])) d]
    let summed = map([for (d in deltas group by d[0] as cell into g) for (x in [string(cell), sum(g |> ~[1])]) x]);
    // Droplets in one batch all dig the same snapshot, so their summed changes can overshoot and
    // run away (pits feeding deeper pits). A batch may not create new extremes: each touched cell
    // stays within the range of its old 4-neighbourhood, so heights stay bounded.
    [for (c, v in values) (let d = summed[string(c)], if (d == null) v
        else (let near = [v, *[for (n in [c - 1, c + 1, c - w, c + w] where n >= 0 and n < len(values) and
                (n div w == c div w or n % w == c % w)) values[n]]], max(min(near), min(max(near), v + d))))]
}

fn hydraulic_with(k: u64, height, options, use_native: bool) array^ {
    let h = len(height)
    let w = if (h == 0) 0 else len(height[0])
    let n = if (options.droplets == null) w * h else options.droplets
    let batch = if (options.batch == null) 64 else options.batch
    let s = {inertia: 0.05, capacity: 4.0, min_capacity: 0.01, erode: 0.3, deposit: 0.3, evaporate: 0.02, gravity: 4.0, steps: 48, *: options};
    if (w < 2 or h < 2) raise error("procart.terrain.hydraulic: the height field must be at least 2 x 2")
    else if (w * h > 250000 or not all([for (row in height) row is array and len(row) == w]))
        raise error("procart.terrain.hydraulic: expected a rectangular field of at most 250,000 cells")
    else if (not (n is int) or n < 0 or n > 1000000) raise error("procart.terrain.hydraulic: droplets must be an int in 0..1,000,000")
    else if (not (batch is int) or batch < 1 or batch > 1000000) raise error("procart.terrain.hydraulic: batch must be an int in 1..1,000,000")
    else if (not (s.steps is int) or s.steps < 0 or s.steps > 1000) raise error("procart.terrain.hydraulic: steps must be an int in 0..1000")
    else {
        let params = [s.inertia, s.capacity, s.min_capacity, s.erode, s.deposit, s.evaporate, s.gravity]
        let values = flat(height);
        if (not all([for (p in params) p is number and float(p) - float(p) == 0.0]) or
            not all([for (v in values) v - v == 0.0])) raise error("procart.terrain.hydraulic: heights and settings must be finite")
        else if (s.inertia < 0 or s.inertia > 1 or s.capacity < 0 or s.min_capacity < 0 or
            s.erode < 0 or s.erode > 1 or s.deposit < 0 or s.deposit > 1 or
            s.evaporate < 0 or s.evaporate > 1 or s.gravity < 0) raise error("procart.terrain.hydraulic: invalid rate or capacity")
        else rows(if (use_native) math.hydraulic(k, values, [w, h, n, batch, s.steps], params)^
            else rain(values, w, h, s, k, n, batch), w, h)
    }
}

// PG7.3: the original snapshot-batched implementation remains an explicit oracle.
pub fn hydraulic(k: u64, height, options = {}) array^ => hydraulic_with(k, height, options, math.hydraulic is fn)^
pub fn hydraulic_reference(k: u64, height, options = {}) array^ => hydraulic_with(k, height, options, false)^

fn rain(values, w: int, h: int, s, k: u64, n: int, batch: int) array =>
    reduce([values, *[for (b in 0 to ((n + batch - 1) div batch - 1)) b]], (vs, b: int) => apply_batch(vs, w, h, s, k, b * batch, if ((b + 1) * batch < n) (b + 1) * batch else n))

// --- depressions, flow and rivers ---
fn outlet(c: int, w: int, h: int, v: float, sea: float) bool => c % w == 0 or c % w == w - 1 or c div w == 0 or c div w == h - 1 or v <= sea

fn around8(c: int, w: int, h: int) array {
    let x = c % w
    let y = c div w;
    [for (dy in -1 to 1) for (dx in -1 to 1) (let nx = x + dx, let ny = y + dy,
        if ((dx == 0 and dy == 0) or nx < 0 or ny < 0 or nx >= w or ny >= h) -1 else ny * w + nx)] |: ~ >= 0
}

// Planchon-Darboux: raise every pit until water can run, by `epsilon` per step, to an outlet
// (the map edge or the sea). Synchronous sweeps until nothing changes.
fn fill_sweep(water, values, w: int, h: int, eps: float) array =>
    [for (c, cur in water) if (cur == values[c]) cur
        else max(values[c], min(cur, min([for (n in around8(c, w, h)) water[n]]) + eps))]

pub fn fill(height, options = {}) array {
    let h = len(height)
    let w = if (h == 0) 0 else len(height[0])
    let sea: float = if (options.sea_level == null) -1.0 else float(options.sea_level)
    let eps: float = if (options.epsilon == null) 1e-6 else float(options.epsilon)
    let values = flat(height)
    let first = [for (c, v in values) if (outlet(c, w, h, v, sea)) v else 1e300]
    let done = reduce([[first, false], *[for (i in 1 to w * h) i]], (st, i) => if (st[1]) st
        else (let next = fill_sweep(st[0], values, w, h, eps), [next, next == st[0]]));
    rows(done[0], w, h)
}

// each cell's steepest strictly-lower 8-neighbour, or -1 (D8 flow).
fn downhill(values, w: int, h: int) array =>
    [for (c, v in values) (let below = [for (n in around8(c, w, h) where values[n] < v) [values[n], n]],
        if (len(below) == 0) -1 else sort(below, (e) => e)[0][1])]

// cells drained through each cell, itself included: one pass from the highest cell down.
pub fn accumulation(height) array {
    let h = len(height)
    let w = if (h == 0) 0 else len(height[0])
    let values = flat(height)
    let down = downhill(values, w, h)
    let order = [for (e in sort([for (c, v in values) [0.0 - v, c]], (e) => e)) e[1]];
    rows(reduce([[for (c in values) 1], *order], (acc, c) => if (down[c] < 0) acc else set_at(acc, down[c], acc[down[c]] + acc[c])), w, h)
}

// rivers on a filled surface: segments [x0, y0, x1, y1, flow] between cell centres, for land
// cells draining at least `threshold` cells (default 0.5% of the map).
pub fn rivers(height, options = {}) map {
    let h = len(height)
    let w = if (h == 0) 0 else len(height[0])
    let sea: float = if (options.sea_level == null) -1.0 else float(options.sea_level)
    let minimum_flow = if (options.threshold == null) max(4, (w * h) div 200) else options.threshold
    let filled = fill(height, {sea_level: sea})
    let values = flat(filled)
    let down = downhill(values, w, h)
    let acc = accumulation(filled)
    let flows = flat(acc)
    let segments = [for (c, f in flows where f >= float(minimum_flow) and values[c] > sea and down[c] >= 0)
        [float(c % w) + 0.5, float(c div w) + 0.5, float(down[c] % w) + 0.5, float(down[c] div w) + 0.5, int(f)]]
    let paths = contour.stitch([for (s in segments)
        {a: {key: [s[0], s[1]], point: [s[0], s[1]]}, b: {key: [s[2], s[3]], point: [s[2], s[3]]}, faces: []}]);
    {accumulation: acc, filled: filled,
     segments: segments, polylines: paths |> ~.points}
}

// --- moisture and biomes ---
// half keyed noise, half nearness to water (sea or river cells), within `reach` cells.
pub fn moisture(k: u64, water, options = {}) array^ {
    let h = len(water)
    let w = if (h == 0) 0 else len(water[0])
    let reach: float = if (options.reach == null) float(max(w, h)) / 8.0 else float(options.reach)
    let wet = [for (y, row in water) for (x, v in row where v) [x, y]]
    let dist = grid.distances([for (row in water) [for (v in row) 0]], wet)
    let damp = field.normalize(noise.field(noise.make(rng.derive(k, "moisture"), {octaves: 4, frequency: 3.0 / float(min(w, h))})^, w, h));
    [for (y in 0 to (h - 1)) [for (x in 0 to (w - 1))
        (let d = dist[y][x], let near = if (d < 0) 0.0 else max(0.0, 1.0 - float(d) / reach), 0.5 * damp[y][x] + 0.5 * near)]]
}

// Amit Patel's mapgen2 table: elevation above sea against moisture.
fn biome_of(e: float, m: float) string {
    if (e > 0.8) (if (m < 0.1) "scorched" else if (m < 0.2) "bare" else if (m < 0.5) "tundra" else "snow")
    else if (e > 0.6) (if (m < 0.33) "temperate desert" else if (m < 0.66) "shrubland" else "taiga")
    else if (e > 0.3) (if (m < 0.16) "temperate desert" else if (m < 0.5) "grassland" else if (m < 0.83) "temperate deciduous forest" else "temperate rain forest")
    else (if (m < 0.16) "subtropical desert" else if (m < 0.33) "grassland" else if (m < 0.66) "tropical seasonal forest" else "tropical rain forest")
}

// latitude is normalized to [-1,1] (poles); temperature is in [0,1].
pub fn temperature(height, options = {}) array^ {
    let centre = if (options.latitude == null) 0.0 else options.latitude
    let span = if (options.latitude_span == null) 2.0 else options.latitude_span
    let lapse = if (options.lapse_rate == null) 0.6 else options.lapse_rate
    let sea = if (options.sea_level == null) 0.4 else options.sea_level;
    if (not core.valid(height) or not all([for (v in [centre, span, lapse, sea]) core.finite(v)]) or
        abs(centre) > 1 or span < 0 or span > 2 or lapse < 0 or sea < 0 or sea >= 1)
        raise error("procart.terrain.temperature: invalid latitude, lapse rate or sea level")
    else (let h = len(height), [for (y, row in height)
        (let latitude = centre + (if (h < 2) 0.0 else (float(y) / float(h - 1) - 0.5) * span),
        [for (v in row) max(0.0, min(1.0, 1.0 - abs(latitude) - max(0.0, float(v) - sea) / (1.0 - sea) * lapse))])])
}

pub fn biomes(height, wetness, options = {}) array^ {
    let sea: float = if (options.sea_level == null) 0.4 else float(options.sea_level)
    let beach: float = if (options.beach == null) 0.02 else float(options.beach);
    if (options.temperature != null and (not core.valid(options.temperature) or len(options.temperature) != len(height) or
        not all([for (y, row in height) len(options.temperature[y]) == len(row) and
            all([for (v in options.temperature[y]) v >= 0 and v <= 1])])))
        raise error("procart.terrain.biomes: temperature must match the height field and lie in [0,1]")
    else [for (y, row in height) [for (x, v in row) if (v <= sea) "ocean" else if (v <= sea + beach) "beach"
        else biome_of(if (options.temperature == null) (v - sea) / (1.0 - sea) else 1.0 - options.temperature[y][x], wetness[y][x])]]
}

// mapgen2's colours as [r, g, b], plus "river".
pub fn biome_colors() map => {ocean: [68, 68, 122], beach: [160, 144, 119], river: [34, 85, 136],
    scorched: [85, 85, 85], bare: [136, 136, 136], tundra: [187, 187, 170], snow: [221, 221, 228],
    'temperate desert': [201, 210, 155], shrubland: [136, 153, 119], taiga: [153, 170, 119], grassland: [136, 170, 85],
    'temperate deciduous forest': [103, 148, 89], 'temperate rain forest': [68, 136, 85],
    'subtropical desert': [210, 185, 139], 'tropical seasonal forest': [85, 153, 68], 'tropical rain forest': [51, 119, 85]}

// --- the whole recipe ---
// fields, flow segments/paths and optional climate; options pass to heightmap/erosion.
pub fn world(k: u64, options) map^ {
    let sea: float = if (options.sea_level == null) 0.4 else float(options.sea_level)
    let raw = heightmap(k, {island: 0.5, *: options})^
    let eroded = if (options.erosion == false) raw
        else field.normalize(thermal(hydraulic(rng.derive(k, "rain"), raw, {droplets: if (options.droplets == null) len(raw) * len(raw[0]) div 2 else options.droplets})^, {iterations: 4})^)
    let streams = rivers(eroded, {sea_level: sea, threshold: options.threshold})
    let river_cells = [for (s in streams.segments) [int(s[0]), int(s[1])]]
    let on_river = map([for (p in river_cells) for (x in [string(p[1] * len(eroded[0]) + p[0]), true]) x])
    let water = [for (y, row in eroded) [for (x, v in row) v <= sea or on_river[string(y * len(row) + x)] == true]]
    let wet = moisture(k, water)^
    let climate = if (options.climate == true) temperature(eroded, options)^ else null;
    {height: eroded, rivers: streams.segments, river_polylines: streams.polylines, moisture: wet, temperature: climate,
        biomes: biomes(eroded, wet, {sea_level: sea, temperature: climate})^}
}
