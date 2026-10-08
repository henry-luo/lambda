import s: lambda.scene3d
import a: lambda.scene3d.animation

let motion = s.track("cube.position", [0.0, 1.0, 2.0], [-1.0, 0.0, 0.0, 1.0, 0.0, 0.0, -1.0, 0.0, 0.0], 'vector')
let playback = s.clip("travel", [motion], {autoplay: true})
let page = s.scene([s.camera(), s.mesh(s.box(), s.material(), {id: "cube"}), playback])
let checked = s.validate(page)^;
[
    checked,
    name(playback) == 'animation-clip',
    playback.id == "travel",
    a.valid_track(motion),
    a.valid_track(s.track("cube.visible", [0.0, 1.0], [true, false], 'bool')),
    a.valid_track(s.track("cube.name", [0.0, 1.0], ["first", "last"], 'string')),
    not a.valid_track(s.track("cube.position", [1.0, 0.0], [0.0, 0.0, 0.0, 1.0, 1.0, 1.0], 'vector')),
    not a.valid_track(s.track("cube.position", [0.0, 1.0], [0.0, 1.0, 2.0], 'vector')),
    not a.valid_track(s.track("cube.visible", [0.0, 1.0], [true, false], 'bool', {interpolation: 'smooth'}))
]
