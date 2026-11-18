// Midtown model golden: the procedural city, the car's dynamics, lane-keeping traffic, startled
// pedestrians, the courier cycle, camera rigs, the assembled native scene, and the open road beyond
// the city (land, loop road, bridges, tunnel, open-road traffic). Values are rounded so interpreter
// and JIT tiers print the same text.
import s: lambda.scene3d
import city: ~~.~~.demo.midtown.mod_city
import v: ~~.~~.demo.midtown.mod_vehicle
import tr: ~~.~~.demo.midtown.mod_traffic
import pd: ~~.~~.demo.midtown.mod_peds
import ms: ~~.~~.demo.midtown.mod_mission
import cam: ~~.~~.demo.midtown.mod_camera
import sc: ~~.~~.demo.midtown.mod_scene
import present: ~~.~~.demo.midtown.mod_present
import u: ~~.~~.demo.midtown.mod_util
import world: ~~.~~.demo.midtown.mod_world
import roads: ~~.~~.demo.midtown.mod_roads
import land: ~~.~~.demo.midtown.mod_land
import rt: ~~.~~.demo.midtown.mod_route
import scenery: ~~.~~.demo.midtown.mod_scenery

fn r2(x) => round(x * 100.0) / 100.0
fn drive_ticks(car, controls, ticks) any^ => reduce([car, *[for (i in 1 to ticks) i]], (c, i) any^ => v.step(c, controls, 1.0 / 60.0))
fn drive(car, controls, seconds) any^ => drive_ticks(car, controls, int(seconds * 60.0)^)
let GAS = {*: v.IDLE, throttle: 1.0}

// ---- the city
"city";
{blocks: len(city.BLOCKS), buildings: len(city.BUILDINGS), lamps: len(city.LAMPS), trees: len(city.TREES),
 signals: len(city.SIGNALS), parked: len(city.PARKED), props: len(city.PROPS), obstacles: len(city.OBSTACLES),
 tallest: r2(max([for (b in city.BUILDINGS) b.h])), styles: [for (k in 0 to 3) len([for (b in city.BUILDINGS where b.style == k) b])],
 ground: [city.ground(0.0, 0.0), city.ground(20.0, 20.0), r2(city.ground(city.RAMPS[0].x, city.RAMPS[0].z))],
 places: [city.district(10.0, 10.0), city.district(-160.0, 160.0), city.district(0.0, 210.0), city.street(3.0, 40.0), city.street(1.0, 2.0)],
 near: [len(city.near_obstacles(0.0, 0.0, 5.0)), len(city.near_obstacles(-160.0, -150.0, 5.0))]}

// ---- the car: launch, top of third, braking, cornering, a wall and a ramp
"vehicle";
let lane = v.spawn(city.road(3) - city.LANE, city.road(0) + 8.0, 0.0)
let fast = drive(lane, GAS, 5.0)
let stopped = drive(fast, {*: v.IDLE, brake: 1.0}, 3.0)
let turning = drive(drive(lane, GAS, 2.5), {*: GAS, steer: 1.0}, 1.0)
let tower = [for (b in city.BUILDINGS where b.kind == "T") b][0]
let crash = drive(v.spawn((tower.x0 + tower.x1) / 2.0, tower.z0 - 30.0, 0.0), GAS, 4.0)
let ramp = city.RAMPS[0]
let run_up = v.spawn(ramp.x - sin(ramp.heading) * 32.0, ramp.z - cos(ramp.heading) * 32.0, ramp.heading)
// Every flight sample shares the same fixed-step prefix. Keep the original
// seconds-to-tick rounding for each sample when advancing from its predecessor.
let flight_ticks = [for (k in 0 to 40) int((k * 0.1) * 60.0)^]
let flight = reduce([[run_up], *[for (k in 1 to 40) k]], (samples, k) any^ =>
    [*samples, drive_ticks(samples[k - 1], GAS, flight_ticks[k] - flight_ticks[k - 1])^]);
{launch: [r2(v.speed(fast)), fast.gear, v.gear_label(fast)], braked: [r2(v.speed(stopped)), r2(stopped.z - fast.z)],
 turn: [r2(turning.heading), r2(v.speed(turning))],
 crash: [r2(crash.z + v.HALF_LENGTH - tower.z0), r2(v.speed(crash)) < 1.0, crash.damage > 10.0],
 jump: [r2(max([for (c in flight) c.y])), len([for (c in flight where not c.grounded) c]) > 4, flight[40].grounded]}

// ---- traffic: a simulated minute, never off the road and never inside another car
"traffic";
let player = {id: "player", player: true, x: 0.0, z: 900.0, heading: 0.0, hl: 2.25}
fn overlaps(cars) any^ => len([for (i, a in cars, j, b in cars where i < j and u.dist(a.x, a.z, b.x, b.z)^ < 3.6) i])
let minute = reduce([{cars: tr.spawn(16), t: 0.0, worst: 0, off: 0}, *[for (i in 1 to 1800) i]], (sim, i) any^ =>
    (let cars = tr.step(sim.cars, player, sim.t, 1.0 / 30.0),
     {cars: cars, t: sim.t + 1.0 / 30.0, worst: max(sim.worst, overlaps(cars)), off: sim.off + len([for (a in cars where not city.on_road(a.x, a.z)^) a])}))
let snapped = tr.nearest_run(-1.0, -100.0, 0.1, 5.0);
{cars: len(minute.cars), overlaps: minute.worst, off_road: minute.off, trips: sum([for (a in minute.cars) a.trips]) > 40,
 lights: [tr.light(0, 3.0), tr.light(0, 11.0), tr.light(0, 20.0), tr.light(1, 3.0), tr.light(1, 15.0)],
 nearest: [snapped.i, snapped.j, snapped.d, r2(snapped.s), r2(snapped.x), r2(snapped.z)]}

// ---- a pedestrian hops clear of a car bearing down on them, then steps back
"pedestrians";
let crowd = pd.spawn(24)
let walker = crowd[0]
let charging = {x: walker.x - 9.0 * sin(walker.heading), z: walker.z - 9.0 * cos(walker.heading), heading: walker.heading,
    vx: 15.0 * sin(walker.heading), vz: 15.0 * cos(walker.heading)}
let startled = reduce([crowd, *[for (i in 1 to 30) i]], (ps, i) any^ => pd.step(ps, charging, 1.0 / 60.0))
let calm = reduce([startled, *[for (i in 1 to 300) i]], (ps, i) any^ => pd.step(ps, {*: charging, vx: 0.0, vz: 0.0, z: 900.0}, 1.0 / 60.0));
{crowd: len(crowd), hop: r2(sqrt(u.sq(startled[0].ox) + u.sq(startled[0].oz))), back: r2(abs(calm[0].ox) + abs(calm[0].oz)),
 on_sidewalks: len([for (p in calm where p.y >= city.CURB) p])}

// ---- courier: collect, deliver with time to spare, then run out of time
"courier";
let job = ms.begin(ms.initial(), lane)
let pickup_car = {*: lane, x: job.target.x, z: job.target.z}
let carrying = ms.step(job, pickup_car, 10.0)
let paid = ms.step(carrying, {*: lane, x: carrying.target.x, z: carrying.target.z}, 20.0)
let late = ms.step(ms.step(paid, {*: lane, x: paid.target.x, z: paid.target.z}, 30.0), lane, 400.0);
{pickup: [job.phase, u.dist(lane.x, lane.z, job.target.x, job.target.z) >= 110.0], carrying: [carrying.phase, carrying.limit > 14.0],
 paid: [paid.phase, paid.cash > 60, paid.streak, paid.delivered], late: [late.phase, late.cash == paid.cash, late.streak],
 stops: len(ms.STOPS)}

// ---- cameras: a look rotation, a rig that settles behind the car
"camera";
let look = cam.look([0.0, 0.0, 0.0], [0.0, 0.0, -10.0])
let rig = reduce([cam.initial(fast), *[for (i in 1 to 120) i]], (c, i) any^ => cam.follow(c, fast, 1.0 / 60.0));
{identity: [for (x in look) r2(x)], turned: [for (x in cam.look([0.0, 0.0, 0.0], [10.0, 0.0, 0.0])^) r2(x)],
 behind: r2(rig.position[2] - fast.z) < 0.0, modes: [for (m in cam.MODES) cam.next_mode(m)]}

// ---- the native scene: one description, valid, with every moving thing a body
"scene";
let st = present.initial()
let scene = sc.scene(st)
fn walk(n) any^ => if (n is element) [n, *[for (c in content(n)^) *walk(c)]] else []
let nodes = walk(scene);
{valid: s.validate(scene) ^ { false }, bodies: len([for (n in nodes where name(n)^ == 'rigid-body') n]),
 clips: [for (n in nodes where name(n)^ == 'animation-clip') n.id], commands: len(present.commands(st, st.camera.position)),
 looks: [for (l in sc.LOOK_ORDER) len(sc.look_edits(l, true))], next_look: [for (l in sc.LOOK_ORDER) sc.next_look(l)]}

// ---- speed governor: lag holds the speed, deeper lag eases it down to a floor, a caught-up scene lifts it
"governor";
let DT = 1.0 / 60.0
let eased = reduce([30.0, *[for (i in 1 to 60) i]], (cap, i) any^ => present.govern(cap, 0.8, 30.0, DT))
let cruising = {*: lane, vx: 0.0, vz: 25.0}
let pedals = {*: v.IDLE, throttle: 1.0};
{free: present.govern(null, 0.05, 30.0, DT), held: present.govern(null, 0.3, 30.0, DT), ratchet: present.govern(30.0, 0.3, 20.0, DT),
 floor: present.govern(null, 0.3, 3.0, DT), kept: present.govern(20.0, 0.1, 25.0, DT), eased: r2(eased),
 lifting: present.govern(20.0, 0.05, 20.0, 0.5), lifted: present.govern(48.0, 0.05, 48.0, 0.5),
 over: present.capped(pedals, 20.0, cruising), under: present.capped(pedals, 30.0, cruising) == pedals, open: present.capped(pedals, null, cruising) == pedals}

// ---- rear indicators: the lamp on the side the wheels point toward blinks; at speed a smaller angle is a full turn
"indicators";
let steered = {*: lane, steer: 0.3};
{left: present.signal(steered, 0.1), right: present.signal({*: steered, steer: -0.3}, 0.1), off_phase: present.signal(steered, 0.5),
 straight: present.signal(lane, 0.1), at_speed: present.signal({*: lane, vz: 45.0, steer: 0.05}, 0.1),
 drift: present.signal({*: lane, vz: 45.0, steer: 0.03}, 0.1)}

// ---- the open road: the land and the loop road, its bridges and tunnel, driving across and through them,
// a minute of open-road traffic, Auto drive round the loop, and a car fished out of the river
"open road";
let LOOP = roads.LOOP.samples
fn structure(kind) any^ => [for (st in roads.STRUCTURES where st.kind == kind) st][0]
fn lane_start(st) any^ => (let p = LOOP[st.start_k], let q = LOOP[st.end_k], let h = u.heading_of(q.x - p.x, q.z - p.z), let r = u.right(h),
    v.spawn(p.x + r[0] * roads.LANE, p.z + r[1] * roads.LANE, h))
fn crossing(kind, seconds) any^ => (let st = structure(kind)^, let start = lane_start(st)^, let q = LOOP[st.end_k],
    let path = reduce([[start], *[for (k in 1 to int(seconds * 10.0)^) k]], (acc, k) any^ => [*acc, drive(acc[len(acc) - 1], GAS, 0.1)^]),
    {past_end: u.dist(path[len(path) - 1].x, path[len(path) - 1].z, start.x, start.z)^ > st.b - st.a, hits: max([for (c in path) c.impact]) == 0.0,
     level: max([for (c in path where u.dist(c.x, c.z, start.x, start.z)^ < st.b - st.a - 5.0) abs(c.y - (st.y + (st.y_end - st.y) * u.dist(c.x, c.z, start.x, start.z)^ / (st.b - st.a)))]) < 0.1})
let grades = [for (k in 1 to (len(LOOP)^ - 1), let a = LOOP[k - 1], let b = LOOP[k]) abs(b.y - a.y) / max(0.01, b.s - a.s)]
let steps = [for (k in 1 to (len(LOOP)^ - 1), let a = LOOP[k - 1], let b = LOOP[k]) abs(world.ground(b.x, b.z)^ - world.ground(a.x, a.z)^)]
let open_minute = reduce([{cars: rt.spawn(7), worst: 0}, *[for (i in 1 to 1800) i]], (sim, i) any^ =>
    (let cars = rt.step(sim.cars, player, 1.0 / 30.0)^,
     {cars: cars, worst: max(sim.worst, len([for (i, a in cars, j, b in cars where i < j and u.dist(a.x, a.z, b.x, b.z)^ < 3.6 and abs(a.y - b.y) < 3.0) i]))}))
let toured = (let st0 = {*: present.initial({x: 667.62, z: -196.34, heading: 1.84}), started: true}, let on = present.engage_auto(st0)^,
    reduce([on, *[for (i in 1 to 1200) i]], (st, i) any^ => present.advance(st, 1.0 / 30.0, st.t + 1.0 / 30.0)^))
let fished = (let wet = {*: present.initial({x: 471.0, z: -230.0, heading: 0.0}), started: true}, present.advance(wet, 1.0 / 60.0, 0.1)^);
{loop: [round(roads.LOOP.length), len(LOOP)], structures: [for (st in roads.STRUCTURES) [st.kind, round(st.b - st.a), r2(st.y)]],
 grade_ok: max(grades) <= roads.GRADE + 0.001, deck_exact: max([for (p in LOOP) abs(world.ground(p.x, p.z)^ - p.y)]) < 0.001, steps_ok: max(steps) < 0.35,
 land: [len(land.NATURAL), len(land.FAR), r2(land.natural_at(474.0, -150.0)^), round(land.natural_at(1220.0, -315.0)^), r2(world.ground(310.0, 0.0)^)],
 truss: crossing("truss", 9.0)^, tunnel: crossing("tunnel", 14.0)^,
 traffic: {cars: len(open_minute.cars), overlaps: open_minute.worst, laps: sum([for (a in open_minute.cars) a.laps]) > 0, paths: len(rt.PATHS)},
 auto: [toured.auto.path, world.district(toured.car.x, toured.car.z)^, v.speed(toured.car) > 15.0],
 splash: [fished.car.y > land.SEA, fished.toast],
 scenery: [len(scenery.TREES) > 2000, len(scenery.BUSHES) > 1000, len(scenery.FARMS), len(scenery.TURBINES), len(scenery.BOATS)],
 places: [for (p in [[250.0, 0.0], [310.0, 20.0], [470.0, -146.0], [800.0, -246.0], [1220.0, -315.0], [965.0, 285.0], [480.0, 252.0], [1652.0, 344.0]])
    world.district(p[0], p[1])^ ++ " / " ++ world.street(p[0], p[1])^]}
