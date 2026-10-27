// The Dreamhome planner's pure parts: the catalog, the starter homes, home
// edits with joined corners, floors, the plan and the 3D scene, and picking.
import s: lambda.scene3d
import D: ~~.~~.mod_document
import C: ~~.mod_catalog
import M: ~~.mod_model
import T: ~~.mod_templates
import P: ~~.mod_plan
import S: ~~.mod_scene

fn view_state(home, mode) => {view2d: {cx: 500.0, cy: 400.0, zoom: .5}, tool: "select", sel: null, dims: true, mode3d: mode,
    walker: {x: 450.0, y: 650.0, yaw: -90.0, pitch: 0.0}, orbit: {*:S.framing(home), yaw: .65, pitch: .82}, cutaway: false, evening: false}
// parts may reach past the piece's box (a handle, a mirror above a vanity) but never turn inside out
fn has_volume(p) => p.u1 > p.u0 and p.v1 > p.v0 and p.t1 > p.t0 and p.t0 >= 0.0

"catalog:";
{pieces: len(C.ITEMS), keys_unique: len(unique([for (it in C.ITEMS) it.key])) == len(C.ITEMS),
 per_category: [for (c in C.CATEGORIES) [c.key, len([for (it in C.ITEMS where it.cat == c.key) it])]],
 openings: [for (it in C.ITEMS where it.opening != null) it.opening],
 lights: [for (it in C.ITEMS where it.light != null) it.key],
 parts_have_volume: all([for (it in C.ITEMS, p in it.parts) has_volume(p)]),
 floors: len(C.FLOORS), unknown_floor_falls_back: C.floor_of("lava").key};

"starter homes:";
[for (t in T.TEMPLATES) {key: t.key, walls: len(t.home.walls), rooms: len(t.home.rooms), items: len(t.home.items),
    area: M.floor_area(t.home), next_id_free: M.thing_of(t.home, "i" ++ string(t.home.seq)) == null,
    openings_inside_walls: all([for (o in t.home.items where o.wall != null, let w = M.wall_of(t.home, o.wall))
        o.at - o.w / 2.0 >= -0.01 and o.at + o.w / 2.0 <= M.wall_len(w) + 0.01])}];

let apt = T.template_of("apartment").home;
"openings take their wall's angle and thickness:";
[for (o in apt.items where o.wall != null) [o.key, o.wall, o.x, o.y, o.rot, o.d]];

"a room brings its walls and reuses a shared side:";
let one = M.add_room(M.blank(), 0.0, 0.0, 400.0, 300.0, "Living");
let two = M.add_room(one.home, 400.0, 0.0, 700.0, 300.0);
{first: [one.id, len(one.home.walls)], second: [two.id, len(two.home.walls), two.home.rooms[1].name], seq: two.home.seq,
 areas: [for (r in two.home.rooms) M.area(r.pts) / 10000.0], centroid: M.centroid(two.home.rooms[0].pts),
 room_at: M.room_at(two.home, 500.0, 100.0).name, outside: M.room_at(two.home, 900.0, 100.0)};

"moving a wall takes its corners, the rooms on them and its doors:";
let door = M.add_opening(two.home, "door", "w3", 150.0);
let shifted = M.shift_wall(door.home, "w3", 50.0, 0.0);
{wall: [for (w in shifted.walls where w.id == "w3" or w.id == "w2" or w.id == "w4") [w.id, w.x1, w.y1, w.x2, w.y2]],
 rooms: [for (r in shifted.rooms) r.pts], door: [M.item_of(shifted, door.id).x, M.item_of(shifted, door.id).y, M.item_of(shifted, door.id).at]};
"a door clamps inside its wall; removing the wall removes the door:";
{clamped: M.item_of(M.update_item(door.home, door.id, {at: 5.0}), door.id).at,
 removed: len(M.remove(door.home, "w3").items), kept_walls: len(M.remove(door.home, "w3").walls)};
"solid stretches either side of the door, ends reaching into the corners:";
{spans: M.solid_spans(door.home, M.wall_of(door.home, "w3")), ends: M.wall_ends(door.home, M.wall_of(door.home, "w3"))};

"pieces, copies and lookups:";
let sofa = M.add_item(door.home, "sofa", 200.0, 150.0, 90.0);
let copy = M.duplicate(sofa.home, sofa.id);
{sofa: M.item_of(sofa.home, sofa.id), copy_at: [M.item_of(copy.home, copy.id).x, M.item_of(copy.home, copy.id).y],
 kinds: [M.kind_of("w3"), M.kind_of("r1"), M.kind_of(sofa.id), M.kind_of(null)],
 nearest: M.nearest_wall(sofa.home, 390.0, 120.0).wall.id, furniture: len(M.furniture(sofa.home))};

"drawing snaps to wall ends and 45° steps:";
{snapped: M.snap_point(sofa.home, 404.0, 6.0, 20.0), grid: M.snap_point(sofa.home, 123.0, 456.0, 20.0),
 steered: M.steer(0.0, 0.0, 300.0, 40.0, false), diagonal: [for (v in M.steer(0.0, 0.0, 200.0, 180.0, false)) round(v)], free: M.steer(0.0, 0.0, 30.0, 4.0, true)};

"floors triangulate, wound to face up:";
let ell = [[0.0, 0.0], [550.0, 0.0], [550.0, 300.0], [800.0, 300.0], [800.0, 550.0], [0.0, 550.0]];
let tris = S.floor_triangles(ell);
{triangles: len(tris), area_kept: abs(sum([for (t in tris) M.area(t)]) - M.area(ell)) < 0.01,
 reversed_too: len(S.floor_triangles(reverse(ell)))};

"history:";
let h1 = D.record(D.history(), apt);
let back = D.undo_step(h1, sofa.home);
{undo_restores: back.value == apt, redo_returns: D.redo_step(back.history, back.value).value == sofa.home, slug: D.slug(apt.title, "home")};

"the plan:";
let plan = P.plan(apt, view_state(apt, "orbit"), 800.0, 500.0);
{tag: name(plan), viewBox: plan.viewBox, length_ok: len(format(plan, 'xml')) > 20000};

"the 3D scene is a valid scene3d, cut away or lit for the evening:";
let st = view_state(apt, "orbit");
let sc = S.scene(apt, st, 800, 450);
{valid: s.validate(sc) ^ { string(^.message) }, cut: s.validate(S.scene(apt, {*:st, cutaway: true, evening: true}, 800, 450)) ^ { string(^.message) },
 walk: s.validate(S.scene(apt, {*:st, mode3d: "walk"}, 800, 450)) ^ { string(^.message) },
 // the renderer shades at most eight lights besides the ambient one: the sun and seven lamps
 lights: len([for (n in content(sc) where n is element and name(n) == 'light' and n.type != 'ambient') n]),
 glb: type(format(sc, 'glb'))};

"PBR finishes, room reflections and window sunlight:";
let resources = [for (n in content(sc) where n is element and name(n) == 'resources') n][0];
let materials = [for (n in content(resources) where n is element and name(n) == 'material') n];
let sun = [for (n in content(sc) where n is element and name(n) == 'light' and n.type == 'directional') n][0];
{standard: all([for (n in materials) n.type == 'standard']), tone: sc.'tone-mapping', environment: sc.environment,
 metal: any([for (n in materials) n.metalness == 1.0 and n.roughness == .22]),
 glass: any([for (n in materials) n.opacity == .25 and n.transparent == true and n.roughness == .08]),
 wood: any([for (n in materials) n.roughness == .48]), stone: any([for (n in materials) n.roughness == .32]),
 sun: [sun.'cast-shadow', sun.'shadow-map-size', sun.'shadow-normal-bias'],
 room: len([for (n in content(resources) where n is element and name(n) == 'environment' and n.id == sc.environment) n]) == 1};

"picking in 3D: straight down on the double bed, then the bedroom floor beside it:";
let bed = [for (it in apt.items where it.key == "double-bed") it][0];
let above = {*:st, orbit: {yaw: 0.0, pitch: 1.5, dist: 6.0, tx: bed.x / 100.0, tz: bed.y / 100.0}};
{bed: S.pick(apt, above, 800, 450, 400.0, 225.0) == bed.id,
 floor: S.pick(apt, {*:above, orbit: {*:above.orbit, tx: 7.2, tz: 3.6}}, 800, 450, 400.0, 225.0),
 sky: S.pick(apt, {*:st, orbit: {*:st.orbit, pitch: .1}}, 800, 450, 400.0, 2.0)}
