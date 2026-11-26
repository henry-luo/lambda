// Fishing model golden: the cove's layout and flora, the fish roster and their skinned buffers, the angler's
// pose geometry and clips, the sea otter's rig and clips, and the pure game step through rowing, a cast,
// a reel-in, the otter's dives, and a staged bite that the strike assist lands and releases. The step runs at a fixed 1/60 s with no host, so
// it is deterministic; values are rounded so interpreter and JIT tiers print the same text.
import w: ~~.~~.demo.fishing.mod_world
import fish: ~~.~~.demo.fishing.mod_fish
import angler: ~~.~~.demo.fishing.mod_angler
import sim: ~~.~~.demo.fishing.mod_sim
import otter: ~~.~~.demo.fishing.mod_otter
import boat: ~~.~~.demo.fishing.mod_boat
import scene: ~~.~~.demo.fishing.mod_scene
import s: lambda.scene3d

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
// the whole scene as built, its particle emitters included, passes the scene3d package validator
"scene";
(let built = scene.scene(sim.initial() ^ { null }, {}) ^ { null }, {valid: s.validate(built) ^ { ^.message },
    emitters: len([for (e in scene.EMITTERS) e])})

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
// the otter in boat space: [across, along]
fn in_boat(st) any^ => sim.to_boat(st.boat, st.otter.x, st.otter.z)^
fn closest(st, seconds, keys) any^ => reduce([[st, 9.0], *[for (i in 1 to int(seconds * 60.0)^) i]], (acc, i) any^ =>
    (let s = sim.step({*: acc[0], keys: keys}, acc[0].t + DT, DT)^, let p = in_boat(s)^, [s, if (abs(p[1]) < 1.4) min(acc[1], abs(p[0])) else acc[1]]))
let fresh = sim.initial() ^ { null }
let b0 = {*: fresh.boat, x: 0.0, z: 2.0, h: 3.14159}
let astern = {*: fresh, boat: b0, otter: {*: fresh.otter, x: b0.x - sin(b0.h) * 2.2, z: b0.z - cos(b0.h) * 2.2}}
let backed = acts(astern, 4.0, DOWN) ^ { [null, []] }
let pass = closest(astern, 4.0, DOWN) ^ { [null, 0.0] };
{backing: backed[1], clear_of_hull: pass[1] > 0.7, ended: [backed[0].otter.act, r2((in_boat(backed[0]) ^ { [0.0, 0.0] })[0])]}
// the oars out with the otter alongside: turning on the spot each way, backing water, and setting off after
// it has drifted under a resting oar. The nearest a working blade comes to it afloat stays over a metre
let LEFT = {*: sim.NO_KEYS, left: true}
let RIGHT = {*: sim.NO_KEYS, right: true}
fn nearest_blade(s) any^ => if (s.mode == "row" and s.otter.y > -0.2) (let ph = (s.t - s.since) / angler.ROW_PERIOD,
    min(for (p in angler.blades(angler.row(ph))^) (let q = sim.to_world(s.boat, p)^, sqrt((q[0] - s.otter.x) ** 2 + (q[2] - s.otter.z) ** 2)))) else 9.0
fn blade_gap(st, spans) any^ => reduce([[st, 9.0], *[for (sp in spans, i in 1 to int(sp[1] * 60.0)^) sp[0]]], (acc, keys) any^ =>
    (let s = sim.step({*: acc[0], keys: keys}, acc[0].t + DT, DT)^, [s, min(acc[1], nearest_blade(s)^)]))[1]
let under = sim.to_world(fresh.boat, [fresh.otter.side * 1.9, 0.0, 0.3]) ^ { [0.0, 0.0, 0.0] }
let under_oar = {*: fresh, otter: {*: fresh.otter, x: under[0], z: under[2]}};
{oars_clear: [for (g in [blade_gap(fresh, [[LEFT, 5.0]])^, blade_gap(fresh, [[RIGHT, 5.0]])^, blade_gap(fresh, [[DOWN, 4.0]])^,
    blade_gap(under_oar, [[sim.NO_KEYS, 1.5], [RIGHT, 3.0]])^]) g > 1.0]}

// ---- the jetty: backing and turning into it from the start, no point of the hull's outline ever gets onto
// the deck, nor does a blade, and the boat can still row away
"jetty";
fn into_deck(q) => min(w.JETTY.width / 2.0 - abs(q[0] - w.JETTY.x), min(q[2] - w.JETTY.z1, w.JETTY.z0 - q[2]))
fn deepest(s) any^ => (let pose = if (s.mode == "row") angler.row((s.t - s.since) / angler.ROW_PERIOD) else angler.row(0.0),
    max([*[for (p in boat.PLANFORM) into_deck(sim.to_world(s.boat, [p[0], 0.0, p[1]])^)], *[for (p in angler.blades(pose)^) into_deck(sim.to_world(s.boat, p)^)]]))
fn intrusion(st, spans) any^ => reduce([[st, -9.0], *[for (sp in spans, i in 1 to int(sp[1] * 60.0)^) sp[0]]], (acc, keys) any^ =>
    (let s = sim.step({*: acc[0], keys: keys}, acc[0].t + DT, DT)^, [s, max(acc[1], deepest(s)^)]))
let DOWN_RIGHT = {*: sim.NO_KEYS, down: true, right: true}
let in_back = intrusion(fresh, [[DOWN_RIGHT, 1.5], [DOWN, 6.0]]) ^ { [fresh, 9.0] }
let in_turn = intrusion(fresh, [[LEFT, 3.0], [UP, 6.0]]) ^ { [fresh, 9.0] }
let away = run(in_back[0], 3.0, UP) ^ { in_back[0] };
{clear: [in_back[1] < 0.0, in_turn[1] < 0.0], rows_away: sqrt((away.boat.x - in_back[0].boat.x) ** 2 + (away.boat.z - in_back[0].boat.z) ** 2) > 1.0}

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
// a press while the fish only nibbles is too soon: the line stays out and the fish stays. The float then
// goes under, and a strike a second and a half into the bite still hooks it
let unassisted = {*: staged, assist: false}
let nibbled = run(unassisted, 0.3, sim.NO_KEYS) ^ { unassisted }
let early = sim.step({*: nibbled, press: true}, nibbled.t + DT, DT) ^ { nibbled }
let bitten = run(early, 2.1, sim.NO_KEYS) ^ { early }
let struck = sim.step({*: bitten, press: true}, bitten.t + DT, DT) ^ { bitten };
{early: [early.mode, early.target == staged.target, early.toast], bitten: [bitten.mode, r2(bitten.t - bitten.since)], struck: struck.mode}
