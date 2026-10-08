import s: lambda.scene3d
fn message(scene) => s.normalize(scene) ^ { ^.message }
let camera = s.camera({id: "main"});
[
 contains(message(<div>), "expected scene3d"),
 contains(message(<scene3d>), "camera"),
 contains(message(<scene3d camera: "missing", camera>), "reference"),
 contains(message(<scene3d camera camera>), "duplicate"),
 contains(message(s.scene([s.camera({near: 2.0, far: 1.0})])), "camera"),
 contains(message(s.scene([camera, s.group([], {rotation: [0.0, inf, 0.0]})])), "transform"),
 contains(message(s.scene([camera, s.mesh(s.geometry([0, 0, 0, 1, 0, 0, 0, 1, 0], {indices: [0, 1, 4]}), s.material())])), "index"),
 contains(message(s.scene([camera, s.mesh(<geometry type: 'unknown'>, s.material())])), "geometry"),
 contains(message(s.scene([camera, s.mesh(s.box(), s.material('unknown'))])), "material"),
 contains(message(s.scene([camera, <mesh geometry: "missing", material: "missing">])), "reference"),
 contains(message(s.scene([camera, <p "HTML is a sibling">])), "child"),
 contains(message(s.scene([camera, s.mesh(s.box(), s.material('basic', {opacity: 2.0}))])), "opacity")
]
