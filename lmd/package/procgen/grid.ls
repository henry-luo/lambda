// Tile grids (Lambda_Pkg_Proc_Gen.md §4 procgen.grid): cellular automata,
// regions, distance maps, random walks, BSP rooms and mazes. A tile grid is
// int[][] rows, tiles[y][x], with 0 for floor and 1 for wall. Internally a grid
// is a flat row-major array; every draw is keyed (PG3) and only integer and
// basic arithmetic is used, so all of it is exact-tier (PG2).
import rng: .rng

fn rows(flat, width: int, height: int) array => [for (y in 0 to (height - 1)) [for (x in 0 to (width - 1)) flat[y * width + x]]]
fn cells_of(tiles) array => [for (row in tiles) for (v in row) v]
fn set_at(flat, i: int, v) array => [*slice(flat, 0, i), v, *slice(flat, i + 1, len(flat))]
fn modulo(a: int, m: int) int => ((a % m) + m) % m

fn dims(options, caller) array^ {
    let w = options.width
    let h = options.height;
    if (not (w is int) or not (h is int) or w < 1 or h < 1 or w * h > 1000000)
        raise error("procgen.grid." ++ caller ++ ": width and height must be positive ints, at most 1,000,000 cells")
    else [w, h]
}

// the in-bounds 4-neighbours of cell c.
fn around(c: int, w: int, h: int) array {
    let x = c % w
    let y = c div w;
    [for (n in [if (x > 0) c - 1 else -1, if (x < w - 1) c + 1 else -1, if (y > 0) c - w else -1, if (y < h - 1) c + w else -1] where n >= 0) n]
}

// --- cellular automata ---
pub fn random_fill(k: u64, options) array^ {
    let d = dims(options, "random_fill")^
    let density: float = if (options.fill == null) 0.45 else float(options.fill);
    rows([for (c in 0 to (d[0] * d[1] - 1)) if (rng.chance(rng.at(k, c), density)) 1 else 0], d[0], d[1])
}

// "B5678/S45678": the live-neighbour counts that give birth and that survive.
fn parse_rule(rule) array^ {
    let parts = if (rule is string) split(rule, "/") else [];
    if (len(parts) != 2 or not starts_with(parts[0], "B") or not starts_with(parts[1], "S"))
        raise error("procgen.grid: a rule is written \"B<digits>/S<digits>\", such as \"B3/S23\"")
    else [for (p in parts) [for (i in 1 to (len(p) - 1)) int(slice(p, i, i + 1))]]
}

fn live_around(flat, c: int, w: int, h: int, edge: int) int {
    let x = c % w
    let y = c div w;
    sum([for (dy in -1 to 1) for (dx in -1 to 1) if (dx == 0 and dy == 0) 0
        else (let nx = x + dx, let ny = y + dy, if (nx < 0 or ny < 0 or nx >= w or ny >= h) edge else flat[ny * w + nx])])
}

fn generation(flat, rule, w: int, h: int, edge: int) array =>
    [for (c, v in flat) (let n = live_around(flat, c, w, h, edge), if (v == 1) (if (n in rule[1]) 1 else 0) else (if (n in rule[0]) 1 else 0))]

// `steps` generations of a life-like rule; cells beyond the edge count as `edge` (1: walls).
pub fn evolve(tiles, options = {}) array^ {
    let rule = parse_rule(if (options.rule == null) "B5678/S45678" else options.rule)^
    let steps = if (options.steps == null) 5 else options.steps
    let edge: int = if (options.edge == null) 1 else options.edge
    let h = len(tiles)
    let w = if (h == 0) 0 else len(tiles[0]);
    if (not (steps is int) or steps < 0 or steps > 1000) raise error("procgen.grid.evolve: steps must be an int in 0..1000")
    else rows(reduce([cells_of(tiles), *[for (i in 1 to steps) i]], (flat, i) => generation(flat, rule, w, h, edge)), w, h)
}

// a cave: random fill, then the smoothing rule (Lambda_Pkg_Proc_Gen.md §5).
pub fn automaton(k: u64, options) array^ => evolve(random_fill(k, options)^, options)^

// --- regions: min-caller propagation with pointer jumping, O(log diameter) rounds in practice ---
fn label_round(lab, passable, w: int, h: int) array {
    let lowest = [for (c, l in lab) if (l < 0) -1 else min([l, *[for (n in around(c, w, h) where passable[n]) lab[n]]])];
    [for (l in lowest) if (l < 0) -1 else lowest[l]]
}

fn settle(lab, passable, w: int, h: int) array => (let next = label_round(lab, passable, w, h), if (next == lab) lab else settle(next, passable, w, h))

// labels[y][x] is the region id of a floor cell (numbered in row-major order of first appearance), -1 elsewhere.
pub fn regions(tiles, floor_tile = 0) map {
    let h = len(tiles)
    let w = if (h == 0) 0 else len(tiles[0])
    let passable = [for (v in cells_of(tiles)) v == floor_tile]
    let roots = settle([for (c, o in passable) if (o) c else -1], passable, w, h)
    let ids = unique([for (r in roots where r >= 0) r])
    let id_of = map([for (i, r in ids) for (x in [string(r), i]) x])
    let labels = [for (r in roots) if (r < 0) -1 else id_of[string(r)]];
    {labels: rows(labels, w, h), sizes: [for (i in 0 to (len(ids) - 1)) len([for (l in labels where l == i) l])]}
}

// the largest region (the first of equal ones) stays floor; every other floor cell becomes wall.
pub fn keep_largest(tiles, floor_tile = 0, wall = 1) array {
    let r = regions(tiles, floor_tile);
    if (len(r.sizes) == 0) tiles
    else (let biggest = [for (i, s in r.sizes where s == max(r.sizes)) i][0],
        [for (y, row in tiles) [for (x, v in row) if (v == floor_tile and r.labels[y][x] != biggest) wall else v]])
}

// --- distance maps: breadth-first layers over floor cells ---
fn layer(dist, passable, w: int, h: int, d: int) array =>
    [for (c, v in dist) if (v >= 0 or not passable[c]) v else if (any([for (n in around(c, w, h)) dist[n] == d])) d + 1 else -1]

// steps from the nearest source [x, y] through 4-connected floor; -1 for walls and unreachable cells.
pub fn distances(tiles, sources, floor_tile = 0) array {
    let h = len(tiles)
    let w = if (h == 0) 0 else len(tiles[0])
    let passable = [for (v in cells_of(tiles)) v == floor_tile]
    // a map, not `in` over the list: moisture passes every water cell as a source
    let starts = map([for (s in sources) for (x in [string(s[1] * w + s[0]), true]) x])
    let first = [for (c, o in passable) if (o and starts[string(c)] == true) 0 else -1]
    // each live round extends one layer; a finished map passes through unchanged
    let done = reduce([[first, 0, false], *[for (i in 1 to len(passable)) i]], (st, i) => if (st[2]) st
        else (let next = layer(st[0], passable, w, h, st[1]), [next, st[1] + 1, next == st[0]]));
    rows(done[0], w, h)
}

// --- drunkard's walk: carve until `fill` of the interior is floor ---
// Folding an unbounded walk into [0, n) reflects it at the border: an outward
// step becomes a pause, so positions come from one cumsum, not a sequential fold.
fn fold_into(u: int, n: int) int => (let m = modulo(u, 2 * n), if (m < n) m else 2 * n - 1 - m)

fn steps_of(k: u64, n: int) array => [for (i in 0 to (n - 1)) rng.below(rng.at(k, i), 4)]

pub fn walk(k: u64, options) array^ {
    let d = dims(options, "walk")^
    let iw = d[0] - 2
    let ih = d[1] - 2;
    if (iw < 1 or ih < 1) raise error("procgen.grid.walk: width and height must be at least 3")
    else {
        let density: float = if (options.fill == null) 0.4 else float(options.fill)
        let n = if (options.steps == null) 20 * iw * ih else options.steps
        let origin = if (options.start == null) [iw div 2, ih div 2] else [options.start[0] - 1, options.start[1] - 1]
        let dirs = steps_of(k, n)
        let xs = [origin[0], *(math.cumsum([for (s in dirs) if (s == 0) 1 else if (s == 1) -1 else 0]) + origin[0])]
        let ys = [origin[1], *(math.cumsum([for (s in dirs) if (s == 2) 1 else if (s == 3) -1 else 0]) + origin[1])]
        let visits = [for (i in 0 to n) [(fold_into(ys[i], ih) + 1) * d[0] + fold_into(xs[i], iw) + 1, i]]
        let first = sort([for (v in visits group by v[0] as cell into g) [min(g |> ~[1]), cell]], (e) => e)
        let target = int(ceil(density * float(iw * ih)))
        let carved = map([for (j, e in first where j < target) for (x in [string(e[1]), true]) x]);
        rows([for (c in 0 to (d[0] * d[1] - 1)) if (carved[string(c)] == true) 0 else 1], d[0], d[1])
    }
}

// --- BSP: recursive splits of a rectangle [x, y, w, h] ---
fn divide(r, k: u64, depth: int, min_size: int) {
    let can_x = r[2] >= 2 * min_size
    let can_y = r[3] >= 2 * min_size;
    if (depth == 0 or (not can_x and not can_y)) {rect: r, children: []}
    else {
        // split across the longer side when it is clearly longer, otherwise by a keyed coin
        let across_x = if (not can_y) true else if (not can_x) false
            else if (r[2] * 4 > r[3] * 5) true else if (r[3] * 4 > r[2] * 5) false else rng.chance(rng.at(k, 0), 0.5)
        let span = if (across_x) r[2] else r[3]
        let cut = min_size + rng.below(rng.at(k, 1), span - 2 * min_size + 1)
        let parts = if (across_x) [[r[0], r[1], cut, r[3]], [r[0] + cut, r[1], r[2] - cut, r[3]]]
            else [[r[0], r[1], r[2], cut], [r[0], r[1] + cut, r[2], r[3] - cut]];
        {rect: r, children: [divide(parts[0], rng.at(k, 2), depth - 1, min_size), divide(parts[1], rng.at(k, 3), depth - 1, min_size)]}
    }
}

fn leaves(node) array => if (len(node.children) == 0) [node.rect] else [for (c in node.children) for (r in leaves(c)) r]

fn bsp_tree(k: u64, options, caller) map^ {
    let d = dims(options, caller)^
    let min_size = if (options.min_size == null) 6 else options.min_size
    let depth = if (options.max_depth == null) 8 else options.max_depth;
    if (not (min_size is int) or min_size < 1) raise error("procgen.grid." ++ caller ++ ": min_size must be a positive int")
    else if (not (depth is int) or depth < 0 or depth > 30) raise error("procgen.grid." ++ caller ++ ": max_depth must be an int in 0..30")
    else divide([0, 0, d[0], d[1]], k, depth, min_size)
}

// the partition's leaf rectangles [x, y, w, h], left to right in tree order.
pub fn bsp(k: u64, options) array^ => leaves(bsp_tree(k, options, "bsp")^)

// --- rooms and corridors on a BSP: one room per leaf, an L-shaped corridor per split ---
fn room_in(leaf, k: u64, margin: int) array {
    let room_w = max(1, leaf[2] - 2 * margin)
    let room_h = max(1, leaf[3] - 2 * margin)
    let w = max(1, room_w div 2) + rng.below(rng.at(k, 0), room_w - max(1, room_w div 2) + 1)
    let h = max(1, room_h div 2) + rng.below(rng.at(k, 1), room_h - max(1, room_h div 2) + 1);
    [leaf[0] + margin + rng.below(rng.at(k, 2), room_w - w + 1), leaf[1] + margin + rng.below(rng.at(k, 3), room_h - h + 1), w, h]
}

fn centre(room) array => [room[0] + room[2] div 2, room[1] + room[3] div 2]

// the rooms of a subtree in leaf order, and the corridors joining its halves.
fn furnish(node, k: u64, margin: int) map {
    if (len(node.children) == 0) {rooms: [room_in(node.rect, k, margin)], corridors: []}
    else {
        let a = furnish(node.children[0], rng.at(k, 0), margin)
        let b = furnish(node.children[1], rng.at(k, 1), margin)
        // join the rooms nearest the cut: the final_visit of the first half and the first of the second
        let p = centre(a.rooms[len(a.rooms) - 1])
        let q = centre(b.rooms[0])
        let bend = if (rng.chance(rng.at(k, 2), 0.5)) [q[0], p[1]] else [p[0], q[1]];
        {rooms: [*a.rooms, *b.rooms], corridors: [*a.corridors, *b.corridors, [p, bend, q]]}
    }
}

fn on_segment(x: int, y: int, a, b) bool =>
    x >= min(a[0], b[0]) and x <= max(a[0], b[0]) and y >= min(a[1], b[1]) and y <= max(a[1], b[1])

pub fn dungeon(k: u64, options) map^ {
    let tree = bsp_tree(k, {*: options, min_size: if (options.min_size == null) 8 else options.min_size,
        max_depth: if (options.max_depth == null) 6 else options.max_depth}, "dungeon")^
    let margin = if (options.margin == null) 1 else options.margin
    let built = furnish(tree, rng.derive(k, "rooms"), margin)
    let w = tree.rect[2]
    let h = tree.rect[3];
    {rooms: built.rooms, corridors: built.corridors,
     tiles: [for (y in 0 to (h - 1)) [for (x in 0 to (w - 1))
        if (any([for (r in built.rooms) x >= r[0] and x < r[0] + r[2] and y >= r[1] and y < r[1] + r[3]]) or
            any([for (c in built.corridors) on_segment(x, y, c[0], c[1]) or on_segment(x, y, c[1], c[2])])) 0 else 1]]}
}

// --- mazes: spanning trees over a width x height cell grid ---
fn maze_cells(options) array^ {
    let d = dims(options, "maze")^;
    if (d[0] * d[1] > 10000) raise error("procgen.grid.maze: at most 10,000 cells") else d
}

// recursive backtracker as a depth-first walk over parent pointers: the stack is the parent chain.
fn backtracker(k: u64, w: int, h: int) array {
    let n = w * h
    let root = rng.below(rng.derive(k, "root"), n)
    let first = [for (c in 0 to (n - 1)) if (c == root) -1 else -2]
    let walked = reduce([[first, root, false], *[for (i in 1 to 2 * n) i]], (st, i: int) => if (st[2]) st
        else (let parent = st[0], let cur = st[1], let passable = [for (m in around(cur, w, h) where parent[m] == -2) m],
            if (len(passable) > 0) (let next = passable[rng.below(rng.at(k, i), len(passable))], [set_at(parent, next, cur), next, false])
            else if (cur == root) [parent, cur, true] else [parent, parent[cur], false]));
    [for (c, p in walked[0] where p >= 0) [p, c]]
}

// Kruskal over keyed-shuffled edges; a join relabels the absorbed component.
fn kruskal(k: u64, w: int, h: int) array {
    let edges = [*[for (y in 0 to (h - 1)) for (x in 0 to (w - 2)) [y * w + x, y * w + x + 1]],
                 *[for (y in 0 to (h - 2)) for (x in 0 to (w - 1)) [y * w + x, (y + 1) * w + x]]]
    let done = reduce([[[for (c in 0 to (w * h - 1)) c], []], *rng.shuffle(k, edges)], (st, e) =>
        (let la = st[0][e[0]], let lb = st[0][e[1]],
            if (la == lb) st else [[for (l in st[0]) if (l == lb) la else l], [*st[1], e]]));
    done[1]
}

// randomized Prim: a keyed frontier cell joins a keyed neighbour already in the tree.
// status: 0 outside, 1 frontier, 2 tree.
fn prim(k: u64, w: int, h: int) array {
    let n = w * h
    let root = rng.below(rng.derive(k, "root"), n)
    let seeds = around(root, w, h)
    let first = [[for (c in 0 to (n - 1)) if (c == root) 2 else if (c in seeds) 1 else 0], seeds, []]
    let done = reduce([first, *[for (i in 1 to (n - 1)) i]], (st, i: int) => if (len(st[1]) == 0) st
        else (let status = st[0], let f = st[1][rng.below(rng.at(k, i), len(st[1]))],
            let inside = [for (m in around(f, w, h) where status[m] == 2) m],
            let joined = inside[rng.below(rng.at(rng.at(k, i), 1), len(inside))],
            let added = [for (m in around(f, w, h) where status[m] == 0) m],
            [[for (c, s in status) if (c == f) 2 else if (c in added) 1 else s],
             [*[for (m in st[1] where m != f) m], *added], [*st[2], [joined, f]]]));
    done[2]
}

// Wilson: loop-erased random walks give a uniform spanning tree. A walk folded at the
// border pauses there, which only adds erased self-loops: every tree keeps weight (1/4)^(n-1).
fn walk_from(k: u64, w: int, h: int, s: int, length: int) array {
    let dirs = steps_of(k, length)
    let xs = [s % w, *math.cumsum([for (d in dirs) if (d == 0) 1 else if (d == 1) -1 else 0]) + s % w]
    let ys = [s div w, *math.cumsum([for (d in dirs) if (d == 2) 1 else if (d == 3) -1 else 0]) + s div w];
    [for (i in 0 to length) fold_into(ys[i], h) * w + fold_into(xs[i], w)]
}

// walk in doubling chunks until the tree is hit; returns the positions up to the hit.
fn walk_to_tree(k: u64, w: int, h: int, tree, s: int, chunk: int, from) array {
    let path = walk_from(rng.at(k, chunk), w, h, s, chunk)
    let hits = [for (i, c in path where tree[c]) i];
    if (len(hits) > 0) [*from, *slice(path, 0, hits[0] + 1)]
    else walk_to_tree(k, w, h, tree, path[chunk], chunk * 2, [*from, *slice(path, 0, chunk)])
}

// loop erasure: from the start, always leave a cell the way its final_visit visit left it.
fn erase_loops(positions) array {
    let final_visit = map([for (pr in [for (i, c in positions) [c, i]] group by pr[0] as cell into g)
        for (x in [string(cell), max(g |> ~[1])]) x])
    let end = positions[len(positions) - 1];
    reduce([[[positions[0]], false], *[for (i in 1 to len(positions)) i]], (st, i) => if (st[1]) st
        else (let c = st[0][len(st[0]) - 1], if (c == end) [st[0], true] else [[*st[0], positions[final_visit[string(c)] + 1]], false]))[0]
}

fn wilson(k: u64, w: int, h: int) array {
    let n = w * h
    let root = rng.below(rng.derive(k, "root"), n)
    let first = [[for (c in 0 to (n - 1)) c == root], []]
    let done = reduce([first, *[for (i in 1 to n) i]], (st, i: int) =>
        (let passable = [for (c, t in st[0] where not t) c], if (len(passable) == 0) st
        else (let path = erase_loops(walk_to_tree(rng.at(k, i), w, h, st[0], passable[0], 16, [])),
            let on_path = map([for (c in path) for (x in [string(c), true]) x]),
            [[for (c, t in st[0]) t or on_path[string(c)] == true],
             [*st[1], *[for (j in 1 to (len(path) - 1)) [path[j - 1], path[j]]]]])));
    done[1]
}

// a spanning tree's edges [a, b] between cells numbered y * width + x.
pub fn maze_edges(k: u64, options) array^ {
    let d = maze_cells(options)^
    let method = if (options.method == null) 'backtracker' else options.method;
    if (method == 'backtracker') backtracker(k, d[0], d[1])
    else if (method == 'kruskal') kruskal(k, d[0], d[1])
    else if (method == 'prim') prim(k, d[0], d[1])
    else if (method == 'wilson') wilson(k, d[0], d[1])
    else raise error("procgen.grid.maze: method must be 'backtracker', 'kruskal', 'prim' or 'wilson'")
}

// tiles of (2 * height + 1) rows by (2 * width + 1): cell (x, y) sits at tile (2x + 1, 2y + 1).
pub fn maze(k: u64, options) array^ {
    let d = maze_cells(options)^
    let edges = maze_edges(k, options)^
    let w = d[0]
    let passable = map([for (e in edges) for (x in [string((e[0] div w + e[1] div w) + 1) ++ "," ++ string((e[0] % w + e[1] % w) + 1), true]) x]);
    [for (ty in 0 to 2 * d[1]) [for (tx in 0 to 2 * d[0])
        if (ty % 2 == 1 and tx % 2 == 1) 0 else if (passable[string(ty) ++ "," ++ string(tx)] == true) 0 else 1]]
}
