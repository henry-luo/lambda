// A home as plain data, and its edits as pure functions. Lengths are in
// centimetres on the plan, x to the east and y to the south:
// {title, wall_height, walls, rooms, items, seq}.
// A wall is {id, x1, y1, x2, y2, thick, color}; a room is {id, name, pts, floor};
// an item is a catalog piece {id, key, x, y, rot, w, d, h, elev, color}, and a door
// or window also names its `wall` and its centre's distance `at` along it.
// Walls meet where their ends coincide, and room corners sit on wall ends, so
// moving a corner moves every wall and room that shares it.
import C: .mod_catalog

pub let DEG = 57.29577951308232
pub let GRID = 10.0
pub fn clamp(v, lo, hi) => max(lo, min(hi, v))
pub fn snap(v, step = GRID) => round(v / step) * step
pub fn dist(ax, ay, bx, by) => math.hypot(bx - ax, by - ay)
fn near(ax, ay, bx, by) => abs(ax - bx) < .5 and abs(ay - by) < .5

pub fn blank(title = "My home") => {title: title, wall_height: 260.0, walls: [], rooms: [], items: [], seq: 1}
// a new id and the home that has used it
pub fn fresh(home, prefix) => {home: {*:home, seq: home.seq + 1}, id: prefix ++ string(home.seq)}
// ids say what they name: walls w…, rooms r…, items i…
pub fn kind_of(id) => if (id == null or id == "") null else (let p = slice(id, 0, 1),
    if (p == "w") "wall" else if (p == "r") "room" else "item")

pub fn wall_of(home, id) => [for (w in home.walls where w.id == id) w][0]
pub fn room_of(home, id) => [for (r in home.rooms where r.id == id) r][0]
pub fn item_of(home, id) => [for (it in home.items where it.id == id) it][0]
pub fn thing_of(home, id) => (let k = kind_of(id), if (k == "wall") wall_of(home, id) else if (k == "room") room_of(home, id)
    else if (k == "item") item_of(home, id) else null)

// ---- walls ------------------------------------------------------------------------

pub fn wall(id, x1, y1, x2, y2, thick = 15.0, color = "#f6f3ec") => {id: id, x1: x1, y1: y1, x2: x2, y2: y2, thick: thick, color: color}
pub fn wall_len(w) => dist(w.x1, w.y1, w.x2, w.y2)
pub fn wall_dir(w) => (let l = max(.001, wall_len(w)), {x: (w.x2 - w.x1) / l, y: (w.y2 - w.y1) / l})
pub fn wall_angle(w) => math.atan2(w.y2 - w.y1, w.x2 - w.x1) * DEG

// where a point falls on a wall: the distance along it and the distance off it
pub fn project_on(w, x, y) => (let l = wall_len(w), let u = wall_dir(w),
    let t = clamp((x - w.x1) * u.x + (y - w.y1) * u.y, 0.0, l),
    {at: t, off: dist(x, y, w.x1 + u.x * t, w.y1 + u.y * t)})
pub fn nearest_wall(home, x, y) => if (len(home.walls) == 0) null
    else (let hits = [for (w in home.walls) {wall: w, *:project_on(w, x, y)}], hits[argmin([for (h in hits) h.off])])

// A wall drawn on the plan reaches half its thickness past an end it shares
// with another wall, so two walls meeting at a corner close it.
fn joined(home, w, x, y) => len([for (o in home.walls where o.id != w.id and (near(o.x1, o.y1, x, y) or near(o.x2, o.y2, x, y))) o]) > 0
pub fn wall_ends(home, w) => (let u = wall_dir(w), let e = w.thick / 2.0,
    let a = if (joined(home, w, w.x1, w.y1)) e else 0.0, let b = if (joined(home, w, w.x2, w.y2)) e else 0.0,
    {x1: w.x1 - u.x * a, y1: w.y1 - u.y * a, x2: w.x2 + u.x * b, y2: w.y2 + u.y * b, from: 0.0 - a, to: wall_len(w) + b})

// the doors and windows in a wall, in order along it
pub fn openings_on(home, wid) array => sort([for (it in home.items where it.wall == wid) it], (it) => it.at) ^ { [] }
// the solid stretches of a wall between its openings, as [from, to] along it
pub fn solid_spans(home, w) => (let ends = wall_ends(home, w), let ops = openings_on(home, w.id),
    let cuts = [ends.from, *[for (o in ops) *[o.at - o.w / 2.0, o.at + o.w / 2.0]], ends.to],
    [for (i in 0 to len(ops) where cuts[i * 2 + 1] - cuts[i * 2] > .5) [cuts[i * 2], cuts[i * 2 + 1]]])

// ---- items --------------------------------------------------------------------------

pub fn piece(id, key, x, y, rot = 0.0, more = {}) => (let c = C.item_of(key),
    {id: id, key: key, x: x, y: y, rot: rot, w: c.w, d: c.d, h: c.h, elev: c.elev, color: c.color, *:more})
// a door or window takes its place, angle and depth from its wall, and stays inside it
pub fn seat(home, it) => if (it.wall == null) it else (let w = wall_of(home, it.wall),
    if (w == null) it else (let l = wall_len(w), let u = wall_dir(w),
        let at = clamp(it.at, min(it.w / 2.0, l / 2.0), max(l - it.w / 2.0, l / 2.0)),
        {*:it, at: at, x: w.x1 + u.x * at, y: w.y1 + u.y * at, rot: wall_angle(w), d: w.thick}))
fn reseat(home) => {*:home, items: [for (it in home.items) seat(home, it)]}

pub fn add_item(home, key, x, y, rot = 0.0) => (let f = fresh(home, "i"),
    {home: {*:f.home, items: [*home.items, piece(f.id, key, x, y, rot)]}, id: f.id})
pub fn add_opening(home, key, wid, at) => (let f = fresh(home, "i"),
    let it = seat(home, piece(f.id, key, 0.0, 0.0, 0.0, {wall: wid, at: at, flip: false})),
    {home: {*:f.home, items: [*home.items, it]}, id: f.id})
pub fn update_item(home, id, patch) => {*:home, items: [for (it in home.items) if (it.id == id) seat(home, {*:it, *:patch}) else it]}
pub fn update_room(home, id, patch) => {*:home, rooms: [for (r in home.rooms) if (r.id == id) {*:r, *:patch} else r]}
pub fn update_wall(home, id, patch) => reseat({*:home, walls: [for (w in home.walls) if (w.id == id) {*:w, *:patch} else w]})

// removing a wall takes its doors and windows with it
pub fn remove(home, id) => {*:home, walls: [for (w in home.walls where w.id != id) w], rooms: [for (r in home.rooms where r.id != id) r],
    items: [for (it in home.items where it.id != id and it.wall != id) it]}

// a copy sits beside the original: furniture 30 cm away, an opening further along its wall
pub fn duplicate(home, id) => (let it = item_of(home, id), if (it == null) null else (let f = fresh(home, "i"),
    let copy = if (it.wall != null) {*:it, id: f.id, at: it.at + it.w + 20.0} else {*:it, id: f.id, x: it.x + 30.0, y: it.y + 30.0},
    {home: {*:f.home, items: [*home.items, seat(home, copy)]}, id: f.id}))

// ---- corners ---------------------------------------------------------------------------

// Each move is [old x, old y, new x, new y]; every wall end and room corner at an
// old point goes to its new one, and the doors and windows follow their walls.
fn moved_point(p, moves) => (let hit = [for (m in moves where near(p[0], p[1], m[0], m[1])) m],
    if (len(hit) == 0) p else [hit[0][2], hit[0][3]])
pub fn move_points(home, moves) => reseat({*:home,
    walls: [for (w in home.walls) (let a = moved_point([w.x1, w.y1], moves), let b = moved_point([w.x2, w.y2], moves),
        {*:w, x1: a[0], y1: a[1], x2: b[0], y2: b[1]})],
    rooms: [for (r in home.rooms) {*:r, pts: [for (p in r.pts) moved_point(p, moves)]}]})
// a wall moves sideways, square to itself, taking its corners with it
pub fn shift_wall(home, id, dx, dy) => (let w = wall_of(home, id), if (w == null) home
    else move_points(home, [[w.x1, w.y1, w.x1 + dx, w.y1 + dy], [w.x2, w.y2, w.x2 + dx, w.y2 + dy]]))

pub fn wall_ends_list(home) => [for (w in home.walls, e in [[w.x1, w.y1], [w.x2, w.y2]]) e]
// a drawn point snaps to a nearby wall end, else to the grid
pub fn snap_point(home, x, y, tol) => (let close = [for (e in wall_ends_list(home) where dist(e[0], e[1], x, y) < tol) e],
    if (len(close) > 0) close[0] else [snap(x), snap(y)])
// a wall being drawn keeps to 45° steps and whole grid lengths unless `free`
pub fn steer(ax, ay, x, y, free) => if (free) [x, y]
    else (let a = round(math.atan2(y - ay, x - ax) * DEG / 45.0) * 45.0 / DEG, let l = snap(dist(ax, ay, x, y)),
        [ax + l * math.cos(a), ay + l * math.sin(a)])

pub fn add_wall(home, x1, y1, x2, y2, thick = 15.0) => (let f = fresh(home, "w"),
    {home: {*:f.home, walls: [*home.walls, wall(f.id, x1, y1, x2, y2, thick)]}, id: f.id})

// ---- rooms ---------------------------------------------------------------------------

pub fn area(pts) => abs(sum([for (i, p in pts) (let q = pts[(i + 1) % len(pts)], p[0] * q[1] - q[0] * p[1])])) / 2.0
pub fn centroid(pts) => (let a = sum([for (i, p in pts) (let q = pts[(i + 1) % len(pts)], p[0] * q[1] - q[0] * p[1])]) / 2.0,
    if (abs(a) < 1.0) [sum([for (p in pts) p[0]]) / len(pts), sum([for (p in pts) p[1]]) / len(pts)]
    else [sum([for (i, p in pts) (let q = pts[(i + 1) % len(pts)], (p[0] + q[0]) * (p[0] * q[1] - q[0] * p[1]))]) / (6.0 * a),
        sum([for (i, p in pts) (let q = pts[(i + 1) % len(pts)], (p[1] + q[1]) * (p[0] * q[1] - q[0] * p[1]))]) / (6.0 * a)])
pub fn inside(pts, x, y) => sum([for (i, p in pts) (let q = pts[(i + 1) % len(pts)],
    if ((p[1] > y) != (q[1] > y) and x < (q[0] - p[0]) * (y - p[1]) / (q[1] - p[1]) + p[0]) 1 else 0)]) % 2 == 1
// the smallest room around a point, so a room drawn inside another is found first
pub fn room_at(home, x, y) => (let hits = [for (r in home.rooms where inside(r.pts, x, y)) r],
    if (len(hits) == 0) null else hits[argmin([for (r in hits) area(r.pts)])])

// A room drawn as a rectangle brings its four walls, joined at the corners;
// a side that lies along an existing wall reuses it.
fn has_wall(home, ax, ay, bx, by) => len([for (w in home.walls where (near(w.x1, w.y1, ax, ay) and near(w.x2, w.y2, bx, by)) or
    (near(w.x1, w.y1, bx, by) and near(w.x2, w.y2, ax, ay))) w]) > 0
pub fn add_room(home, x0, y0, x1, y1, title = null, floor = "oak") {
    let pts = [[min(x0, x1), min(y0, y1)], [max(x0, x1), min(y0, y1)], [max(x0, x1), max(y0, y1)], [min(x0, x1), max(y0, y1)]]
    let f = fresh(home, "r")
    let sides = [for (i in 0 to 3, let a = pts[i], let b = pts[(i + 1) % 4] where not has_wall(home, a[0], a[1], b[0], b[1])) [a, b]]
    let walls = [for (i, s in sides) wall("w" ++ string(f.home.seq + i), s[0][0], s[0][1], s[1][0], s[1][1])]
    let named = if (title == null) "Room " ++ string(len(home.rooms) + 1) else title;
    {home: {*:f.home, seq: f.home.seq + len(sides), walls: [*home.walls, *walls],
        rooms: [*home.rooms, {id: f.id, name: named, pts: pts, floor: floor}]}, id: f.id}
}

// ---- the whole home --------------------------------------------------------------------

pub fn bounds(home) => (let pts = [*wall_ends_list(home), *[for (r in home.rooms, p in r.pts) p], *[for (it in home.items) [it.x, it.y]]],
    if (len(pts) == 0) {x0: 0.0, y0: 0.0, x1: 800.0, y1: 600.0}
    else {x0: min([for (p in pts) p[0]]), y0: min([for (p in pts) p[1]]), x1: max([for (p in pts) p[0]]), y1: max([for (p in pts) p[1]])})
pub fn floor_area(home) => sum([for (r in home.rooms) area(r.pts)]) / 10000.0
pub fn furniture(home) => [for (it in home.items where it.wall == null) it]

// a file is a home when it carries walls, rooms and items; anything else is refused
pub fn valid(home) => home is map and home.walls is array and home.rooms is array and home.items is array and home.seq is int
