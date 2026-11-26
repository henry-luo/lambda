// S12.1.1v3 / D9.5.6: particle emitter builders and validation stay pure, and reject what the native emitter rejects.
import s: lambda.scene3d
let disc = s.geometry([-1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 1.0, 0.0, 0.0])
let basic = s.material('basic', {transparent: true})
fn scene(node) any^ => s.scene([s.camera(), node])
fn message(node) any^ => s.validate(scene(node)) ^ { ^.message }
fn emitter(options, controls = []) any => s.particle_emitter(disc, basic, options, controls)
fn curve(kind, times, values) any => s.particle_curve(kind, times, values) ^ { null }
let every_curve = [curve('size', [0.0, 1.0], [0.1, 1.0]), curve('rotation', [0.0, 1.0], [0.0, 0.0, 0.0, 0.0, 6.28, 0.0]),
    curve('color', [0.0, 0.5, 1.0], [1.0, 1.0, 1.0, 1.0, 0.5, 0.0, 0.2, 0.2, 0.2]), curve('opacity', [0.0, 1.0], [1.0, 0.0]),
    curve('velocity', [0.0, 1.0], [0.0, 1.0, 0.0, 0.0, 0.0, 0.0])]
let cpu = emitter({id: "sparks", backend: "cpu", space: "world", shape: "cone", angle: 30.0, capacity: 64, burst: 4, rate: 10.0,
    lifetime: 1.5, 'lifetime-min': 0.5, size: 0.2, gravity: [0.0, -9.81, 0.0], seed: 42, presentation: "mesh"},
    [*every_curve, s.particle_noise({strength: [0.5, 1.0, 0.5], frequency: 2.0}), s.particle_sheet({columns: 4, rows: 2}),
     s.particle_trail({segments: 8, lifetime: 0.3, width: 0.05}), s.particle_collision({radius: 0.05})])
let gpu = emitter({id: "rain", backend: "gpu", capacity: 256},
    [s.particle_collision({radius: 0.02}, [s.particle_plane([0.0, 1.0, 0.0], 0.0), s.particle_sphere([0.0, 1.0, 0.0], 0.5)])])
let by_id = s.scene([s.camera(), s.resources([s.geometry([-1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 1.0, 0.0, 0.0], {id: "g"}), s.material('basic', {id: "m"})]),
    s.particle_emitter("g", "m", {id: "drops"}), s.particle_emitter("g", "m", {id: "skin", shape: "mesh", surface: "g", 'normal-speed': 0.5})]);
[
 s.validate(scene(cpu))^,
 s.validate(scene(gpu))^,
 s.validate(by_id)^,
 s.validate(s.scene([s.camera(), s.group([emitter({id: "nested"})])]))^,
 contains(message(emitter({capacity: 0})), "capacity"),
 contains(message(emitter({backend: "vulkan"})), "backend"),
 contains(message(s.particle_emitter(disc, s.material('standard'))), "basic color"),
 contains(message(emitter({}, [curve('size', [0.2, 1.0], [1.0, 1.0])])), "increasing times"),
 contains(message(emitter({}, [curve('rotation', [0.0, 1.0], [0.0, 0.0, 0.0])])), "matching values"),
 contains(message(emitter({}, [curve('color', [0.0, 1.0], [1.0, 1.0, 1.0, 2.0, 1.0, 1.0])])), "range"),
 contains(message(emitter({}, [curve('size', [0.0, 1.0], [1.0, 1.0]), curve('size', [0.0, 1.0], [1.0, 0.0])])), "at most once"),
 contains(message(emitter({backend: "gpu"}, [s.particle_collision()])), "1..8"),
 contains(message(emitter({}, [s.particle_collision({}, [s.particle_plane()])])), "1..8"),
 contains(message(emitter({shape: "mesh"})), "surface"),
 contains(s.validate(s.scene([s.camera(), s.resources([s.geometry([-1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 1.0, 0.0, 0.0], {id: "g"})]), emitter({surface: "g"})])) ^ { ^.message }, "surface"),
 contains(message(emitter({lifetime: 1.0, 'lifetime-min': 2.0})), "lifetime-min"),
 contains(message(emitter({}, [s.particle_trail({segments: 0})])), "segments"),
 contains(message(emitter({}, [s.light('point')])), "child"),
 contains(message(<'particle-emitter' disc>), "material"),
 (s.particle_curve('spin', [0.0, 1.0], [0.0, 1.0]) ^ { ^.message }) == "scene3d: unknown particle curve kind"
]
