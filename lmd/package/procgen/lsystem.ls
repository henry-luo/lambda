// L-systems (Lambda_Pkg_Proc_Gen.md §4 procgen.lsystem, PG9): rewriting and a
// bracketed 2D turtle. A system is a string of one-character symbols, or for a
// parametric system an array of modules: a symbol string or [symbol, ...params].
// Rewriting is exact-tier; the turtle turns by free angles through cos/sin, so
// it is tolerance-tier (PG2).
import rng: .rng

let MAX_SYMBOLS = 100000

// --- rewriting ---
// A rule is a replacement string or module array; an array of [weight, body] pairs
// picks one body per symbol, keyed by generation and position; a function of the
// module returns its replacement (a parametric rule). A symbol without a rule stays.
fn stochastic(body) bool => body is array and len(body) > 0 and all([for (o in body) o is array and len(o) == 2 and o[0] is number])

fn choose(body, k, generation: int, at: int) =>
    if (stochastic(body)) rng.weighted(rng.at(rng.at(k, generation), at), body |> ~[1], body |> ~[0]) else body

fn modules_of(body) array => if (body is string) [for (i in 0 to (len(body) - 1)) slice(body, i, i + 1)] else body
fn symbol_of(m) => if (m is array) m[0] else m

fn rewrite_text(source, rules, k, generation: int) =>
    join([for (i in 0 to (len(source) - 1)) (let s = slice(source, i, i + 1), let body = rules[s],
        if (body == null) s else choose(body, k, generation, i))], "")

fn rewrite_modules(source, rules, k, generation: int) array =>
    [for (i, m in source) for (r in (let body = rules[symbol_of(m)],
        if (body == null) [m] else if (body is function) modules_of(body(m)) else modules_of(choose(body, k, generation, i)))) r]

fn grow(source, rules, k, generation: int, order: int) {
    if (generation >= order) source
    else (let next = if (source is string) rewrite_text(source, rules, k, generation) else rewrite_modules(source, rules, k, generation),
        if (len(next) > MAX_SYMBOLS) error("procgen.lsystem: expansion exceeds " ++ string(MAX_SYMBOLS) ++ " symbols")
        else grow(next, rules, k, generation + 1, order))
}

// `order` generations of `rules` from `axiom`; stochastic rules need a key.
pub fn expand(axiom, rules, order, k = null) any^ {
    let keyed = any([for (key, body at rules) stochastic(body)]);
    if (not (order is int) or order < 0 or order > 64) raise error("procgen.lsystem: order must be an int in 0..64")
    else if (keyed and not (k is u64)) raise error("procgen.lsystem: stochastic rules need a key")
    else if (not (axiom is string or axiom is array)) raise error("procgen.lsystem: the axiom must be a string or an array of modules")
    else (let out = grow(axiom, rules, k, 0, order), if (out is error) raise out else out)
}

// --- the turtle ---
// Brackets push and pop the turtle. The sequence is parsed into nested blocks and
// each run is walked by halving, as the TikZ turtle did, so long strings need only
// logarithmic stack depth.
fn blocks(syms, depth, closer, lo: int, hi: int, base: int) array =>
    [for (i in lo to (hi - 1) where depth[i] == base and syms[i] != "]")
        if (syms[i] == "[") {block: blocks(syms, depth, closer, i + 1, closer[string(i)], base + 1)} else i]

fn act(t, i: int, x: float, y: float, heading: float, level: int) array {
    let s = t.syms[i]
    let p = t.params[i];
    if (contains(t.draw, s) or contains(t.move, s))
        (let length = if (len(p) > 0) float(p[0]) else t.step, let radians = heading * 3.141592653589793 / 180.0,
            let nx = x + length * math.cos(radians), let ny = y + length * math.sin(radians),
            [nx, ny, heading, if (contains(t.draw, s)) [[x, y, nx, ny, level]] else []])
    else if (s == "+") [x, y, heading + (if (len(p) > 0) float(p[0]) else t.angle), []]
    else if (s == "-") [x, y, heading - (if (len(p) > 0) float(p[0]) else t.angle), []]
    else if (contains(t.reverse, s)) [x, y, heading + 180.0, []]
    else [x, y, heading, []]
}

// returns [x, y, heading, segments] after items[lo, hi).
fn walk(t, items, lo: int, hi: int, x: float, y: float, heading: float, level: int) array {
    if (lo >= hi) [x, y, heading, []]
    else if (hi - lo == 1)
        (let item = items[lo], if (item is map) [x, y, heading, walk(t, item.block, 0, len(item.block), x, y, heading, level + 1)[3]]
            else act(t, item, x, y, heading, level))
    else (let mid = (lo + hi) div 2, let a = walk(t, items, lo, mid, x, y, heading, level),
        let b = walk(t, items, mid, hi, a[0], a[1], a[2], level), [b[0], b[1], b[2], a[3] ++ b[3]])
}

// consecutive segments that join end to start share one polyline: [[x, y], ...] each.
pub fn polylines(segments) array {
    let starts = [for (i, s in segments where i == 0 or s[0] != segments[i - 1][2] or s[1] != segments[i - 1][3]) i]
    let ends = [*slice(starts, 1, len(starts)), len(segments)];
    [for (j, a in starts) [[segments[a][0], segments[a][1]], *[for (i in a to (ends[j] - 1)) [segments[i][2], segments[i][3]]]]]
}

// Returns {segments: [[x0, y0, x1, y1, depth], ...], end: [x, y, heading]}, depth being the bracket
// nesting of the segment. Options: step (1.0), angle in degrees (90), heading (0, along +x), start ([0, 0]), and the symbol
// sets draw ("FG": move and draw), move ("f": move only) and reverse ("|": turn 180). A module's
// first parameter overrides the step of a draw or move and the angle of a turn.
pub fn turtle(sequence, options = {}) map^ {
    let syms = if (sequence is string) modules_of(sequence) else [for (m in sequence) symbol_of(m)]
    let params = if (sequence is string) [for (s in syms) []] else [for (m in sequence) if (m is array) slice(m, 1, len(m)) else []]
    let delta = [for (s in syms) if (s == "[") 1 else if (s == "]") -1 else 0]
    let after = math.cumsum(delta);
    if (len(syms) > 0 and (min(after) < 0 or after[len(after) - 1] != 0)) raise error("procgen.lsystem.turtle: unbalanced brackets")
    else {
        let depth = [for (i, d in delta) after[i] - d]
        let level_of = [for (i, s in syms) if (s == "]") depth[i] - 1 else depth[i]]
        let pairs = [for (i, s in syms where s == "[" or s == "]") [level_of[i], i]]
        let closer = map([for (pr in pairs group by pr[0] as level into g) for (j, pos in g |> ~[1] where j % 2 == 0)
            for (x in [string(pos), (g |> ~[1])[j + 1]]) x])
        let t = {syms: syms, params: params, step: float(if (options.step == null) 1.0 else options.step),
                 angle: float(if (options.angle == null) 90.0 else options.angle),
                 draw: if (options.draw == null) "FG" else options.draw, move: if (options.move == null) "f" else options.move,
                 reverse: if (options.reverse == null) "|" else options.reverse}
        let origin = if (options.start == null) [0.0, 0.0] else options.start
        let items = blocks(syms, depth, closer, 0, len(syms), 0)
        let done = walk(t, items, 0, len(items), float(origin[0]), float(origin[1]), float(if (options.heading == null) 0.0 else options.heading), 0);
        {segments: done[3], end: [done[0], done[1], done[2]]}
    }
}
