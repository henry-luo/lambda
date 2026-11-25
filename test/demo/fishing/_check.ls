import s: lambda.scene3d
import physics: lambda.physics
import lagoon: .mod_lagoon
import otter: .mod_otter
fn wrap(kids) any^ => s.scene([physics.world({}), s.camera({id: "cam"}), s.resources([*(lagoon.materials() ^ { [] }), *(otter.materials() ^ { [] })]), *kids], {camera: "cam"})
fn check(label, kids) any^ => (let r = if (kids is error) kids else s.validate(wrap(kids)^), label ++ ": " ++ (if (r is error) string(r.message) else string(r)))
let els = lagoon.elements() ^ { [] };
[*[for (e in els) check(string(e.id), [e])^], check("otter", [otter.body(0.0, -0.05, 0.0, 0.0) ^ { ~ }, *(otter.clips() ^ { [] })])^]
