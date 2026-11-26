// Fishing model golden: the cove's layout and flora, the fish roster and their skinned buffers, the angler's
// pose geometry and clips, the sea otter's rig and clips, and the pure game step through rowing, a cast,
// a reel-in, the otter's dives, and a staged bite that the strike assist lands and releases. The step runs at a fixed 1/60 s with no host, so
// it is deterministic; values are rounded so interpreter and JIT tiers print the same text.
import w: ~~.~~.demo.fishing.mod_world
import fish: ~~.~~.demo.fishing.mod_fish
import angler: ~~.~~.demo.fishing.mod_angler
import sim: ~~.~~.demo.fishing.mod_sim
import otter: ~~.~~.demo.fishing.mod_otter

fn r2(x) => round(x * 100.0) / 100.0
fn v2(p) => [for (x in p) r2(x)]
let DT = 1.0 / 60.0
let UP = {*: sim.NO_KEYS, up: true}
fn run(st, seconds, keys) any^ => reduce([st, *[for (i in 1 to int(seconds * 60.0)^) i]], (s, i) any^ => sim.step({*: s, keys: keys}, s.t + DT, DT)^)
// the otter's acts over a run, each listed when it starts
fn acts(st, seconds, keys) any^ => reduce([[st, [st.otter.act]], *[for (i in 1 to int(seconds * 60.0)^) i]], (acc, i) any^ =>
    (let s = sim.step({*: acc[0], keys: keys}, acc[0].t + DT, DT)^, [s, if (s.otter.act != acc[1][len(acc[1]) - 1]) [*acc[1], s.otter.act] else acc[1]]))
fn kinds(st) => [for (k in ["koi", "trout", "perch", "bluegill", "carp", "minnow", "tang", "ray"]) len([for (f in st.fish where f.kind == k) f])]

// ---- the cove
"cove";
{rocks: len(w.ROCKS), emergent: len(w.OBSTACLES), pads: len(w.PADS), flowers: len([for (p in w.PADS where p.flower) p]),
 grass: len(w.GRASS_CLUMPS), tubes: len(w.TUBES), stars: len(w.STARS), urchins: len(w.URCHINS), reeds: len(w.REEDS),
 lotus: len(w.LOTUS), lettuce: len(w.LETTUCE), kelp: len(w.KELP), fans: len(w.FANS), moss: len(w.MOSS_BALLS),
 swords: len(w.SWORDS), carpet: len(w.CARPET),
 centre_depth: r2(w.depth(0.0, 0.0) ^ { -1.0 }), start_shore: r2(w.shore(w.START.x, w.START.z)),
 start_navigable: w.navigable(w.START.x, w.START.z) ^ { false }, bank_navigable: w.navigable(0.0, 16.5) ^ { true }}

// ---- fish
"fish";
let koi = fish.ROSTER[0]
let body = fish.buffer(koi) ^ { null };
{count: len(fish.ROSTER), lengths: [r2(min([for (f in fish.ROSTER) f.length])), r2(max([for (f in fish.ROSTER) f.length]))],
 vertices: len(body.positions) div 3, triangles: len(body.indices) div 3, weights_sum: r2(sum(body.weights) / (len(body.weights) div 4)),
 tracks: len(content(fish.swim_clip(koi) ^ { <'animation-clip'> }))}

// ---- the angler
"angler";
{ready_tip: v2(angler.rod_tip(angler.READY_POSE.rod) ^ { [] }), floating: v2(angler.bobber(angler.wait(0.0)) ^ { [] }),
 blades_at_catch: [for (p in (angler.blades(angler.row(0.0)) ^ { [] })) v2(p)],
 hook_at_show: v2(angler.hook(angler.LIFTED + 0.5) ^ { [] }), released: v2((angler.catch_pose(angler.BACK_IN, 0.6) ^ { {p: []} }).p),
 clips: [for (c in (angler.clips() ^ { [] })) [string(c.id), len(content(c)), r2(c.duration)]]}

// ---- the sea otter: its rig and clips, then an idle dive under the boat and a dive to keep up with hard rowing
"otter";
{bones: len(otter.BONES), parts: len(otter.PARTS),
 clips: [for (c in (otter.clips() ^ { [] })) [string(c.id), len(content(c)), r2(c.duration)]],
 dive_mid: (let r = otter.dive_root(0.5), [r2(r.y), r2(r.pitch), r2(r.roll)]), surfaced: (let r = otter.surface_root(1.0), [r2(r.y), r2(r.pitch), r2(r.roll)])}
let idle = acts(sim.initial() ^ { null }, 22.0, sim.NO_KEYS) ^ { [null, []] }
let hard = acts(sim.initial() ^ { null }, 6.0, UP) ^ { [null, []] };
{idle: idle[1], side: idle[0].otter.side, idle_end: [idle[0].otter.act, r2(idle[0].otter.y)], rowing: hard[1]}
// backing up in open water straight at the otter floating two metres astern: it paddles out of the way,
// never inside the hull's width while the boat passes
let DOWN = {*: sim.NO_KEYS, down: true}
fn in_boat(st) => (let o = st.otter, let b = st.boat, let rx = o.x - b.x, let rz = o.z - b.z,
    [rx * sin(b.h) + rz * cos(b.h), rx * cos(b.h) - rz * sin(b.h)])
fn closest(st, seconds, keys) any^ => reduce([[st, 9.0], *[for (i in 1 to int(seconds * 60.0)^) i]], (acc, i) any^ =>
    (let s = sim.step({*: acc[0], keys: keys}, acc[0].t + DT, DT)^, let p = in_boat(s), [s, if (abs(p[0]) < 1.4) min(acc[1], abs(p[1])) else acc[1]]))
let fresh = sim.initial() ^ { null }
let b0 = {*: fresh.boat, x: 0.0, z: 2.0, h: 3.14159}
let astern = {*: fresh, boat: b0, otter: {*: fresh.otter, x: b0.x - sin(b0.h) * 2.2, z: b0.z - cos(b0.h) * 2.2}}
let backed = acts(astern, 4.0, DOWN) ^ { [null, []] }
let pass = closest(astern, 4.0, DOWN) ^ { [null, 0.0] };
{backing: backed[1], clear_of_hull: pass[1] > 0.7, ended: [backed[0].otter.act, r2(in_boat(backed[0])[1])]}

// ---- the game: row out, glide to a stop, cast, reel in
"row";
let st0 = sim.initial() ^ { null }
let rowed = run(st0, 3.0, UP) ^ { st0 };
{mode: rowed.mode, moved: r2(sqrt((rowed.boat.x - st0.boat.x) ** 2 + (rowed.boat.z - st0.boat.z) ** 2)), speed: r2(rowed.boat.v), kinds: kinds(rowed)}
"cast";
let glided = run(rowed, 3.0, sim.NO_KEYS) ^ { rowed }
let cast = run({*: glided, press: true}, 2.5, sim.NO_KEYS) ^ { glided };
{glided: [glided.mode, r2(glided.boat.v)], mode: cast.mode, bobber: v2(sim.bobber_world(cast) ^ { [] }), hint: sim.hint(cast)}
"reel";
let reeled = run({*: cast, press: true}, 1.2, sim.NO_KEYS) ^ { cast };
{mode: reeled.mode, target: reeled.target}

// ---- a staged bite: the assist strikes, the koi is shown, counted and let go
"catch";
let staged = sim.staged(st0, "bite") ^ { st0 }
let landed = run(staged, 9.0, sim.NO_KEYS) ^ { staged }
let released = [for (f in landed.fish where f.id == staged.target) f];
{staged: [staged.mode, staged.target], mode: landed.mode, catches: landed.catches, best: landed.best, toast: landed.toast,
 fish_mode: if (len(released) > 0) released[0].mode else null, target: landed.target}
