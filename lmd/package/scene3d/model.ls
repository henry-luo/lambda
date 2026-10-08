import t: .transform

fn numeric(v) bool => v is number and abs(v) < inf
fn value(v, fallback) => if (v == null) fallback else v
fn fail(message) error => error("scene3d: " ++ message)
fn text(v) bool => (v is string or v is symbol) and len(string(v)) > 0
fn fields(node) map => map([for (k, v in node where k is string or k is symbol) (string(k), v)])
fn nodes(node, depth = 0) array^ {
    let checked = if (depth > 64) raise fail("hierarchy quota exceeded");
    [node, *[for (child in content(node)) *nodes(child, depth + 1)^]]
}
fn definition(all_nodes, reference, kind) element^ {
    let matches = [for (n in all_nodes where n.id != null and string(n.id) == string(reference) and name(n) == kind) n]
    if (len(matches) != 1) raise fail("missing " ++ string(kind) ++ " reference") else matches[0]
}
fn check_geometry(n) bool^ {
    let checked_type = if (not contains(['box', 'plane', 'buffer'], n.type)) raise fail("unknown geometry type")
    if (n.type == 'buffer') {
        let p = n.positions
        let checked_positions = if (not (p is array) or len(p) == 0 or len(p) % 3 != 0 or len(p) > 786432 or not all([for (x in p) numeric(x)]))
            raise fail("invalid geometry positions")
        let checked_arrays = [for (key in ["normals", "uvs", "colors"] where n[key] != null)
            if (not (n[key] is array) or len(n[key]) != (if (key == "uvs") len(p) / 3 * 2 else len(p)) or
                not all([for (x in n[key]) numeric(x)])) raise fail("invalid geometry components") else true]
        let checked_indices = if (n.indices != null and (not (n.indices is array) or len(n.indices) % 3 != 0 or
            not all([for (x in n.indices) numeric(x) and x >= 0 and x < len(p) / 3 and floor(x) == x])))
            raise fail("geometry index out of bounds")
        let checked_triangles = if (n.indices == null and len(p) % 9 != 0) raise fail("geometry requires triangles")
        true
    } else {
        let size = value(n.size, [1.0, 1.0, 1.0])
        if (not t.vector(size, 3) or not all([for (x in size) x > 0])) raise fail("invalid primitive size") else true
    }
}
fn check_node(n, all_nodes) bool^ {
    let checked_element = if (not (n is element)) raise fail("scene children must be elements")
    let kind = name(n)
    let checked_kind = if (not contains(['scene3d', 'group', 'camera', 'light', 'mesh', 'resources', 'geometry', 'material', 'texture'], kind))
        raise fail("unknown scene element")
    let checked_vectors = [for (key in ["position", "rotation", "scale", "target", "up"] where n[key] != null)
        if (not t.vector(n[key], 3) or (key == "scale" and any([for (x in n[key]) x == 0])))
            raise fail("invalid object transform") else true]
    let checked_visible = if (n.visible != null and not (n.visible is bool)) raise fail("invalid visibility")
    let children = content(n)
    let allowed = if (kind == 'scene3d' or kind == 'group') ['group', 'camera', 'light', 'mesh', 'resources', 'geometry', 'material', 'texture']
        else if (kind == 'resources') ['geometry', 'material', 'texture']
        else if (kind == 'mesh') ['geometry', 'material'] else if (kind == 'material') ['texture'] else []
    let checked_children = if (any([for (child in children) not (child is element) or not contains(allowed, name(child))])) raise fail("invalid scene child")
    if (kind == 'camera') {
        let fov = value(n.fov, 50.0), near = value(n.near, 0.1), far = value(n.far, 1000.0)
        if (n.type != 'perspective' or not numeric(fov) or fov <= 0 or fov >= 180 or not numeric(near) or near <= 0 or
            not numeric(far) or far <= near or (n.aspect != null and (not numeric(n.aspect) or n.aspect <= 0)))
            raise fail("invalid perspective camera") else true
    } else if (kind == 'geometry') check_geometry(n)^
    else if (kind == 'material') {
        let checked_type = if (not contains(['basic', 'lambert'], n.type)) raise fail("unknown material type")
        let checked_opacity = if (n.opacity != null and (not numeric(n.opacity) or n.opacity < 0 or n.opacity > 1)) raise fail("invalid material opacity")
        let checked_side = if (n.side != null and not contains(['front', 'back', 'double'], n.side)) raise fail("invalid material side")
        let checked_texture = if (n.texture != null) definition(all_nodes, n.texture, 'texture')^
        true
    } else if (kind == 'light') {
        if (not contains(['ambient', 'directional'], n.type) or (n.intensity != null and (not numeric(n.intensity) or n.intensity < 0)))
            raise fail("invalid light") else true
    } else if (kind == 'texture') {
        if (not (n.src is string) or len(n.src) == 0) raise fail("texture requires source") else true
    } else if (kind == 'mesh') {
        let geometries = [for (c in children where name(c) == 'geometry') c]
        let materials = [for (c in children where name(c) == 'material') c]
        let checked_geometry = if (n.geometry != null) {
            let d = definition(all_nodes, n.geometry, 'geometry')^
            if (len(geometries) != 0) raise fail("duplicate mesh geometry") else true
        } else if (len(geometries) != 1) raise fail("mesh requires geometry") else true
        let checked_material = if (n.material != null) {
            let d = definition(all_nodes, n.material, 'material')^
            if (len(materials) != 0) raise fail("duplicate mesh material") else true
        } else if (len(materials) != 1) raise fail("mesh requires material") else true
        let checked_instances = if (n.instances != null and (not (n.instances is array) or len(n.instances) > 16384 or
            not all([for (m in n.instances) t.vector(m, 16)]))) raise fail("invalid instance matrices")
        true
    } else true
}
fn normalized(n) element {
    let attrs = fields(n)
    if (name(n) == 'scene3d') <scene3d *: attrs, *[for (c in content(n)) normalized(c)]>
    else if (name(n) == 'group') <group position: [0.0, 0.0, 0.0], rotation: [0.0, 0.0, 0.0], scale: [1.0, 1.0, 1.0], *: attrs,
        *[for (c in content(n)) normalized(c)]>
    else if (name(n) == 'camera') <camera fov: 50.0, near: 0.1, far: 1000.0, *: attrs>
    else if (name(n) == 'mesh') <mesh visible: true, *: attrs, *[for (c in content(n)) normalized(c)]>
    else if (name(n) == 'resources') <resources *: attrs, *[for (c in content(n)) normalized(c)]>
    else n
}
pub fn normalize(scene) element^ {
    let checked_root = if (not (scene is element) or name(scene) != 'scene3d') raise fail("expected scene3d root")
    let all_nodes = nodes(scene)^
    let checked_count = if (len(all_nodes) > 8192) raise fail("node quota exceeded")
    let ids = [for (n in all_nodes where n.id != null) n.id]
    let checked_ids = if (any([for (id in ids) not text(id) or len(string(id)) >= 128 or
        len([for (other in ids where string(other) == string(id)) other]) > 1])) raise fail("invalid or duplicate scene ID")
    let checked_nodes = [for (n in all_nodes) check_node(n, all_nodes)^]
    let cameras = [for (n in all_nodes where name(n) == 'camera') n]
    let checked_camera = if (scene.camera != null) definition(all_nodes, scene.camera, 'camera')^
        else if (len(cameras) == 0) raise fail("active camera is missing") else cameras[0]
    normalized(scene)
}
