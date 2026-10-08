import s: lambda.scene3d
let scene = s.scene([
 s.camera({id: "main"}),
 s.resources([s.plane([1.0, 1.0, 1.0], {id: "quad"}), s.material('basic', {id: "red", color: "#ff0000"})]),
 <mesh geometry: "quad", material: "red", instances: [s.transform([-1.0, 0.0, 0.0])^, s.transform([1.0, 0.0, 0.0])^]>
], {camera: "main"})
let normalized = s.normalize(scene)^;
[s.validate(scene)^, len(content(content(normalized)[1])) == 2, len(content(normalized)[2].instances) == 2]
