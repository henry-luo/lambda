// Comparisons of converted characters with the reference poses that
// test/demo/scene3d/assets/motion/_oracle.mjs writes: shared by motion_oracle.ls
// and the local-only Mixamo check. Every position is in character metres.
import motion: lambda.scene3d.motion
import t: lambda.scene3d.transform

// 0.01 mm: both references compute in float32 in places
let close = 0.00001

fn origin(m) => [m[12], m[13], m[14]];
pub fn gap(a, b) => max(abs(a[0] - b[0]), max(abs(a[1] - b[1]), abs(a[2] - b[2])));
// Three.js drops characters such as ':' from node names
fn sanitized(n) any^ => replace(replace(n, ":", ""), " ", "_");
fn key_at(k, seconds) => [for (i in ((0 to len(k.times) - 1) ^ { [] }) where abs(k.times[i] - seconds) < 0.000001) i][0];
pub fn world_at(c, clip_name, seconds) any^ {
    let k = if (clip_name == null) null else [for (x in motion.clips_of(c) where x.name == clip_name) x][0];
    if (k == null) motion.pose(c)^ else motion.pose(c, clip_name, key_at(k, seconds))^
}
// the largest joint difference over the joints the reference names, or null when it names none
pub fn joint_drift(c, clip_name, seconds, want, renamed) {
    let world = world_at(c, clip_name, seconds)
    let gaps = [for (j in motion.joints(c)) for (w in [want[if (renamed) sanitized(j.name) else j.name]] where w != null)
        gap(origin(world[j.name]), w)];
    if (len(gaps) == 0) null else max(gaps)
}
// references key skinned vertices by their bind position in 10 µm units (dense
// meshes place distinct vertices under a millimetre apart); allow a rounding step
fn unit(x) => int(round(x * 100000)) ^ { 0 };
fn vertex_key(x, y, z) => string(x) ++ "," ++ string(y) ++ "," ++ string(z);
fn lookup(want, p) {
    let x = unit(p[0]), y = unit(p[1]), z = unit(p[2])
    let exact = want[vertex_key(x, y, z)];
    if (exact != null) exact
    else [for (dx in [-1, 0, 1]) for (dy in [-1, 0, 1]) for (dz in [-1, 0, 1])
        for (hit in [want[vertex_key(x + dx, y + dy, z + dz)]] where hit != null) hit][0]
}
// Bind correction depends on the joint and pose, so share it across vertices.
fn skin_matrices(c, clip_name, seconds) array^ {
    let world = world_at(c, clip_name, seconds)^;
    [for (j in motion.joints(c))
        if (j.inverse_bind == null) null else t.multiply(world[j.name], j.inverse_bind)^]
}
// [vertices compared, all within tolerance] over every `stride`th vertex; vertices
// with more than four influences are not referenced
pub fn skin_drift(c, clip_name, seconds, want, stride) any^ {
    let matrices = skin_matrices(c, clip_name, seconds)^
    let gs = [for (m in ((content(motion.body_of(c))) ^ { [] }) where ((name(m) == 'mesh') ^ { false })) for (g in ((content(m)) ^ { [] }) where ((name(g) == 'geometry' and g["skin-indices"] != null) ^ { false })) g]
    let gaps = [for (g in gs) for (v in ((0 to len(g.positions) / 3 - 1) ^ { [] }) where v % stride == 0)
        for (p in [[g.positions[3 * v], g.positions[3 * v + 1], g.positions[3 * v + 2]]])
        for (w in [lookup(want, p)] where w != null)
            (let parts = [for (s in 0 to 3 where g["skin-weights"][4 * v + s] > 0)
                t.point(matrices[int(g["skin-indices"][4 * v + s])], p)^ * g["skin-weights"][4 * v + s]],
             gap([sum([for (x in parts) x[0]]), sum([for (x in parts) x[1]]), sum([for (x in parts) x[2]])], w))];
    [len(gaps), max(gaps) < close]
}
pub fn agrees(drifts) => all([for (d in drifts) d != null and d < close]);
pub fn within(d) => d < close;
pub fn id_of(cs) => cs[0] ++ (if (cs[1] == null) "" else "#" ++ cs[1])

// the largest distance from a rigid mesh's posed vertex to the nearest vertex ufbx places for
// that model, over every `stride`th vertex (meshes are unwelded per corner, so orders differ)
pub fn rigid_drift(c, clip_name, seconds, want, stride) any^ {
    let matrices = skin_matrices(c, clip_name, seconds)^;
    max([for (m in ((content(motion.body_of(c))) ^ { [] }) where ((name(m) == 'mesh' and want[m.name] != null) ^ { false }))
        for (g in ((content(m)) ^ { [] }) where ((name(g) == 'geometry') ^ { false })) for (v in ((0 to len(g.positions) div 3 - 1) ^ { [] }) where v % stride == 0)
            (let p = [g.positions[3 * v], g.positions[3 * v + 1], g.positions[3 * v + 2]],
             let q = t.point(matrices[int(g["skin-indices"][4 * v])], p)^,
             min([for (w in want[m.name]) gap(q, w)]))])
}
