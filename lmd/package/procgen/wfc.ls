// Wave function collapse (Lambda_Pkg_Proc_Gen.md §4 procgen.wfc), after Maxim
// Gumin's WaveFunctionCollapse. A model is a set of patterns with weights and,
// per direction, which patterns may sit next to each other; `run` fills a grid
// with pattern indices and `render` turns them back into sample values.
// Directions are 0 right (+x), 1 down (+y), 2 left, 3 up. The solver picks the
// smallest domain (exact, unlike Shannon entropy, PG2), so all of it is exact-tier.
import rng: .rng

fn opposite(d: int) int => (d + 2) % 4
fn dx(d: int) int => [1, 0, -1, 0][d]
fn dy(d: int) int => [0, 1, 0, -1][d]

// masks[d][p][q] is 1 when q may sit at p's neighbour in direction d.
fn model_of(kind, values, weights, masks, n) map^ {
    let t = len(weights);
    if (t == 0) raise error("procgen.wfc: the model has no patterns")
    else if (t > 1000) raise error("procgen.wfc: more than 1000 patterns")
    else {kind: kind, count: t, values: values, weights: weights, masks: masks, n: n,
          zero: [for (q in 0 to (t - 1)) 0],
          // the counts of a full domain
          full: [for (d in 0 to 3) reduce([[for (q in 0 to (t - 1)) 0], *masks[d]], (a, b) => a + b)]}
}

// --- simple tiled models ---
fn tile_masks(t: int, pairs) array =>
    [for (d in 0 to 3) [for (p in 0 to (t - 1)) (let ok = [for (pr in pairs where pr[0] == d and pr[1] == p) pr[2]],
        [for (q in 0 to (t - 1)) if (q in ok) 1 else 0])]]

// tiles are any values; adjacency is [a, b, "x"] (b may sit right of a) or [a, b, "y"] (b below a).
pub fn tiled(tiles, adjacency, weights = null) map^ {
    let index = map([for (i, t in tiles) for (x in [string(t), i]) x])
    let pairs = [for (r in adjacency, let a = index[string(r[0])], let b = index[string(r[1])], let d = if (r[2] == "x") 0 else 1)
        for (pr in [[d, a, b], [opposite(d), b, a]]) pr];
    if (any([for (r in adjacency) index[string(r[0])] == null or index[string(r[1])] == null or not (r[2] == "x" or r[2] == "y")]))
        raise error("procgen.wfc.tiled: adjacency rules are [a, b, \"x\" | \"y\"] over known tiles")
    else model_of('tiled', tiles, if (weights == null) [for (t in tiles) 1] else weights, tile_masks(len(tiles), pairs), 1)^
}

// tiles, weights and adjacency observed in a sample grid (each value is a tile).
pub fn tiled_from_sample(sample, options = {}) map^ {
    let h = len(sample)
    let w = if (h == 0) 0 else len(sample[0])
    let wrap = options.periodic == true
    let tiles = unique([for (row in sample) for (v in row) v])
    let index = map([for (i, t in tiles) for (x in [string(t), i]) x])
    let pairs = unique([for (y in 0 to (h - 1)) for (x in 0 to (w - 1)) for (d in 0 to 3)
        (let nx = x + dx(d), let ny = y + dy(d), if (wrap or (nx >= 0 and ny >= 0 and nx < w and ny < h))
            [d, index[string(sample[y][x])], index[string(sample[(ny + h) % h][(nx + w) % w])]] else null)] |: ~ != null);
    if (h == 0) raise error("procgen.wfc.tiled_from_sample: the sample is empty")
    else model_of('tiled', tiles, [for (t in tiles) len([for (row in sample) for (v in row where v == t) v])], tile_masks(len(tiles), pairs), 1)^
}

// --- overlapping models: every n x n window of the sample is a pattern ---
fn turn_quarter(p, n: int) array => [for (y in 0 to (n - 1)) for (x in 0 to (n - 1)) p[(n - 1 - x) * n + y]]
fn reflect(p, n: int) array => [for (y in 0 to (n - 1)) for (x in 0 to (n - 1)) p[y * n + n - 1 - x]]

// Gumin's variant order: p, its mirror, then each rotation followed by its mirror.
fn variants(p, n: int, symmetry: int) array {
    let r1 = turn_quarter(p, n)
    let r2 = turn_quarter(r1, n)
    let r3 = turn_quarter(r2, n);
    slice([p, reflect(p, n), r1, reflect(r1, n), r2, reflect(r2, n), r3, reflect(r3, n)], 0, symmetry)
}

// q may sit at offset (ox, oy) from p when their overlapping cells agree.
fn agrees(p, q, n: int, ox: int, oy: int) bool =>
    all([for (y in max(0, oy) to (min(n, n + oy) - 1)) for (x in max(0, ox) to (min(n, n + ox) - 1)) p[y * n + x] == q[(y - oy) * n + x - ox]])

pub fn overlapping(sample, options = {}) map^ {
    let n = if (options.n == null) 3 else options.n
    let symmetry = if (options.symmetry == null) 8 else options.symmetry
    let wrap = options.periodic_input != false
    let h = len(sample)
    let w = if (h == 0) 0 else len(sample[0]);
    if (not (n is int) or n < 2 or n > h or n > w) raise error("procgen.wfc.overlapping: n must be an int from 2 to the sample size")
    else if (not (symmetry in [1, 2, 4, 8])) raise error("procgen.wfc.overlapping: symmetry must be 1, 2, 4 or 8")
    else {
        let colors = unique([for (row in sample) for (v in row) v])
        let color = map([for (i, c in colors) for (x in [string(c), i]) x])
        let windows = [for (y in 0 to (if (wrap) h - 1 else h - n)) for (x in 0 to (if (wrap) w - 1 else w - n))
            for (v in variants([for (j in 0 to (n - 1)) for (i in 0 to (n - 1)) color[string(sample[(y + j) % h][(x + i) % w])]], n, symmetry)) v]
        let counted = [for (p in windows group by join([for (c in p) string(c)], ",") as id into same) [same[0], len(same)]]
        let patterns = counted |> ~[0]
        let model = model_of('overlapping', [for (p in patterns) colors[p[0]]], counted |> ~[1],
            [for (d in 0 to 3) [for (p in patterns) [for (q in patterns) if (agrees(p, q, n, dx(d), dy(d))) 1 else 0]]], n)^;
        {*: model, patterns: [for (p in patterns) [for (c in p) colors[c]]]}
    }
}

// --- the solver ---
fn neighbour(c: int, d: int, w: int, h: int, wrap: bool) int {
    let x = c % w + dx(d)
    let y = c div w + dy(d);
    if (wrap) ((y + h) % h) * w + (x + w) % w else if (x < 0 or y < 0 or x >= w or y >= h) -1 else y * w + x
}

// The solver keeps, per cell c and direction d, a count vector: counts[c][d][p] is how many
// patterns of c's domain allow p at c's d-neighbour. A revision is then lookups, and a
// shrinking domain subtracts only the removed patterns' rows, so the work is amortized over
// all removals rather than repeated per sweep (the counting form of Gumin's propagator).
fn sum_rows(model, rows) array => reduce([model.zero, *rows], (a, b) => a + b)

fn counts_of(model, dom) array => [for (d in 0 to 3) sum_rows(model, [for (q in dom) model.masks[d][q]])]

// counts after a domain shrinks from `old` to `kept`: rebuild from the smaller side.
fn recount(model, counts, old, kept) array {
    if (len(kept) * 2 <= len(old)) counts_of(model, kept)
    else (let removed = except(old, kept), [for (d in 0 to 3) counts[d] - sum_rows(model, [for (q in removed) model.masks[d][q]])])
}

// keep the patterns of cell c that every neighbour's domain still supports.
fn revise(st, c: int, w: int, h: int, wrap: bool) array {
    let supports = [for (d in 0 to 3, let m = neighbour(c, d, w, h, wrap) where m >= 0) st.counts[m][opposite(d)]];
    if (len(supports) == 0) st.domains[c]
    // counts are non-negative, so their product is positive exactly where every neighbour supports p
    else (let joint = reduce(supports, (a, b) => a * b), [for (p in st.domains[c] where joint[p] > 0) p])
}

// replace cells at ascending indices: native slices between the updates, not a pass over every cell.
fn splice(cells, ups) array {
    let starts = [0, *[for (u in ups) u[0] + 1]];
    reduce([[], *[for (j, u in ups) j]], (acc, j) => acc ++ slice(cells, starts[j], ups[j][0]) ++ [ups[j][1]]) ++
        slice(cells, starts[len(ups)], len(cells))
}

// arc consistency in synchronous sweeps: revise the neighbours of changed cells until nothing
// shrinks. Returns {domains, counts}, or null on a contradiction.
fn propagate(model, st, changed, w: int, h: int, wrap: bool) {
    let candidates = unique([for (c in changed) for (d in 0 to 3, let m = neighbour(c, d, w, h, wrap) where m >= 0) m])
    let shrunk = sort([for (c in candidates, let kept = revise(st, c, w, h, wrap) where len(kept) < len(st.domains[c])) [c, kept]], (r) => r[0]);
    if (any([for (r in shrunk) len(r[1]) == 0])) null
    else if (len(shrunk) == 0) st
    else (let counts = [for (r in shrunk) [r[0], recount(model, st.counts[r[0]], st.domains[r[0]], r[1])]],
        propagate(model, {domains: splice(st.domains, shrunk), counts: splice(st.counts, counts)}, shrunk |> ~[0], w, h, wrap))
}

// one observation: collapse a smallest undecided domain to one keyed, weighted pattern.
fn observe(model, st, k: u64, w: int, h: int, wrap: bool) {
    let sizes = [for (dom in st.domains) len(dom)]
    let pending = [for (c, s in sizes where s > 1) c];
    if (len(pending) == 0) {done: true, state: st}
    else (let least = min([for (c in pending) sizes[c]]), let ties = [for (c in pending where sizes[c] == least) c],
        let c = ties[rng.below(rng.at(k, 0), len(ties))],
        let p = rng.weighted(rng.at(k, 1), st.domains[c], [for (q in st.domains[c]) model.weights[q]]),
        let collapsed = {domains: splice(st.domains, [[c, [p]]]), counts: splice(st.counts, [[c, [for (d in 0 to 3) model.masks[d][p]]]])},
        {done: false, state: propagate(model, collapsed, [c], w, h, wrap)})
}

fn attempt(model, k: u64, w: int, h: int, wrap: bool) {
    let n = w * h
    let full = [for (p in 0 to (model.count - 1)) p]
    let initial = propagate(model, {domains: [for (c in 0 to (n - 1)) full], counts: [for (c in 0 to (n - 1)) model.full]},
        [for (c in 0 to (n - 1)) c], w, h, wrap);
    if (initial == null) null
    else (let end = reduce([{done: false, state: initial}, *[for (i in 0 to n) i]], (st, i: int) =>
        if (st.done or st.state == null) st else observe(model, st.state, rng.at(k, i), w, h, wrap)),
        if (end.state == null) null else end.state.domains)
}

fn attempts(model, k: u64, w: int, h: int, wrap: bool, i: int, limit: int) {
    if (i >= limit) null
    else (let out = attempt(model, rng.at(k, i), w, h, wrap), if (out != null) out else attempts(model, k, w, h, wrap, i + 1, limit))
}

// a width x height grid of pattern indices; periodic wraps the output. A contradiction
// restarts with the next keyed attempt, up to `retries` (10).
// the binary64 kernel covers positive int/float weights with exactly representable
// integer prefixes. Other numeric domains keep the Lambda reference (PG7.3).
fn native_weights(weights) bool =>
    all([for (w in weights) (w is int or w is float) and w > 0 and w <= 9007199254740991.0]) and
    all([for (s in math.cumsum(weights)) s > 0 and s <= 9007199254740991.0])

fn run_with(k: u64, model, options, use_native: bool) array^ {
    let w = options.width
    let h = options.height
    let retries = if (options.retries == null) 10 else options.retries;
    if (not (w is int) or not (h is int) or w < 1 or h < 1 or w * h > 10000) raise error("procgen.wfc.run: width and height must be positive ints, at most 10,000 cells")
    else if (not (retries is int) or retries < 1) raise error("procgen.wfc.run: retries must be a positive int")
    else if (use_native and native_weights(model.weights)) {
        let out = math.wfc(k, model.weights, model.masks, [w, h, options.periodic == true, retries])^;
        [for (y in 0 to (h - 1)) [for (x in 0 to (w - 1)) out[y * w + x]]]
    } else (let out = attempts(model, k, w, h, options.periodic == true, 0, retries),
         if (out == null) raise error("procgen.wfc.run: contradiction in every one of " ++ string(retries) ++ " attempts")
         else [for (y in 0 to (h - 1)) [for (x in 0 to (w - 1)) out[y * w + x][0]]])
}

pub fn run(k: u64, model, options) array^ => run_with(k, model, options, math.wfc is fn)^

// retained oracle and fallback; unavailable native functions read as null.
pub fn run_reference(k: u64, model, options) array^ => run_with(k, model, options, false)^

// sample values for a grid of pattern indices: the tile, or a pattern's top-left value.
pub fn render(model, grid) array => [for (row in grid) [for (p in row) model.values[p]]]

pub fn generate(k: u64, model, options) array^ => render(model, run(k, model, options)^)
