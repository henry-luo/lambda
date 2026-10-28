import t: .transform
import animation: .animation

// the texture maps a standard material may name, beside its base color `texture`
let STANDARD_MAPS = ["metal-rough-texture", "normal-texture", "occlusion-texture", "emissive-texture"]
let PHYSICAL_MAPS = ["clearcoat-texture", "clearcoat-roughness-texture", "clearcoat-normal-texture", "sheen-color-texture", "sheen-roughness-texture", "transmission-texture", "thickness-texture"]
let MAPS = [*STANDARD_MAPS, *PHYSICAL_MAPS]
let OUTDOOR_NUMBERS = ["wind-strength","wind-height","wind-frequency","wind-phase","wind-direction",
    "foliage","foliage-thickness","water-amplitude","water-frequency","water-phase","water-direction"]
fn outdoor_range(key,v) bool => if (key=="foliage") v>=0 and v<=1 else if (key=="wind-height") v>0
    else if (contains(["wind-strength","wind-frequency","foliage-thickness","water-amplitude","water-frequency"],key)) v>=0 else true
let PHYSICAL_NUMBERS = ["clearcoat", "clearcoat-roughness", "clearcoat-normal-scale", "sheen", "sheen-roughness", "transmission", "ior", "thickness", "attenuation-distance"]
let PHYSICAL_COLORS = ["sheen-color", "attenuation-color"]
fn physical_range(key, v) bool => if (key == "clearcoat-normal-scale") true else if (key == "ior") v >= 1
    else if (key == "thickness") v >= 0 else if (key == "attenuation-distance") v > 0 else v >= 0 and v <= 1
fn numeric(v) bool => t.numeric(v)
fn value(v, fallback) => if (v == null) fallback else v
fn fail(message) error => error("scene3d: " ++ message)
fn text(v) bool => (v is string or v is symbol) and len(string(v)) > 0
fn color(v) bool {
    if (not (v is string)) false
    else if (v == "transparent") true
    else {
        let hex = if (slice(v, 0, 1) == "#") slice(v, 1, len(v)) else v
        contains([3, 4, 6, 8], len(hex)) and all([for (i in 0 to (len(hex) - 1)) contains("0123456789abcdefABCDEF", slice(hex, i, i + 1))])
    }
}
// shader materials (SH1) and simulations (SH5): uniforms, instance attributes and variables take GLSL
// names their host prefix leaves free
let RESERVED = ["position", "normal", "uv", "uv1", "color", "tangent", "instance_matrix", "model_matrix", "view_matrix",
    "projection_matrix", "camera_position", "fragment_color"]
let SIMULATION_RESERVED = ["resolution", "delta", "fragment_color"]
let POST_RESERVED = ["color_input", "resolution", "fragment_color", "pass_color", "effect_main"]
let DRAWABLES = ['mesh', 'points', 'sprite', 'line', 'line-segments']
let OBJECT_CHILDREN = ['group', 'lod', 'skeleton', 'camera', 'light', *DRAWABLES, 'resources', 'geometry', 'material', 'texture',
    'animation-clip', 'rigid-body', 'collider', 'physics', 'joint', 'motion-controller', 'environment', 'simulation']
let IDENT = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_"
fn glsl_name(k, reserved = RESERVED) bool {
    let s = string(k)
    len(s) > 0 and len(s) < 23 and slice(s, 0, 3) != "gl_" and not contains("0123456789", slice(s, 0, 1)) and
        all([for (i in 0 to (len(s) - 1)) contains(IDENT, slice(s, i, i + 1))]) and not contains(reserved, s)
}
// a uniform's playback type: a number, a vector, or a "#rrggbb" color; null for the texture a sampler reads
fn uniform_type(v) => if (numeric(v)) 'number' else if (v is array) 'vector'
    else if (v is string and slice(v, 0, 1) == "#") 'color' else null
fn fields(node) map => map([for (k, v in node where k is string or k is symbol) (string(k), v)])
fn nodes(node, depth = 0) array^ {
    let checked_element = if (not (node is element)) raise fail("scene children must be elements")
    let checked = if (depth > 64) raise fail("hierarchy quota exceeded");
    [node, *[for (child in content(node)) *nodes(child, depth + 1)^]]
}
fn definition(all_nodes, reference, kind) element^ {
    let matches = [for (n in all_nodes where n.id != null and string(n.id) == string(reference) and name(n) == kind) n]
    if (len(matches) != 1) raise fail("missing " ++ string(kind) ++ " reference") else matches[0]
}
// the material a mesh names or holds
fn mesh_material(mesh, all_nodes) element^ {
    let held = [for (c in content(mesh) where c is element and name(c) == 'material') c]
    if (mesh.material != null) definition(all_nodes, mesh.material, 'material')^
    else if (len(held) != 1) raise fail("mesh requires material") else held[0]
}
// a `uniforms` map of at most 32 entries
fn uniform_map(n) map^ {
    let u = value(n.uniforms, {})
    if (not (u is map) or len([for (k, v in u) k]) > 32) raise fail("shader uniforms must be a map of at most 32 values") else u
}
// a uniform: a GLSL name and numbers, up to 256-number arrays or a "#rrggbb" color; with `samplers`, other text
fn check_uniform(k, v, samplers, reserved = null) bool^ =>
    if (not glsl_name(k, value(reserved, if (samplers) RESERVED else SIMULATION_RESERVED))) raise fail("invalid shader uniform name")
    else if ((uniform_type(v) == 'vector' and (len(v) < 1 or len(v) > 256 or not all([for (x in v) numeric(x)]))) or
        (uniform_type(v) == 'color' and not color(v)) or (uniform_type(v) == null and (not samplers or not text(v))))
        raise fail("invalid shader uniform value") else true
// SH5: "<simulation>.<variable>" samples a simulation variable's records; other text names a texture
fn check_sampler(all_nodes, v) bool^ {
    let parts = split(string(v), ".")
    if (len(parts) == 1) { let texture = definition(all_nodes, v, 'texture')^; true }
    else if (len(parts) != 2 or not contains([for (c in content(definition(all_nodes, parts[0], 'simulation')^)) string(c.name)], parts[1]))
        raise fail("shader uniform names no simulation variable") else true
}
fn check_shader(n, all_nodes) bool^ {
    let checked_outdoor = if (any([for (key in [*OUTDOOR_NUMBERS,"foliage-color"]) n[key]!=null]))
        raise fail("outdoor factors require a built-in material")
    let checked_sources = if (not text(n.vertex) or not text(n.fragment)) raise fail("shader material requires vertex and fragment sources")
    let checked_state = if ((n.side != null and not contains(['front', 'back', 'double'], n.side)) or
        any([for (key in ["transparent", "alpha-to-coverage"] where n[key] != null) not (n[key] is bool)]) or len(content(n)) != 0)
        raise fail("invalid shader material side, transparency or children")
    let u = uniform_map(n)^
    let checked_values = [for (k, v in u) if (check_uniform(k, v, true)^ and uniform_type(v) == null) check_sampler(all_nodes, v)^ else true]
    if (len([for (k, v in u where uniform_type(v) == null) k]) > 8) raise fail("shader sampler quota exceeded") else true
}
// SH5: a GPU simulation of one to four variables, stepped toward its numeric `time` uniform
fn check_simulation(n) bool^ {
    let w = n.width, h = n.height, step = value(n.timestep, 1.0 / 60.0)
    let checked_shape = if (not text(n.id) or contains(string(n.id), ".") or not numeric(w) or not numeric(h) or w < 1 or h < 1 or
        floor(w) != w or floor(h) != h or w * h > 262144 or not numeric(step) or step <= 0 or step > 1)
        raise fail("simulation requires an ID without dots, at most 262144 width × height records and a timestep in (0, 1]")
    let variables = content(n)
    let names = [for (v in variables) string(v.name)]
    let checked_variables = if (len(variables) < 1 or len(variables) > 4) raise fail("simulation requires one to four variable children")
        else if (any([for (v in variables) not glsl_name(v.name, SIMULATION_RESERVED) or not text(v.shader) or
            (v.initial != null and (not (v.initial is array) or len(v.initial) != w * h * 4 or not all([for (x in v.initial) numeric(x)])))]))
            raise fail("simulation variable requires a GLSL name, a shader and 4 initial numbers per record")
        else if (len(unique(names)) != len(names)) raise fail("duplicate simulation variable")
    let u = uniform_map(n)^
    let checked_values = [for (k, v in u) if (check_uniform(k, v, false)^ and contains(names, string(k)))
        raise fail("simulation uniform repeats a variable name") else true]
    if (not numeric(u.time)) raise fail("simulation requires a numeric time uniform") else true
}
fn display_effect(n) bool => n.domain == 'display' or (n.domain == null and n.type == 'fxaa')
fn opaque_color(v) bool {
    if (not color(v) or v == "transparent") false else {
        let hex = if (slice(v, 0, 1) == "#") slice(v, 1, len(v)) else v
        contains([3, 6], len(hex)) or (len(hex) == 4 and contains(["f", "F"], slice(hex, 3, 4))) or
            (len(hex) == 8 and lower(slice(hex, 6, 8)) == "ff")
    }
}
fn check_effect(n, all_nodes) bool^ {
    let checked_type = if (not contains(['bloom', 'fxaa', 'shader'], n.type)) raise fail("unknown postprocessing effect")
    let checked_domain = if ((n.domain != null and not contains(['linear', 'display'], n.domain)) or
        (n.type == 'bloom' and display_effect(n)) or (n.type == 'fxaa' and not display_effect(n)))
        raise fail("postprocessing requires linear effects before display effects")
    if (n.type == 'shader') {
        let checked_source = if (not text(n.fragment)) raise fail("shader effect requires a fragment source")
        let u = uniform_map(n)^
        let checked_values = [for (k, v in u) if (check_uniform(k, v, true, POST_RESERVED)^ and uniform_type(v) == null) check_sampler(all_nodes, v)^ else true]
        if (len([for (k, v in u where uniform_type(v) == null) k]) > 8) raise fail("shader sampler quota exceeded") else true
    } else {
        let checked_uniforms = if (n.uniforms != null) raise fail("built-in effects take numeric options, not uniforms")
        if (n.type == 'bloom' and any([for (k in ["strength", "threshold", "radius"] where n[k] != null) not numeric(n[k]) or n[k] < 0]))
            raise fail("bloom strength, threshold and radius must be finite nonnegative numbers") else true
    }
}

pub fn check_geometry(n) bool^ {
    let checked_uv_mode = if (n["drawable-uvs"] != null and not contains(['source', 'surface'], n["drawable-uvs"])) raise fail("invalid drawable-uvs mode")
    let checked_type = if (not contains(['box', 'plane', 'buffer'], n.type)) raise fail("unknown geometry type")
    if (n.type == 'buffer') {
        let p = n.positions
        let checked_positions = if (not (p is array) or len(p) == 0 or len(p) % 3 != 0 or len(p) > 786432 or not all([for (x in p) numeric(x)]))
            raise fail("invalid geometry positions")
        let checked_arrays = [for (key in ["normals", "uvs", "uvs1", "colors", "tangents", "point-sizes"] where n[key] != null)
            if (not (n[key] is array) or not contains((if (key == "point-sizes") [len(p) / 3] else if (contains(["uvs", "uvs1"], key)) [len(p) / 3 * 2] else if (key == "tangents") [len(p) / 3 * 4] else if (key == "colors") [len(p), len(p) / 3 * 4] else [len(p)]), len(n[key])) or
                not all([for (x in n[key]) numeric(x)])) raise fail("invalid geometry components") else true]
        let checked_colors = if (n.colors != null and any([for (x in n.colors) x < 0 or x > 1])) raise fail("vertex color out of range")
        // glTF tangents: a unit direction and a handedness of exactly ±1
        let checked_tangents = if (n.tangents != null and any([for (i in 0 to (len(p) / 3 - 1), let t = slice(n.tangents, i * 4, i * 4 + 4))
            abs(t[3]) != 1 or abs(t[0] * t[0] + t[1] * t[1] + t[2] * t[2] - 1) > 0.01])) raise fail("invalid geometry tangent")
        let checked_indices = if (n.indices != null and (not (n.indices is array) or
            not all([for (x in n.indices) numeric(x) and x >= 0 and x < len(p) / 3 and floor(x) == x])))
            raise fail("geometry index out of bounds")
        let checked_sizes = if (n["point-sizes"] != null and any([for (x in n["point-sizes"]) x <= 0])) raise fail("point-sizes needs one positive number per vertex")
        let vertices = len(p) / 3
        let skin = n["skin-indices"], weights = n["skin-weights"]
        let checked_skin = if (skin != null or weights != null) {
            if (not (skin is array) or not (weights is array) or len(skin) != vertices * 4 or len(weights) != len(skin) or
                not all([for (x in skin) numeric(x) and x >= 0 and x < 256 and floor(x) == x]) or
                not all([for (x in weights) numeric(x) and x >= 0 and x <= 1]) or
                not all([for (i in 0 to (vertices - 1)) abs(sum(slice(weights, i * 4, i * 4 + 4)) - 1.0) <= 0.00001]))
                raise fail("invalid skin indices or weights") else true
        } else true
        let morph = n["morph-positions"], normals = n["morph-normals"]
        let checked_morph = if (morph != null or normals != null) {
            if (not (morph is array) or not contains([len(p), len(p) * 2], len(morph)) or not all([for (x in morph) numeric(x)]) or
                (normals != null and (not (normals is array) or len(normals) != len(morph) or not all([for (x in normals) numeric(x)]))))
                raise fail("invalid relative morph targets") else true
        } else true
        true
    } else {
        let size = value(n.size, [1.0, 1.0, 1.0])
        if (not t.vector(size, 3) or not all([for (x in size) x > 0])) raise fail("invalid primitive size") else true
    }
}
fn check_node(n, all_nodes) bool^ {
    let checked_element = if (not (n is element)) raise fail("scene children must be elements")
    let kind = name(n)
    let checked_kind = if (not contains(['scene3d', 'group', 'lod', 'skeleton', 'bone', 'camera', 'light', *DRAWABLES, 'resources', 'geometry', 'material', 'texture', 'animation-clip', 'keyframe-track', 'physics', 'rigid-body', 'collider', 'joint', 'motion-controller', 'motion-state', 'environment', 'fog', 'simulation', 'variable', 'postprocessing', 'effect'], kind))
        raise fail("unknown scene element")
    let checked_refs = [for (key in (if (kind == 'scene3d') ["camera", "environment"] else if (contains(DRAWABLES, kind)) ["geometry", "material", "skeleton"]
        else if (kind == 'material') ["texture", *MAPS] else []) where n[key] != null)
        if (not text(n[key])) raise fail("resource reference requires text") else true]
    let checked_vectors = [for (key in ["position", "rotation", "scale", "target", "up"] where n[key] != null)
        if (not t.vector(n[key], 3) or (key == "scale" and any([for (x in n[key]) x == 0])))
            raise fail("invalid object transform") else true]
    let checked_quaternion = if (n.quaternion != null and (not t.vector(n.quaternion, 4) or sum([for (q in n.quaternion) q * q]) == 0))
        raise fail("invalid object quaternion")
    let checked_matrix = if (n.matrix != null and (not t.affine(n.matrix) or n.position != null or n.rotation != null or n.scale != null or n.quaternion != null))
        raise fail("matrix and TRS transforms are exclusive")
    let checked_bind = if (n["inverse-bind-matrix"] != null and (kind != 'bone' or not t.affine(n["inverse-bind-matrix"])))
        raise fail("invalid authored inverse bind matrix")
    let color_key = if (kind == 'scene3d' or kind == 'environment') "background" else "color"
    let checked_color = if ((contains(['scene3d', 'environment', 'material', 'light'], kind) and n[color_key] != null and not color(n[color_key])) or
        (kind == 'light' and n["ground-color"] != null and not color(n["ground-color"]))) raise fail("invalid color")
    let checked_visible = if (n.visible != null and not (n.visible is bool)) raise fail("invalid visibility")
    let checked_culling = if ((kind == 'scene3d' or contains(DRAWABLES,kind)) and n["frustum-culling"] != null and not (n["frustum-culling"] is bool)) raise fail("invalid frustum-culling flag")
    let checked_bounds = if (contains(DRAWABLES,kind) and n.bounds != null and
        (not t.vector(n.bounds,6) or any([for (i in 0 to 2) n.bounds[i] > n.bounds[i+3]]))) raise fail("bounds requires six ordered finite numbers")
    let checked_tone = if (kind == 'scene3d' and ((n["tone-mapping"] != null and not contains(['none', 'neutral', 'aces'], n["tone-mapping"])) or
        (n.exposure != null and (not numeric(n.exposure) or n.exposure <= 0)))) raise fail("invalid tone mapping or exposure")
    // the environment that lights the scene is an <environment> named by id
    let checked_lighting = if (kind == 'scene3d' and n.environment != null) definition(all_nodes, n.environment, 'environment')^
    let checked_intensity = if (kind == 'scene3d' and n["environment-intensity"] != null and
        (not numeric(n["environment-intensity"]) or n["environment-intensity"] < 0)) raise fail("invalid environment intensity")
    let children = content(n)
    let allowed = if (kind == 'scene3d') ['fog', 'postprocessing', *OBJECT_CHILDREN]
        else if (kind == 'group' or kind == 'rigid-body') OBJECT_CHILDREN
        else if (kind == 'lod') ['group']
        else if (kind == 'postprocessing') ['effect']
        else if (kind == 'simulation') ['variable']
        else if (kind == 'environment') ['group', 'lod', 'light', *DRAWABLES, 'resources', 'geometry', 'material', 'texture']
        else if (kind == 'skeleton' or kind == 'bone') ['bone']
        else if (kind == 'resources') ['geometry', 'material', 'texture', 'environment']
        else if (kind == 'animation-clip') ['keyframe-track'] else if (kind == 'motion-controller') ['motion-state']
        else if (contains(DRAWABLES, kind)) ['geometry', 'material'] else if (kind == 'material') ['texture'] else []
    let checked_children = if (any([for (child in children) not (child is element) or not contains(allowed, name(child))])) raise fail("invalid scene child")
    let checked_lod = if (kind == 'lod') {
        let distances=n.distances, hysteresis=value(n.hysteresis,0.1)
        let checked_distances=if (not (distances is array) or len(distances)<1 or len(distances)>16 or len(distances)!=len(children) or
            not all([for (d in distances) numeric(d)]) or distances[0]!=0 or
            any([for (i in 1 to (len(distances)-1)) distances[i]<=distances[i-1]])) raise fail("LOD requires ordered distances starting at zero, matching its groups")
        let checked_hysteresis=if (not numeric(hysteresis) or hysteresis<0 or hysteresis>=0.5) raise fail("LOD hysteresis must be in [0,0.5)")
        let checked_content=if (any([for (child in nodes(n)^) not contains(['lod','group','resources','geometry','material','texture',*DRAWABLES],name(child))])) raise fail("LOD levels accept geometric content only")
        true
    }
    let checked_uv = if (kind == 'texture' and
        ((n["uv-set"] != null and (not numeric(n["uv-set"]) or not contains([0, 1], n["uv-set"]))) or
         (n["uv-transform"] != null and not t.vector(n["uv-transform"], 6))))
        raise fail("texture requires uv-set 0 or 1 and a six-number UV transform")
    if (kind == 'fog') {
        let near=value(n.near,0.0),far=value(n.far,1000.0),density=value(n.density,0.002),falloff=value(n.falloff,0.1)
        if (not contains(['linear','exp2','height'],n.type) or (n.color!=null and not color(n.color)) or
            not numeric(near) or near<0 or not numeric(far) or far<=near or not numeric(density) or density<0 or
            not numeric(falloff) or falloff<0 or (n.height!=null and not numeric(n.height))) raise fail("invalid fog type, color or range") else true
    } else if (kind == 'effect') check_effect(n, all_nodes)^
    else if (kind == 'postprocessing') {
        let effects = content(n)
        if (len(effects) > 8) raise fail("postprocessing requires at most eight effect children")
        else if (any([for (i in 1 to (len(effects) - 1)) not display_effect(effects[i]) and
            any([for (j in 0 to (i - 1)) display_effect(effects[j])])]))
            raise fail("postprocessing requires linear effects before display effects") else true
    } else if (kind == 'animation-clip') {
        if (not text(n.id) or len(children) == 0 or len(children) > 1024 or
            (n.duration != null and not numeric(n.duration)) or
            (n.autoplay != null and not (n.autoplay is bool)) or (n.additive != null and not (n.additive is bool)))
            raise fail("invalid animation clip") else true
    } else if (kind == 'keyframe-track') {
        if (not animation.valid_track(n)) raise fail("invalid animation track")
        else {
            let path = split(n.path, ".")
            let targets = [for (target in all_nodes where target.id != null and string(target.id) == path[0]) target]
            // SH2/SH5: `<material or simulation>.uniforms.<name>` or `<mesh>.material.uniforms.<name>` drives a uniform
            let uniform_at = if (len(path) == 3 and path[1] == "uniforms") 1
                else if (len(path) == 4 and path[1] == "material" and path[2] == "uniforms") 2 else 0
            if (len(path) < 2 or len(path) > (if (uniform_at > 0) 4 else 3) or len(targets) != 1) raise fail("invalid animation target")
            else if (uniform_at > 0) {
                let target = targets[0]
                let owner = if (uniform_at == 1) target else if (not contains(DRAWABLES, name(target))) raise fail("invalid material animation path")
                    else mesh_material(target, all_nodes)^
                let uniforms_owner = name(owner) == 'simulation' or (contains(['material', 'effect'], name(owner)) and owner.type == 'shader')
                let authored = if (not uniforms_owner or owner.uniforms == null) null else owner.uniforms[path[len(path) - 1]]
                let expected = if (authored == null) null else uniform_type(authored)
                let components = len(n.values) / len(n.times)
                if (expected == null or n.type != expected or components > 16 or
                    components != (if (expected == 'number') 1 else if (expected == 'color') 3 else len(authored)))
                    raise fail("invalid animation binding type or components") else true
            } else {
                let target = targets[0]
                let property = path[len(path) - 1]
                let nested = len(path) == 3
                let checked_matrix = if (target.matrix != null and contains(["position", "scale", "quaternion"], property)) raise fail("cannot animate matrix node TRS")
                let checked_material = if (nested and (not contains(DRAWABLES, name(target)) or path[1] != "material")) raise fail("invalid material animation path")
                let kind = if (nested) 'material' else name(target)
                let owner = if (nested) mesh_material(target, all_nodes)^ else target
                let material_type = if (kind == 'material' and contains(["opacity", "color", "emissive"], property))
                    (if (contains(["color", "emissive"], property)) 'color' else 'number')
                    else if (kind == 'material' and contains(['standard', 'physical'], owner.type) and contains(["metalness", "roughness"], property)) 'number'
                    else if (kind == 'material' and owner.type!='shader' and contains(OUTDOOR_NUMBERS,property)) 'number'
                    else if (kind == 'material' and contains(['standard','physical'],owner.type) and property=="foliage-color") 'color'
                    else if (kind == 'material' and owner.type == 'physical' and contains(PHYSICAL_NUMBERS, property)) 'number'
                    else if (kind == 'material' and owner.type == 'physical' and contains(PHYSICAL_COLORS, property)) 'color' else null
                let expected = if (material_type != null) material_type else if (contains(["position", "scale"], property)) 'vector'
                    else if (kind == 'sprite' and contains(["size","center"],property)) 'vector'
                    else if ((kind == 'points' and property == "size") or (contains(['line','line-segments'],kind) and property == "width")) 'number'
                    else if (property == "quaternion") 'quaternion' else if (property == "visible") 'bool'
                    else if (property == "name") 'string' else if (property == "color" and contains(['material', 'light'], kind)) 'color'
                    else if (property == "morph-weights" and kind == 'mesh') 'vector'
                    else if ((kind == 'effect' and target.type == 'bloom' and contains(["strength", "threshold", "radius"], property)) or
                        (property == "opacity" and kind == 'material') or (contains(["intensity", "distance", "angle", "penumbra"], property) and kind == 'light') or
                        (contains(["fov", "near", "far", "aspect", "view_height"], property) and kind == 'camera')) 'number' else null
                let components = len(n.values) / len(n.times)
                if (expected == null or n.type != expected or
                    (contains(["position", "scale"], property) and components != 3) or
                    (kind == 'sprite' and contains(["size","center"],property) and components != 2) or
                    (property == "morph-weights" and (target["morph-weights"] == null or components != len(target["morph-weights"]))))
                    raise fail("invalid animation binding type or components") else true
            }
        }
    } else if (kind == 'camera') {
        let fov = value(n.fov, 50.0), near = value(n.near, 0.1), far = value(n.far, 1000.0)
        if (not contains(['perspective','orthographic'],n.type) or not numeric(fov) or fov <= 0 or fov >= 180 or not numeric(near) or (if (n.type == 'orthographic') near < 0 else near <= 0) or
            (n.view_height!=null and (not numeric(n.view_height) or n.view_height<=0)) or
            (n["infinite-far"] != null and not (n["infinite-far"] is bool)) or (n["infinite-far"] == true and n.type != 'perspective') or
            not numeric(far) or (n["infinite-far"] != true and far <= near) or (n.aspect != null and (not numeric(n.aspect) or n.aspect <= 0)))
            raise fail("invalid scene camera") else true
    } else if (kind == 'motion-controller') {
        // Lambda_Pkg_Motion.md §9.2: unique state names; `state` and every `next` name a state
        let names = [for (st in children) string(st.name)]
        if (not text(n.id) or len(children) == 0 or len(children) > 64 or len(unique(names)) != len(names) or
            (n.fade != null and (not numeric(n.fade) or n.fade < 0)) or (n.cue != null and not numeric(n.cue)) or
            (n.state != null and not contains(names, string(n.state))) or
            any([for (st in children) st.next != null and not contains(names, string(st.next))]))
            raise fail("invalid motion controller") else true
    } else if (kind == 'motion-state') {
        // a state's clip is an animation-clip that no other state uses and that does not autoplay
        let clips = [for (c in all_nodes where name(c) == 'animation-clip' and c.id != null and string(c.id) == string(n.clip)) c]
        let users = [for (c in all_nodes where name(c) == 'motion-state' and string(c.clip) == string(n.clip)) c]
        if (not text(n.name) or not text(n.clip) or len(clips) != 1 or clips[0].autoplay == true or len(users) != 1 or
            (n.loop != null and not contains(["repeat", "once", "pingpong"], string(n.loop))) or
            (n.fade != null and (not numeric(n.fade) or n.fade < 0)) or (n["time-scale"] != null and not numeric(n["time-scale"])))
            raise fail("invalid motion state") else true
    } else if (kind == 'environment') {
        // an environment is a scene of its own, captured from its origin: no cameras and no nested environments
        let inside = nodes(n)^
        let near = value(n.near, 0.1), far = value(n.far, 100.0)
        if (any([for (c in slice(inside, 1, len(inside))) contains(['camera', 'environment'], name(c))]))
            raise fail("environments cannot hold cameras or environments")
        else if (any([for (c in inside) name(c) == 'light' and c["cast-shadow"] == true])) raise fail("environment lights cannot cast shadows")
        // an image environment takes its light from `src` (a Radiance HDR file) instead of content
        else if (n.src != null and (not (n.src is string) or len(n.src) == 0 or len(inside) > 1))
            raise fail("an image environment names a file and holds no content")
        else if (not numeric(near) or not numeric(far) or near <= 0 or far <= near) raise fail("invalid environment background or clip planes")
        else true
    } else if (kind == 'geometry') check_geometry(n)^
    else if (kind == 'material' and n.type == 'shader') check_shader(n, all_nodes)^
    else if (kind == 'simulation') check_simulation(n)^
    else if (kind == 'material') {
        let checked_type = if (not contains(['basic', 'lambert', 'standard', 'physical'], n.type)) raise fail("unknown material type")
        let checked_outdoor = [for (key in OUTDOOR_NUMBERS where n[key]!=null)
            if (not numeric(n[key]) or not outdoor_range(key,n[key]) or
                (not starts_with(key,"wind-") and not contains(['standard','physical'],n.type))) raise fail("invalid outdoor material factor or model") else true]
        let checked_foliage_color = if (n["foliage-color"]!=null and (not contains(['standard','physical'],n.type) or not color(n["foliage-color"]))) raise fail("invalid foliage color")
        let checked_pbr = [for (key in ["metalness", "roughness"] where n[key] != null)
            if (not contains(['standard', 'physical'], n.type) or not numeric(n[key]) or n[key] < 0 or n[key] > 1) raise fail("invalid material metalness or roughness") else true]
        let checked_physical = [for (key in PHYSICAL_NUMBERS where n[key] != null)
            if (n.type != 'physical' or not numeric(n[key]) or not physical_range(key, n[key]))
                raise fail("invalid physical material factor") else true]
        let checked_physical_colors = [for (key in PHYSICAL_COLORS where n[key] != null)
            if (n.type != 'physical' or not color(n[key])) raise fail(if (key == "sheen-color") "invalid sheen color" else "invalid attenuation color") else true]
        let checked_transmission_coverage = if ((n.transmission != null or n["transmission-texture"] != null) and
            (n.depth_only == true or (not contains(['opaque', 'mask'], n["alpha-mode"]) and
                (n["alpha-mode"] == 'blend' or n["alpha-mode"] == "blend" or n.transparent == true or
                    (n.opacity != null and n.opacity < 1))))) raise fail("transmission requires opaque or masked coverage")
        let checked_opacity = if (n.opacity != null and (not numeric(n.opacity) or n.opacity < 0 or n.opacity > 1)) raise fail("invalid material opacity")
        let checked_transparent = if (n.transparent != null and not (n.transparent is bool)) raise fail("invalid transparency")
        let checked_depth = if (n.depth_only!=null and (not (n.depth_only is bool) or n.depth_only and (n.transparent==true or n.opacity!=null and n.opacity!=1)))
            raise fail("depth-only materials must be opaque")
        let checked_alpha = if (n["alpha-mode"] != null and not contains(['opaque', 'blend', 'mask'], n["alpha-mode"])) raise fail("invalid material alpha mode")
        let checked_cutoff = if (n["alpha-cutoff"] != null and (not numeric(n["alpha-cutoff"]) or n["alpha-cutoff"] <= 0 or n["alpha-cutoff"] > 1))
            raise fail("invalid material alpha cutoff")
        // the maps of a standard material each name a texture
        let checked_maps = [for (key in MAPS where n[key] != null)
            if (not contains(['standard', 'physical'], n.type)) raise fail("texture maps require a standard material")
            else if (contains(PHYSICAL_MAPS, key) and n.type != 'physical') raise fail("extension maps require a physical material") else definition(all_nodes, n[key], 'texture')^]
        let checked_strengths = if ((n["normal-scale"] != null and not numeric(n["normal-scale"])) or
            (n["occlusion-strength"] != null and (not numeric(n["occlusion-strength"]) or n["occlusion-strength"] < 0 or n["occlusion-strength"] > 1)))
            raise fail("invalid material normal scale or occlusion strength")
        let checked_side = if (n.side != null and not contains(['front', 'back', 'double'], n.side)) raise fail("invalid material side")
        let checked_emissive = if ((n.emissive != null and not color(n.emissive)) or
            (n["emissive-intensity"] != null and (not numeric(n["emissive-intensity"]) or n["emissive-intensity"] < 0)))
            raise fail("invalid material emission")
        let checked_texture = if (n.texture != null) {
            let d = definition(all_nodes, n.texture, 'texture')^
            if (len(children) != 0) raise fail("duplicate material texture") else true
        } else if (len(children) > 1) raise fail("duplicate material texture") else true
        true
    } else if (kind == 'light') {
        if (not contains(['ambient', 'directional', 'point', 'spot', 'hemisphere'], n.type) or (n.intensity != null and (not numeric(n.intensity) or n.intensity < 0)) or
            (n.distance != null and (not numeric(n.distance) or n.distance < 0)) or
            (n.angle != null and (not numeric(n.angle) or n.angle <= 0 or n.angle > 1.5707963267948966)) or
            (n.penumbra != null and (not numeric(n.penumbra) or n.penumbra < 0 or n.penumbra > 1)))
            raise fail("invalid light")
        // shadows (SP3: lights opt in)
        else if ((n["cast-shadow"] != null and not (n["cast-shadow"] is bool)) or
            (n["shadow-map-size"] != null and (not numeric(n["shadow-map-size"]) or floor(n["shadow-map-size"]) != n["shadow-map-size"] or
                n["shadow-map-size"] < 512 or n["shadow-map-size"] > 4096)) or
            (n["shadow-intensity"] != null and (not numeric(n["shadow-intensity"]) or n["shadow-intensity"] < 0 or n["shadow-intensity"] > 1)) or
            any([for (key in ["shadow-bias", "shadow-normal-bias"] where n[key] != null) not numeric(n[key])]) or
            (n["shadow-softness"] != null and (not numeric(n["shadow-softness"]) or n["shadow-softness"] < 0 or n["shadow-softness"] > 16)) or
            (n["shadow-extent"] != null and (not numeric(n["shadow-extent"]) or n["shadow-extent"] < 0)))
            raise fail("invalid light shadow")
        else if ((n["shadow-cascades"]!=null and (not numeric(n["shadow-cascades"]) or n["shadow-cascades"]<1 or n["shadow-cascades"]>4 or floor(n["shadow-cascades"])!=n["shadow-cascades"] or (n.type!='directional' and n["shadow-cascades"]!=1))) or
            (n["shadow-distance"]!=null and (not numeric(n["shadow-distance"]) or n["shadow-distance"]<=0)) or
            (n["shadow-split"]!=null and (not numeric(n["shadow-split"]) or n["shadow-split"]<0 or n["shadow-split"]>1)) or
            (n["shadow-transition"]!=null and (not numeric(n["shadow-transition"]) or n["shadow-transition"]<0 or n["shadow-transition"]>0.25))) raise fail("invalid directional shadow cascades")
        else if (n["cast-shadow"] == true and not contains(['directional', 'point', 'spot'], n.type))
            raise fail("cast-shadow requires a directional, point or spot light")
        // SP5: cube maps stay within 1024 texels a face
        else if (n.type == 'point' and n["shadow-map-size"] != null and n["shadow-map-size"] > 1024)
            raise fail("point-light shadow maps are at most 1024 texels")
        else true
    } else if (kind == 'texture') {
        let checked_source = if (not (n.src is string) or len(n.src) == 0) raise fail("texture requires source")
        let checked_raster_size = [for (key in ["width", "height"] where n[key] != null)
            if (not numeric(n[key]) or n[key] < 1 or floor(n[key]) != n[key]) raise fail("invalid SVG texture dimensions") else true]
        let checked_space = if (n["color-space"] != null and not contains(['srgb', 'linear'], n["color-space"])) raise fail("invalid texture color space")
        let checked_size = if (n["max-size"] != null and ((not numeric(n["max-size"]) or floor(n["max-size"]) != n["max-size"]) or n["max-size"] < 1 or n["max-size"] > 4096))
            raise fail("texture max-size must be an integer in 1..4096")
        let checked_sampler = [for (key in ["wrap-s", "wrap-t", "min-filter", "mag-filter"] where n[key] != null)
            if (not contains(if (key == "wrap-s" or key == "wrap-t") [10497, 33071, 33648]
                else if (key == "min-filter") [9728, 9729, 9984, 9985, 9986, 9987] else [9728, 9729], n[key]))
                raise fail("invalid texture sampler") else true]
        true
    } else if (contains(DRAWABLES, kind)) {
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
            not all([for (m in n.instances) t.affine(m)]))) raise fail("invalid instance matrices")
        let checked_skeleton = if (n.skeleton != null) {
            let rig = definition(all_nodes, n.skeleton, 'skeleton')^
            let bones = [for (b in nodes(rig)^ where name(b) == 'bone') b]
            if (len(bones) == 0 or len(bones) > 256 or n.instances != null) raise fail("invalid skeleton or skinned instances") else true
        } else true
        let checked_shadows = if (any([for (key in ["cast-shadow", "receive-shadow"] where n[key] != null) not (n[key] is bool)]))
            raise fail("invalid mesh shadow flags")
        let checked_weights = if (n["morph-weights"] != null and
            (not (n["morph-weights"] is array) or len(n["morph-weights"]) < 1 or len(n["morph-weights"]) > 2 or
            not all([for (x in n["morph-weights"]) numeric(x)]))) raise fail("invalid morph weights")
        // SH3: a shader mesh may draw instances its vertex shader places, with named per-instance numbers
        let drawn_with = mesh_material(n, all_nodes)^
        let drawn_geometry = if (n.geometry != null) definition(all_nodes, n.geometry, 'geometry')^ else geometries[0]
        let checked_drawable_material = if (kind != 'mesh' and (drawn_with.type != 'basic' or drawn_with.depth_only == true or
            n.skeleton != null or n["morph-weights"] != null or drawn_geometry["skin-indices"] != null or drawn_geometry["morph-positions"] != null))
            raise fail("drawables require basic color without deformation")
        let checked_drawable_shadows = if (kind != 'mesh' and (n["cast-shadow"] == true or n["receive-shadow"] == true)) raise fail("unlit drawables do not cast or receive shadows")
        let checked_size_mode = if (kind != 'mesh' and n["size-mode"] != null and not contains(['screen','world'], n["size-mode"])) raise fail("size-mode must be screen or world")
        let size = value(n.size, if (kind == 'sprite') [1.0,1.0] else 1.0)
        let checked_size = if (kind == 'sprite') {
            if (not t.vector(size,2) or any([for (v in size) v <= 0]) or (n.center != null and not t.vector(n.center,2))) raise fail("sprite size must be positive and its center finite") else true
        } else if (kind == 'points' and (not numeric(size) or size <= 0)) raise fail("drawable size must be positive")
        let width = value(n.width,1.0)
        let checked_width = if (contains(['line','line-segments'],kind) and (not numeric(width) or width <= 0)) raise fail("drawable width must be positive")
        // leave unsupported slots to material validation before resolving their UV streams.
        let map_textures = if (drawn_with.type == 'shader') [] else [
            *[for (key in (if (drawn_with.type == 'physical') ["texture", *MAPS] else if (drawn_with.type == 'standard') ["texture", *STANDARD_MAPS] else ["texture"]) where drawn_with[key] != null) definition(all_nodes, drawn_with[key], 'texture')^],
            *[for (c in content(drawn_with) where name(c) == 'texture') c]]
        let checked_map_uv = [for (tex in map_textures, let set = value(tex["uv-set"], 0))
            if ((set == 1 and drawn_geometry.uvs1 == null) or
                (tex["uv-set"] != null and set == 0 and (kind == 'mesh' or drawn_geometry["drawable-uvs"] == 'source') and drawn_geometry.type == 'buffer' and drawn_geometry.uvs == null))
                raise fail("material requests an absent texture coordinate set") else true]
        let shaded = drawn_with.type == 'shader'
        let count_value = n["instance-count"], data = n["instance-data"]
        let checked_shaded = if (not shaded and (count_value != null or data != null))
            raise fail("instance-count and instance-data require a shader material")
            else if (shaded and (n.skeleton != null or n["morph-weights"] != null))
            raise fail("shader materials cannot draw skinned or morphed geometry")
        let checked_count = if (count_value != null and (not numeric(count_value) or floor(count_value) != count_value or
            count_value < 1 or count_value > 16384 or n.instances != null))
            raise fail("instance-count must be an integer in 1..16384 without instance matrices")
        let count = if (count_value != null) count_value else if (n.instances != null) len(n.instances) else 1
        let checked_data = if (data == null) true
            else if (not (data is map)) raise fail("instance-data must be a map of at most 6 attributes")
            else {
                let entries = [for (k, v in data) [k, v]]
                if (len(entries) > 6) raise fail("instance-data must be a map of at most 6 attributes")
                else if (any([for (e in entries) not glsl_name(e[0]) or not (e[1] is array) or len(e[1]) < count or
                    len(e[1]) > 4 * count or len(e[1]) % count != 0 or not all([for (x in e[1]) numeric(x)])]))
                    raise fail("instance data needs 1 to 4 numbers per instance under a GLSL name") else true
            }
        true
    } else true
}
// source data validates without a draw topology; each actual consumer checks its primitive counts.
fn check_topology(n, all_nodes) bool^ {
    let geometries = [for (c in content(n) where name(c) == 'geometry') c]
    let g = if (n.geometry != null) definition(all_nodes, n.geometry, 'geometry')^ else geometries[0]
    let count = if (g.type != 'buffer') (if (g.type == 'box') 36 else 6)
        else if (g.indices != null and len(g.indices) > 0) len(g.indices) else len(g.positions) / 3
    let kind = name(n)
    if (kind == 'mesh') {
        if (count % 3 != 0) raise fail(if (g.indices != null and len(g.indices) > 0) "geometry index out of bounds" else "geometry requires triangles") else true
    } else {
        let valid = if (kind == 'sprite') count == 1 else if (kind == 'line') count >= 2
            else if (kind == 'line-segments') count >= 2 and count % 2 == 0 else count >= 1
        let vertices = if (kind == 'line') 4 * (count - 1) + 3 * max(count - 2, 0)
            else if (kind == 'line-segments') count * 2 else count * 4
        if (not valid) raise fail("invalid drawable topology component counts")
        else if (vertices > 262144) raise fail("expanded drawable vertex quota exceeded") else true
    }
}
fn normalized(n) element {
    let kind = name(n)
    let defaults = if (kind == 'group' and n.matrix == null) {position: [0.0,0.0,0.0], scale: [1.0,1.0,1.0],
        *:(if (n.quaternion == null) {rotation: [0.0,0.0,0.0]} else {})}
        else if (kind == 'camera') {fov:50.0,near:0.1,far:1000.0} else if (contains(DRAWABLES,kind)) {visible:true} else {}
    let attrs = {*: defaults, *: fields(n)}
    let children = [for (c in content(n)) normalized(c)]
    if (kind == 'scene3d') <scene3d *: attrs, *children>
    else if (kind == 'group') <group *: attrs, *children>
    else if (kind == 'lod') <lod *: attrs, *children>
    else if (kind == 'camera') <camera *: attrs>
    else if (kind == 'mesh') <mesh *: attrs, *children>
    else if (kind == 'points') <points *: attrs, *children>
    else if (kind == 'sprite') <sprite *: attrs, *children>
    else if (kind == 'line') <line *: attrs, *children>
    else if (kind == 'line-segments') <'line-segments' *: attrs, *children>
    else if (kind == 'resources') <resources *: attrs, *children>
    else if (kind == 'environment') <environment *: attrs, *children>
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
    let checked_topologies = [for (n in all_nodes where contains(DRAWABLES,name(n))) check_topology(n,all_nodes)^]
    let checked_chains = if (len([for (n in all_nodes where name(n) == 'postprocessing') n]) > 1)
        raise fail("scene accepts one postprocessing chain")
    let checked_bloom = if (any([for (n in all_nodes) name(n) == 'effect' and n.type == 'bloom']) and
        not opaque_color(scene.background)) raise fail("bloom currently requires an opaque viewport background")
    // physical descriptions share the native validator used by batch and live worlds.
    let checked_physics = if (any([for (n in all_nodes) contains(['physics','rigid-body','collider','joint'],name(n)) or n.physics != null]))
        lambda.sys.physics_validate(scene)^
    // SP5: each casting light owns a depth map and a texture unit
    let checked_fog = if (len([for (n in all_nodes where name(n)=='fog') n])>1) raise fail("one leaf fog belongs directly to the scene")
    let checked_shadows = if (sum([for (n in all_nodes where name(n) == 'light' and n["cast-shadow"] == true) value(n["shadow-cascades"],1)]) > 4)
        raise fail("shadow light quota exceeded")
    let cameras = [for (n in all_nodes where name(n) == 'camera') n]
    let checked_camera = if (scene.camera != null) definition(all_nodes, scene.camera, 'camera')^
        else if (len(cameras) == 0) raise fail("active camera is missing") else cameras[0]
    normalized(scene)
}
