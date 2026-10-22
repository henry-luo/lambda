// Comparisons of converted characters with the reference poses that
// test/demo/scene3d/assets/motion/_oracle.mjs writes: shared by motion_oracle.ls
// and the local-only Mixamo check. Every position is in character metres.
import motion: lambda.scene3d.motion
import t: lambda.scene3d.transform

// 0.01 mm: both references compute in float32 in places
let close = 0.00001

fn origin(m) => [m[12], m[13], m[14]];
pub fn gap(a, b) => max([abs(a[0] - b[0]), abs(a[1] - b[1]), abs(a[2] - b[2])]);
// Three.js drops characters such as ':' from node names
fn sanitized(n) => replace(replace(n, ":", ""), " ", "_");
fn key_at(k, seconds) => [for (i in 0 to len(k.times) - 1 where abs(k.times[i] - seconds) < 0.000001) i][0];
pub fn world_at(c, clip_name, seconds) {
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
fn lookup(want, p) {
    let exact = want[string(unit(p[0])) ++ "," ++ string(unit(p[1])) ++ "," ++ string(unit(p[2]))];
    if (exact != null) exact
    else [for (dx in [-1, 0, 1]) for (dy in [-1, 0, 1]) for (dz in [-1, 0, 1])
        for (hit in [want[string(unit(p[0]) + dx) ++ "," ++ string(unit(p[1]) + dy) ++ "," ++ string(unit(p[2]) + dz)]] where hit != null) hit][0]
}
// [vertices compared, all within tolerance] over every `stride`th vertex; vertices
// with more than four influences are not referenced
pub fn skin_drift(c, clip_name, seconds, want, stride) {
    let world = world_at(c, clip_name, seconds)
    let js = motion.joints(c)
    let gs = [for (m in content(motion.body_of(c)) where name(m) == 'mesh') for (g in content(m) where name(g) == 'geometry' and g["skin-indices"] != null) g]
    let gaps = [for (g in gs) for (v in 0 to len(g.positions) / 3 - 1 where v % stride == 0)
        for (w in [lookup(want, [g.positions[3 * v], g.positions[3 * v + 1], g.positions[3 * v + 2]])] where w != null)
            (let p = [g.positions[3 * v], g.positions[3 * v + 1], g.positions[3 * v + 2]],
             let parts = [for (s in 0 to 3 where g["skin-weights"][4 * v + s] > 0)
                (let j = js[int(g["skin-indices"][4 * v + s])],
                 t.point(t.multiply(world[j.name], j.inverse_bind)^, p)^ * g["skin-weights"][4 * v + s])],
             gap([sum([for (x in parts) x[0]]), sum([for (x in parts) x[1]]), sum([for (x in parts) x[2]])], w))];
    [len(gaps), max(gaps) < close]
}
pub fn agrees(drifts) => all([for (d in drifts) d != null and d < close]);
pub fn within(d) => d < close;
pub fn id_of(cs) => cs[0] ++ (if (cs[1] == null) "" else "#" ++ cs[1])

// the largest distance from a rigid mesh's posed vertex to the nearest vertex ufbx places for
// that model, over every `stride`th vertex (meshes are unwelded per corner, so orders differ)
pub fn rigid_drift(c, clip_name, seconds, want, stride) {
    let world = world_at(c, clip_name, seconds)
    let js = motion.joints(c);
    max([for (m in content(motion.body_of(c)) where name(m) == 'mesh' and want[m.name] != null)
        for (g in content(m) where name(g) == 'geometry') for (v in 0 to len(g.positions) div 3 - 1 where v % stride == 0)
            (let p = [g.positions[3 * v], g.positions[3 * v + 1], g.positions[3 * v + 2]],
             let j = js[int(g["skin-indices"][4 * v])],
             let q = t.point(t.multiply(world[j.name], j.inverse_bind)^, p)^,
             min([for (w in want[m.name]) gap(q, w)]))])
}
