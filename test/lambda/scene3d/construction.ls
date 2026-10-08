import s: lambda.scene3d
let Vector = s.Vector3
let Scene = s.Scene
let camera = s.camera({id: "main"})
let cube = s.mesh(s.box(), s.material('lambert', {color: "#4c8bf5"}), {id: "cube"})
let scene = s.scene([camera, s.light('ambient'), s.group([cube])], {camera: "main", viewBox: "0 0 640 360"})
let normalized = s.normalize(scene)^;
[scene is Scene, [1.0, 2.0, 3.0] is Vector, s.validate(scene)^,
 name(normalized) == 'scene3d', normalized.camera == "main",
 content(normalized)[0].near == 0.1, content(normalized)[2].scale == [1.0, 1.0, 1.0],
 name(content(content(normalized)[2])[0]) == 'mesh']
