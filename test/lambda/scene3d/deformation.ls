import s: lambda.scene3d
let rig = <skeleton id: "rig", <bone id: "root", <bone id: "tip", position: [0, 1, 0]>>>
let geometry = s.geometry([0, 0, 0, 1, 0, 0, 0, 1, 0], {
    'skin-indices': [0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0],
    'skin-weights': [1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0],
    'morph-positions': [0, 0, 0, 0, 0, 0, 1, 0, 0]
})
let ribbon = s.mesh(geometry, s.material(), {id: "ribbon", skeleton: "rig", 'morph-weights': [0]})
let movement = s.clip("bend", [
    s.track("tip.quaternion", [0, 1], [0, 0, 0, 1, 0, 0, 1, 0], 'quaternion'),
    s.track("ribbon.morph-weights", [0, 1], [0, 1], 'vector')
])
let checked = s.validate(s.scene([s.camera(), rig, ribbon, movement]))^;
[
    checked,
    contains((s.validate(s.scene([s.camera(), ribbon, movement])) ^ { ^.message }), "reference"),
    contains((s.validate(s.scene([s.camera(), rig, ribbon, s.clip("bad", [s.track("ribbon.opacity", [0, 1], [0, 1])])])) ^ { ^.message }), "binding")
]
