// lambda.scene3d.motion: characters in the common <character> schema
// (vibe/Lambda_Pkg_Motion.md §6, §8). FBX and BVH convert into it once, in
// lambda-io; every function here is pure data (S12.1.1v3), and playback
// belongs to the Radiant host.
import t: lambda.scene3d.transform
import v: lambda.scene3d.camera
import assets: lambda.scene3d.asset
import h: lambda.scene3d.motion.humanoid

pub type Vec3 = float[3]
pub type Quat = float[4]
pub type Mat4 = float[16]
// a joint names itself and its children (S11.4.12)
pub type Joint = <joint name: string, humanoid: symbol?, position: Vec3, quaternion: Quat,
    scale: Vec3?, tip: Vec3?, inverse_bind: Mat4?, Joint*>
// a channel names its joint, never a scene ID; every array holds one value per key
pub type Channel = <channel joint: string, position: float[]?, quaternion: float[]?, scale: float[]?>
// a baked clip: every channel has one value per key of `times`
pub type Clip = <clip name: string, duration: float, rate: float?, times: float[], Channel*>
// scene3d mesh vocabulary with body-local resource IDs, geometry in character space
pub type Body = <body <resources>?, <mesh>+>
pub type Character = <character name: string?, source: symbol, units: symbol, rig: symbol?,
    <skeleton Joint+>, Body?, Clip*>

fn fail(message) error => error("motion: " ++ message)
fn fields(n) map => map([for (k, x in n where k is string or k is symbol) (string(k), x)])

// Lambda tags are static, so one rebuild covers every character tag
fn rebuild(n, attrs, children) element {
    let kind = name(n)
    if (kind == 'character') <character *: attrs, *children>
    else if (kind == 'skeleton') <skeleton *: attrs, *children>
    else if (kind == 'joint') <joint *: attrs, *children>
    else if (kind == 'clip') <clip *: attrs, *children>
    else if (kind == 'channel') <channel *: attrs, *children>
    else <body *: attrs, *children>
}

pub fn skeleton_of(c) element => [for (x in content(c) where name(x) == 'skeleton') x][0]
pub fn clips_of(c) array => [for (x in content(c) where name(x) == 'clip') x]
pub fn body_of(c) => [for (x in content(c) where name(x) == 'body') x][0]
fn preorder(j) array => [j, *[for (child in content(j)) *preorder(child)]]
// every joint, parents before children: the skin-index order of a body
pub fn joints(c) array => [for (root in content(skeleton_of(c))) *preorder(root)]
fn joint_index(js) map => map([for (i in 0 to len(js) - 1) (js[i].name, i)])

fn increasing(values) bool => len(values) == 1 or all([for (i in 1 to (len(values) - 1)) values[i] > values[i - 1]])

fn check_clip(k, names) bool^ {
    let n = len(k.times)
    let checked_times = if (n == 0 or k.times[0] != 0 or not increasing(k.times) or k.duration < k.times[n - 1])
        raise fail("clip '" ++ k.name ++ "' needs times from 0, strictly increasing, within its duration")
    let targets = [for (ch in content(k)) ch.joint]
    let checked_targets = if (len(unique(targets)) != len(targets) or any([for (j in targets) not contains(names, j)]))
        raise fail("clip '" ++ k.name ++ "' must address existing joints, at most once each")
    let checked_lengths = [for (ch in content(k))
        if ((ch.position != null and len(ch.position) != 3 * n) or (ch.quaternion != null and len(ch.quaternion) != 4 * n) or
            (ch.scale != null and len(ch.scale) != 3 * n))
            raise fail("channel '" ++ ch.joint ++ "' needs one value per key") else true];
    true
}

// §6.3: the type fixes the shape; these are the invariants it cannot state
pub fn validate(c) bool^ {
    let checked_shape = if (not (c is Character)) raise fail("not a character (Lambda_Pkg_Motion.md §6.2)")
    let js = joints(c)
    let names = [for (j in js) j.name]
    let checked_names = if (len(unique(names)) != len(names)) raise fail("joint names must be unique")
    let roles = [for (j in js where j.humanoid != null) j.humanoid]
    let checked_roles = if (len(unique(roles)) != len(roles)) raise fail("humanoid roles must be unique")
    let clip_names = [for (k in clips_of(c)) k.name]
    let checked_clip_names = if (len(unique(clip_names)) != len(clip_names)) raise fail("clip names must be unique")
    let checked_clips = [for (k in clips_of(c)) check_clip(k, names)^]
    let body = body_of(c)
    let geometries = if (body == null) [] else [for (x in content(body)) *[for (g in content(x) where name(g) == 'geometry') g]]
    let skins = [for (g in geometries where g["skin-indices"] != null) g["skin-indices"]]
    let checked_skins = if (any([for (s in skins) any([for (x in s) x < 0 or x >= len(js)])]))
        raise fail("a body skin index names no joint");
    true
}

// ---- loading -----------------------------------------------------------------

fn scaled(x, s) => [x[0] * s, x[1] * s, x[2] * s]
fn scale_joint(j, s) element {
    let bind = j.inverse_bind
    let attrs = {*: fields(j), position: scaled(j.position, s),
        *: (if (j.tip != null) {tip: scaled(j.tip, s)} else {}),
        *: (if (bind != null) {inverse_bind: [*bind[0 to 11], bind[12] * s, bind[13] * s, bind[14] * s, bind[15]]} else {})};
    rebuild(j, attrs, [for (child in content(j)) scale_joint(child, s)])
}
fn scale_channel(ch, s) element => rebuild(ch, {*: fields(ch),
    *: (if (ch.position != null) {position: ch.position * s} else {})}, [])
fn scale_geometry(g, s) element => if (name(g) == 'geometry' and g.positions != null)
    <geometry *: {*: fields(g), positions: g.positions * s}> else g
fn scale_mesh(m, s) element => if (name(m) == 'mesh') <mesh *: fields(m), *[for (x in content(m)) scale_geometry(x, s)]>
    else <resources *: fields(m), *[for (x in content(m)) scale_geometry(x, s)]>
// multiply every length: rest positions, tips, position channels, body
// geometry and inverse-bind translations
pub fn scale(c, s) element => rebuild(c, {*: fields(c), units: 'm'}, [for (x in content(c))
    if (name(x) == 'skeleton') rebuild(x, fields(x), [for (j in content(x)) scale_joint(j, s)])
    else if (name(x) == 'clip') rebuild(x, fields(x), [for (ch in content(x)) scale_channel(ch, s)])
    else rebuild(x, fields(x), [for (m in content(x)) scale_mesh(m, s)])])

// a Z-up source turns -90 degrees about X: its roots and their channels rotate
let z_to_y = [-0.7071067811865476, 0.0, 0.0, 0.7071067811865476]
fn turn_root(j) element => rebuild(j, {*: fields(j), position: t.quaternion_rotate(z_to_y, j.position),
    quaternion: t.quaternion_multiply(z_to_y, j.quaternion)}, [for (x in content(j)) x])
fn turn_channel(ch) element {
    let keys = if (ch.position != null) len(ch.position) / 3 else if (ch.quaternion != null) len(ch.quaternion) / 4 else 0;
    rebuild(ch, {*: fields(ch),
        *: (if (ch.position != null) {position: [for (k in 0 to keys - 1) *t.quaternion_rotate(z_to_y, ch.position[3 * k to 3 * k + 2])]} else {}),
        *: (if (ch.quaternion != null) {quaternion: [for (k in 0 to keys - 1) *t.quaternion_multiply(z_to_y, ch.quaternion[4 * k to 4 * k + 3])]} else {})}, [])
}
fn z_up(c) element {
    let roots = [for (j in content(skeleton_of(c))) j.name];
    rebuild(c, fields(c), [for (x in content(c))
        if (name(x) == 'skeleton') rebuild(x, fields(x), [for (j in content(x)) turn_root(j)])
        else if (name(x) == 'clip') rebuild(x, fields(x), [for (ch in content(x)) if (contains(roots, ch.joint)) turn_channel(ch) else ch])
        else x])
}

fn keep_clip(c, wanted) element^ {
    let matches = [for (k in clips_of(c) where k.name == wanted) k]
    let checked = if (len(matches) != 1) raise fail("clip '" ++ string(wanted) ++ "' must match exactly one clip");
    rebuild(c, fields(c), [for (x in content(c) where name(x) != 'clip' or x.name == wanted) x])
}

// `input(…, {type: 'character'})` plus the caller's options (§8.2): `format`,
// `scale` (to metres), `up` ('y' or 'z'), `rig` (humanoid roles) and `clip`
// (keep one clip)
pub fn load(source, options = {}) element^ {
    let flavor = if (options.format == null) 'auto' else options.format
    let raw = input(source, {type: 'character', flavor: flavor})^
    let sized = if (options.scale == null) raw else scale(raw, options.scale)
    let upright = if (options.up == 'z') z_up(sized) else sized
    let picked = if (options.clip == null) upright else keep_clip(upright, options.clip)^
    let tagged = if (options.rig == null) picked else humanoid(picked, options.rig)^
    let checked = validate(tagged)^;
    tagged
}

// ---- poses -------------------------------------------------------------------

fn key3(values, k) => [values[3 * k], values[3 * k + 1], values[3 * k + 2]]
fn key4(values, k) => [values[4 * k], values[4 * k + 1], values[4 * k + 2], values[4 * k + 3]]
fn local(j, ch, k) {
    let position = if (ch != null and ch.position != null) key3(ch.position, k) else j.position
    let rotation = if (ch != null and ch.quaternion != null) key4(ch.quaternion, k) else j.quaternion
    let size = if (ch != null and ch.scale != null) key3(ch.scale, k) else if (j.scale != null) j.scale else [1.0, 1.0, 1.0];
    t.compose(position, rotation, size)
}
fn world_pairs(j, parent, channels, k) array {
    let m = t.multiply(parent, local(j, channels[j.name], k))^;
    [[j.name, m], *[for (child in content(j)) *world_pairs(child, m, channels, k)]]
}
let identity = [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0]

// each joint's world matrix at key `key` of the clip named `clip_name`, or at rest
pub fn pose(c, clip_name = null, key = 0) map^ {
    let found = if (clip_name == null) [] else [for (k in clips_of(c) where k.name == clip_name) k]
    let checked = if (clip_name != null and len(found) != 1) raise fail("clip '" ++ string(clip_name) ++ "' must match exactly one clip")
    let channels = if (clip_name == null) {} else map([for (ch in content(found[0])) (ch.joint, ch)])
    let pairs = [for (root in content(skeleton_of(c))) *world_pairs(root, identity, channels, key)];
    map([for (p in pairs) (p[0], p[1])])
}

// ---- humanoid roles ------------------------------------------------------------

// a source joint name without its namespace prefix (`mixamorig:Hips` → `Hips`)
fn base_name(n) string {
    let parts = split(n, ":");
    string(parts[len(parts) - 1])
}
fn profile(rig) map^ {
    if (rig is map) rig
    else if (rig == 'mixamo') h.mixamo
    else if (rig == 'cmu') h.cmu
    else if (rig == 'vrm') h.vrm
    else raise fail("unknown rig profile '" ++ string(rig) ++ "'; use 'mixamo', 'cmu', 'vrm' or a name map")
}
fn tag_joint(j, roles) element {
    let role = if (roles[j.name] != null) roles[j.name] else roles[base_name(j.name)];
    rebuild(j, {*: fields(j), *: (if (role != null) {humanoid: symbol(role)} else {})},
        [for (child in content(j)) tag_joint(child, roles)])
}
// joint roles from a profile ('mixamo', 'cmu', 'vrm') or a caller map {joint name: role}
pub fn humanoid(c, rig) element^ {
    let roles = profile(rig)^
    let tagged = rebuild(c, {*: fields(c), rig: if (rig is map) 'custom' else rig}, [for (x in content(c))
        if (name(x) == 'skeleton') rebuild(x, fields(x), [for (j in content(x)) tag_joint(j, roles)]) else x])
    let checked = validate(tagged)^;
    tagged
}

fn role_joint(c, role) => [for (j in joints(c) where j.humanoid == role) j][0]
// the joint that carries root motion: the hips, else the first root
fn root_joint(c) {
    let hips = role_joint(c, 'hips');
    if (hips != null) hips else content(skeleton_of(c))[0]
}
// the root's rest height above the lowest joint: the leg length that scales root motion
fn hip_height(c) float^ {
    let rest = pose(c)^
    let heights = [for (k, m in rest) m[13]];
    origin(rest[root_joint(c).name])[1] - min(heights)
}
fn hip_ratio(source, target) float^ {
    let from = hip_height(source)^
    let to = hip_height(target)^;
    if (from > 1e-9 and to > 1e-9) to / from else 1.0
}

// ---- moving clips between characters ----------------------------------------------

// target joint name → source joint name: by role where both sides are tagged,
// else by name without namespace (a CMU "Spine" is a Mixamo "Spine1")
fn joint_map(source, target) map {
    let by_name = map([for (j in joints(source)) (base_name(j.name), j)])
    let by_role = map([for (j in joints(source) where j.humanoid != null) (string(j.humanoid), j.name)])
    let named = (j) => (let s = by_name[base_name(j.name)],
        if (s != null and (s.humanoid == null or j.humanoid == null)) s.name else null)
    let source_of = (j) => if (j.humanoid != null and by_role[string(j.humanoid)] != null) by_role[string(j.humanoid)]
        else named(j);
    map([for (j in joints(target)) for (s in [source_of(j)] where s != null) (j.name, s)])
}
fn same_rest(source, target, pairs) bool {
    let from = map([for (j in joints(source)) (j.name, j.quaternion)]);
    all([for (j in joints(target) where pairs[j.name] != null)
        abs(abs(qdot(j.quaternion, from[pairs[j.name]])) - 1) < 1e-9])
}
fn qdot(a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]
fn clip_named(k, label) element => rebuild(k, {*: fields(k), name: label}, [for (ch in content(k)) ch])

// identical rest rotations: rename channels, scale root motion, and drop other
// translation and scale unless `keep_translation`
fn copy_clip(k, source, target, pairs, options) element^ {
    let back = map([for (b, s in pairs) (s, string(b))])
    let source_root = root_joint(source).name
    let ratio = hip_ratio(source, target)^
    let keep = options.keep_translation == true;
    rebuild(k, fields(k), [for (ch in content(k) where back[ch.joint] != null)
        (let root = ch.joint == source_root,
         <channel joint: back[ch.joint],
            *: (if (ch.position != null and root) {position: ch.position * ratio}
                else if (ch.position != null and keep) {position: ch.position} else {}),
            *: (if (ch.quaternion != null) {quaternion: ch.quaternion} else {}),
            *: (if (ch.scale != null and keep) {scale: ch.scale} else {})>)])
}

// world rotations by composing local quaternions down the skeleton, at a key or at rest
fn world_quats(j, parent, channels, key) array {
    let ch = channels[j.name]
    let local = if (key != null and ch != null and ch.quaternion != null) key4(ch.quaternion, key) else j.quaternion
    let q = t.quaternion_normalize(t.quaternion_multiply(parent, local));
    [[j.name, q], *[for (child in content(j)) *world_quats(child, q, channels, key)]]
}
fn rotations(c, channels, key) map =>
    map([for (root in content(skeleton_of(c))) for (p in world_quats(root, [0.0, 0.0, 0.0, 1.0], channels, key)) (p[0], p[1])])
// a mapped joint takes the source's world rotation change from rest; an
// unmapped one keeps its rest local rotation (§8.4 steps 2-3)
fn retarget_walk(j, parent, moved, rest_from, rest_to, pairs) array {
    let s = pairs[j.name]
    let w = if (s != null) t.quaternion_multiply(t.quaternion_multiply(moved[s], t.quaternion_inverse(rest_from[s])), rest_to[j.name])
        else t.quaternion_multiply(parent, j.quaternion)
    let local = t.quaternion_normalize(t.quaternion_multiply(t.quaternion_inverse(parent), w));
    [[j.name, local], *[for (child in content(j)) *retarget_walk(child, w, moved, rest_from, rest_to, pairs)]]
}
// consecutive keys on one hemisphere, so SLERP takes the short way
fn continuous(keys) array {
    let n = len(keys) / 4
    let flips = [for (k in 0 to n - 1) if (k > 0 and qdot(key4(keys, k), key4(keys, k - 1)) < 0) -1.0 else 1.0]
    let signs = math.cumprod(flips);
    [for (k in 0 to n - 1) for (c in 0 to 3) keys[4 * k + c] * signs[k]]
}

// §8.4: each mapped joint's world rotation change from rest, re-expressed
// locally; root motion scaled by the hip-height ratio into the target's root frame
pub fn retarget(k, from, to, options = {}) element^ {
    let missing = [*[for (r in h.required where role_joint(from, r) == null) "source " ++ string(r)],
                   *[for (r in h.required where role_joint(to, r) == null) "target " ++ string(r)]]
    let checked_roles = if (len(missing) > 0) raise fail("retarget needs the VRM required roles on both skeletons; missing " ++ join(missing, ", "))
    let checked_clip = if (len([for (x in clips_of(from) where x.name == k.name) x]) != 1) raise fail("retarget's clip must belong to its source character")
    let pairs = joint_map(from, to)
    let channels = map([for (ch in content(k)) (ch.joint, ch)])
    let rest_key = options.rest_frame
    let rest_from = if (rest_key == null) rotations(from, channels, null) else rotations(from, channels, rest_key)
    let rest_to = rotations(to, {}, null)
    let n = len(k.times)
    let poses = [for (key in 0 to n - 1) map([for (root in content(skeleton_of(to)))
        for (p in retarget_walk(root, [0.0, 0.0, 0.0, 1.0], rotations(from, channels, key), rest_from, rest_to, pairs)) (p[0], p[1])])]
    // root motion: the source root's world position, scaled, in the target root's parent frame
    let source_root = root_joint(from).name
    let target_root = root_joint(to)
    let ratio = hip_ratio(from, to)^
    let rest = pose(to)^
    let parent_name = [for (j in joints(to)) for (c in content(j) where c.name == target_root.name) j.name][0]
    let into_parent = if (parent_name == null) identity else t.inverse(rest[parent_name])^
    let root_positions = [for (key in 0 to n - 1)
        *t.point(into_parent, origin(pose(from, k.name, key)^[source_root]) * ratio)^]
    let mapped = [for (j in joints(to) where pairs[j.name] != null) j.name];
    <clip name: k.name, duration: k.duration, *: (if (k.rate != null) {rate: k.rate} else {}), times: k.times,
        *[for (b in joints(to) where pairs[b.name] != null or b.name == target_root.name)
            <channel joint: b.name,
                *: (if (b.name == target_root.name) {position: root_positions} else {}),
                *: (if (pairs[b.name] != null) {quaternion: continuous([for (key in 0 to n - 1) *poses[key][b.name]])} else {})>]>
}

// add the first clip of each source as a clip named by its key (§8.3)
pub fn attach(c, sources, options = {}) element^ {
    let added = [for (label, source in sources)
        (let found = clips_of(source),
         let checked = if (len(found) == 0) raise fail("attach source '" ++ string(label) ++ "' has no clip"),
         let pairs = joint_map(source, c),
         let checked_pairs = if (len(pairs) == 0) raise fail("attach source '" ++ string(label) ++ "' shares no joint with the character"),
         let moved = if (same_rest(source, c, pairs)) copy_clip(found[0], source, c, pairs, options)^
             else retarget(found[0], source, c, options)^,
         clip_named(moved, string(label)))]
    let joined = rebuild(c, fields(c), [*content(c), *added])
    let checked = validate(joined)^;
    joined
}

// ---- clip operations -----------------------------------------------------------

fn map_clips(c, f) element => rebuild(c, fields(c), [for (x in content(c)) if (name(x) == 'clip') f(x) else x])

// hold the root's translation on `axes` at its first key (Mixamo's "In Place")
pub fn in_place(c, axes = ['x', 'z']) element^ {
    let held = [for (a in axes) if (a == 'x') 0 else if (a == 'y') 1 else if (a == 'z') 2
        else raise fail("in_place axes are 'x', 'y' or 'z'")]
    let root = root_joint(c).name
    let hold = (ch) => if (ch.joint != root or ch.position == null) ch
        else <channel *: {*: fields(ch), position: [for (k in 0 to len(ch.position) / 3 - 1) for (i in 0 to 2)
            if (contains(held, i)) ch.position[i] else ch.position[3 * k + i]]}>
    let out = map_clips(c, (k) => rebuild(k, fields(k), [for (ch in content(k)) hold(ch)]))
    let checked = validate(out)^;
    out
}

fn swap_side(role) => if (starts_with(role, "left")) "right" ++ slice(role, 4) else if (starts_with(role, "right")) "left" ++ slice(role, 5) else null
// swap left and right joints, reflect rotations across the YZ plane and translations in X
pub fn mirror(c) element^ {
    let js = joints(c)
    let checked_roles = if (all([for (j in js) j.humanoid == null])) raise fail("mirror needs humanoid roles; tag them with humanoid or the rig option")
    let by_role = map([for (j in js where j.humanoid != null) (string(j.humanoid), j.name)])
    let partner = map([for (j in js) (j.name, (let r = if (j.humanoid == null) null else swap_side(string(j.humanoid)),
        if (r != null and by_role[r] != null) by_role[r] else j.name))])
    let reflect = (ch) => <channel joint: partner[ch.joint],
        *: (if (ch.position != null) {position: [for (k in 0 to len(ch.position) / 3 - 1) *[0.0 - ch.position[3 * k], ch.position[3 * k + 1], ch.position[3 * k + 2]]]} else {}),
        *: (if (ch.quaternion != null) {quaternion: [for (k in 0 to len(ch.quaternion) / 4 - 1)
            *[ch.quaternion[4 * k], 0.0 - ch.quaternion[4 * k + 1], 0.0 - ch.quaternion[4 * k + 2], ch.quaternion[4 * k + 3]]]} else {}),
        *: (if (ch.scale != null) {scale: ch.scale} else {})>
    let out = map_clips(c, (k) => rebuild(k, fields(k), [for (ch in content(k)) reflect(ch)]))
    let checked = validate(out)^;
    out
}

// keep the keys within [from, to] seconds and rebase them to 0
pub fn trim(c, from, to) element^ {
    let checked_range = if (not (from <= to)) raise fail("trim needs from <= to")
    let cut = (k) => (let kept = [for (i in 0 to len(k.times) - 1 where k.times[i] >= from - 1e-6 and k.times[i] <= to + 1e-6) i],
        if (len(kept) == 0) k else (let a = kept[0], let b = kept[len(kept) - 1],
            rebuild(k, {*: fields(k), times: [for (i in a to b) k.times[i] - k.times[a]], duration: k.times[b] - k.times[a]},
                [for (ch in content(k)) rebuild(ch, {*: fields(ch),
                    *: (if (ch.position != null) {position: ch.position[3 * a to 3 * b + 2]} else {}),
                    *: (if (ch.quaternion != null) {quaternion: ch.quaternion[4 * a to 4 * b + 3]} else {}),
                    *: (if (ch.scale != null) {scale: ch.scale[3 * a to 3 * b + 2]} else {})}, [])])))
    let empty = [for (k in clips_of(c) where len([for (x in k.times where x >= from - 1e-6 and x <= to + 1e-6) x]) == 0) k.name]
    let checked_keys = if (len(empty) > 0) raise fail("trim keeps no key of clip '" ++ empty[0] ++ "'")
    let out = map_clips(c, cut)
    let checked = validate(out)^;
    out
}

// ---- mannequin ---------------------------------------------------------------

// one octahedral bone from head to tail, every vertex weighted to `bone`
fn bone_shape(head, tail, radius) {
    let d = v.subtract(tail, head)
    let across = if (abs(d[1]) > 0.9 * sqrt(v.dot(d, d))) [1.0, 0.0, 0.0] else [0.0, 1.0, 0.0]
    let u = v.times(v.unit(v.cross(d, across)), radius)
    let w = v.times(v.unit(v.cross(d, u)), radius)
    let mid = v.add(head, v.times(d, 0.2));
    [head, tail, v.add(mid, u), v.add(mid, w), v.subtract(mid, u), v.subtract(mid, w)]
}
let bone_faces = [0, 2, 3, 0, 3, 4, 0, 4, 5, 0, 5, 2, 1, 3, 2, 1, 4, 3, 1, 5, 4, 1, 2, 5]
fn origin(m) => [m[12], m[13], m[14]]

// a capsule-like figure for a bodiless skeleton (BVH): a bone from each
// joint to each child, or to its tip (§8.6)
pub fn mannequin(c, options = {}) element^ {
    let radius = if (options.radius == null) 0.03 else options.radius
    let color = if (options.color == null) "#d0d4dc" else options.color
    let rest = pose(c)^
    let js = joints(c)
    let index = joint_index(js)
    let segments = [for (j in js) *[*[for (child in content(j)) [index[j.name], origin(rest[j.name]), origin(rest[child.name])]],
        *(if (j.tip != null) [[index[j.name], origin(rest[j.name]), t.point(rest[j.name], j.tip)^]] else [])]]
    let shown = [for (s in segments where v.dot(v.subtract(s[2], s[1]), v.subtract(s[2], s[1])) > 1e-12) s]
    let positions = [for (s in shown) for (p in bone_shape(s[1], s[2], radius)) *p]
    let indices = [for (n in 0 to len(shown) - 1) for (f in bone_faces) 6 * n + f]
    let joints_of = [for (s in shown) for (p in 0 to 5) *[s[0], 0, 0, 0]]
    let weights = [for (s in shown) for (p in 0 to 5) *[1.0, 0.0, 0.0, 0.0]]
    let body = <body <mesh <geometry type: 'buffer', positions: positions, indices: indices,
        'skin-indices': joints_of, 'skin-weights': weights>
        <material type: 'basic', color: color, side: 'double'>>>;
    rebuild(c, fields(c), [*[for (x in content(c) where name(x) == 'skeleton') x], body,
        *[for (x in content(c) where name(x) == 'clip') x]])
}

// ---- compiling to the scene ----------------------------------------------------

fn bone(j, id, index) element => <bone id: id ++ "-bone-" ++ string(index[j.name]), name: j.name,
    position: j.position, quaternion: j.quaternion,
    *: (if (j.scale != null) {scale: j.scale} else {}),
    *: (if (j.inverse_bind != null) {'inverse-bind-matrix': j.inverse_bind} else {}),
    *[for (child in content(j)) bone(child, id, index)]>

fn tracks(ch, times, id, index) array {
    let target = id ++ "-bone-" ++ string(index[ch.joint]);
    [*(if (ch.position != null) [<'keyframe-track' path: target ++ ".position", type: 'vector', interpolation: 'linear', times: times, values: ch.position>] else []),
     *(if (ch.quaternion != null) [<'keyframe-track' path: target ++ ".quaternion", type: 'quaternion', interpolation: 'linear', times: times, values: ch.quaternion>] else []),
     *(if (ch.scale != null) [<'keyframe-track' path: target ++ ".scale", type: 'vector', interpolation: 'linear', times: times, values: ch.scale>] else [])]
}

fn skinned(m, geometries) bool {
    let inline = [for (g in content(m) where name(g) == 'geometry') g]
    let g = if (len(inline) > 0) inline[0] else geometries[string(m.geometry)];
    g != null and g["skin-indices"] != null
}

// §8.7: the one place a character gains scene IDs and track paths. Bone k is
// joint k in preorder, a body's resources and meshes are prefixed like a
// loaded asset's, and each non-empty clip becomes `<id>-clip-<n>`
pub fn scene(c, options = {}) element^ {
    let checked = validate(c)^
    let id = if (options.id == null) "character" else string(options.id)
    let checked_id = if (len(id) == 0 or len(id) > 48 or contains(id, "."))
        raise fail("scene id must be nonempty, at most 48 characters, and contain no dot")
    let js = joints(c)
    let index = joint_index(js)
    let skeleton = <skeleton id: id ++ "-skeleton", *[for (root in content(skeleton_of(c))) bone(root, id, index)]>
    let body = body_of(c)
    let parts = if (body == null) [] else content(body)
    let geometries = map([for (r in parts where name(r) == 'resources') for (g in content(r) where name(g) == 'geometry' and g.id != null)
        (string(g.id), g)])
    let meshes = [for (m in parts)
        if (name(m) == 'mesh' and skinned(m, geometries))
            (let r = assets.rewrite(m, id, {}), <mesh *: fields(r), skeleton: id ++ "-skeleton", *content(r)>)
        else assets.rewrite(m, id, {})]
    let cs = clips_of(c)
    let clips = [for (n in 0 to len(cs) - 1 where len(content(cs[n])) > 0)
        <'animation-clip' id: id ++ "-clip-" ++ string(n), label: cs[n].name, duration: cs[n].duration,
            autoplay: options.autoplay == cs[n].name, *[for (ch in content(cs[n])) *tracks(ch, cs[n].times, id, index)]>];
    <group *: (if (options.transform is map) options.transform else {}), id: id, *[skeleton, *meshes, *clips]>
}
