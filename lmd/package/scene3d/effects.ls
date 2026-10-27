// D7.2.4v4 / S12.1.1v3: effects build pure shader/clip descriptions; Radiant owns playback.
import animation: .animation
import transforms: .transform

let VERTEX = "out vec2 surface_uv;
void main() {
    surface_uv = uv;
    gl_Position = projection_matrix * view_matrix * model_matrix * instance_matrix * vec4(position, 1.0);
}"

// all sampling clips outside the source instead of stretching its edge pixels into a smear.
let COMMON = "in vec2 surface_uv;
uniform sampler2D source_image;
uniform float phase, amount, rotation, aspect;
uniform vec3 tint;
const float TAU = 6.28318530718;
vec4 sample_image(vec2 p) {
    if (any(lessThan(p, vec2(0.0))) || any(greaterThan(p, vec2(1.0)))) return vec4(0.0);
    vec4 c = texture(source_image, p);
    return vec4(c.rgb * tint * c.a, c.a);
}
vec2 direction() { return vec2(cos(rotation) / aspect, sin(rotation)); }
"

// independent implementations inspired by Blotter's five material families, sharing one sampler ABI.
let PRESETS = [
    {id: 'channel-split', title: "Channel split", description: "Three colour channels drift out of register.",
        uniforms: {}, body: "void main() {
    vec2 d = direction() * amount * (0.55 + 0.45 * sin(TAU * phase));
    vec4 r = sample_image(surface_uv + d), g = sample_image(surface_uv), b = sample_image(surface_uv - d);
    fragment_color = vec4(r.r, g.g, b.b, max(r.a, max(g.a, b.a)));
}"},
    {id: 'liquid-distort', title: "Liquid distortion", description: "A flowing field bends the image like liquid glass.",
        uniforms: {frequency: 7.0, seed: 0.4}, body: "uniform float frequency, seed;
void main() {
    vec2 p = surface_uv * vec2(aspect, 1.0) * frequency;
    float t = TAU * phase;
    vec2 flow = vec2(sin(p.y + sin(p.x + t) + seed) * cos(p.x * 0.7 - t),
        cos(p.x + sin(p.y - t) + seed) * sin(p.y * 0.8 + t));
    fragment_color = sample_image(surface_uv + amount * flow / vec2(aspect, 1.0));
}"},
    {id: 'rolling-distort', title: "Rolling distortion", description: "A travelling wave rolls across the surface.",
        uniforms: {frequency: 3.0, spread: 0.22}, body: "uniform float frequency, spread;
void main() {
    vec2 axis = vec2(cos(rotation), sin(rotation));
    float p = dot((surface_uv - 0.5) * vec2(aspect, 1.0), axis);
    float centre = sin(TAU * phase) * 0.65;
    float band = exp(-pow((p - centre) / spread, 2.0));
    float wave = sin(TAU * (p * frequency - phase));
    fragment_color = sample_image(surface_uv + direction() * (amount * band * wave));
}"},
    {id: 'sliding-door', title: "Sliding doors", description: "Alternating strips slide apart and meet again.",
        uniforms: {divisions: 12.0}, body: "uniform float divisions;
void main() {
    vec2 axis = vec2(cos(rotation), sin(rotation));
    vec2 across = vec2(-axis.y, axis.x);
    float stripe = floor(dot(surface_uv * vec2(aspect, 1.0), across) * divisions);
    float signum = mod(stripe, 2.0) * 2.0 - 1.0;
    float travel = 0.5 - 0.5 * cos(TAU * phase);
    fragment_color = sample_image(surface_uv + direction() * (signum * amount * travel));
}"},
    {id: 'flies', title: "Flies", description: "The image dissolves into a restless field of dots.",
        uniforms: {cells: 90.0, radius: 0.34}, body: "uniform float cells, radius;
void main() {
    vec2 grid = vec2(cells, cells / aspect);
    vec2 p = surface_uv * grid;
    // derive coverage from continuous coordinates, before cell/seed discontinuities.
    float aa = max(max(fwidth(p.x), fwidth(p.y)), 0.001);
    vec4 dots = vec4(0.0);
    // neighbouring cells let moving dots cross cell boundaries without being clipped.
    for (int y = -1; y <= 1; y++) for (int x = -1; x <= 1; x++) {
        vec2 cell = floor(p) + vec2(x, y);
        float seed = fract(sin(dot(cell, vec2(127.1, 311.7))) * 43758.5453);
        float t = TAU * (phase + seed);
        vec2 drift = min(amount * 3.0, 0.8) * vec2(sin(t), cos(t));
        vec2 local = p - cell - 0.5 - drift;
        float coverage = 1.0 - smoothstep(radius - aa, radius + aa, length(local));
        dots += sample_image((cell + 0.5) / grid) * coverage;
    }
    fragment_color = mix(sample_image(surface_uv), min(dots, vec4(1.0)), min(amount * 4.0, 1.0));
}"}
]

pub fn catalog() array => [for (p in PRESETS) {id: p.id, title: p.title, description: p.description}]

pub fn material(kind, texture, uniforms = {}, options = {}) element^ {
    let matches = [for (p in PRESETS where string(p.id) == string(kind)) p]
    let checked_kind = if (len(matches) != 1) raise error("scene3d.effects: unknown effect")
    let checked_texture = if (not (texture is string or texture is symbol) or len(string(texture)) == 0)
        raise error("scene3d.effects: expected a texture ID")
    let preset = matches[0]
    let defaults = {phase: 0.0, amount: 0.06, rotation: 0.0, aspect: 2.0, tint: "#ffffff", *: preset.uniforms}
    let checked_map = if (not (uniforms is map)) raise error("scene3d.effects: expected a uniforms map")
    let checked_keys = [for (k, v in uniforms where defaults[k] == null)
        raise error("scene3d.effects: unknown uniform " ++ string(k))]
    let u = {*: defaults, *: uniforms}
    let checked_numbers = [for (k, v in u where k != 'tint' and not transforms.numeric(v))
        raise error("scene3d.effects: uniform requires a finite number")]
    let checked_ranges = if (u.aspect <= 0 or u.amount < 0 or
        (u.frequency != null and u.frequency <= 0) or (u.spread != null and u.spread <= 0) or
        (u.divisions != null and (u.divisions < 1 or u.divisions > 512)) or
        (u.cells != null and (u.cells < 1 or u.cells > 1024)) or
        (u.radius != null and (u.radius <= 0 or u.radius > 0.5))) raise error("scene3d.effects: uniform out of range");
    <material side: 'double', transparent: true, *: options, type: 'shader',
        vertex: VERTEX, fragment: COMMON ++ preset.body, uniforms: {*: u, source_image: string(texture)}>
}

// one cycle is phase 0..1; every preset closes that cycle without a jump at loop boundaries.
pub fn animate(material_id, duration = 8.0, options = {}) element^ {
    let checked_id = if (not (material_id is string or material_id is symbol) or len(string(material_id)) == 0 or contains(string(material_id), "."))
        raise error("scene3d.effects: expected a material ID without dots")
    let checked_duration = if (not transforms.numeric(duration) or duration <= 0)
        raise error("scene3d.effects: duration must be positive");
    animation.clip(string(material_id) ++ "-cycle", [animation.track(string(material_id) ++ ".uniforms.phase",
        [0.0, duration], [0.0, 1.0])], {autoplay: true, *: options, duration: duration})
}

// serialize through XML so paths, text and attribute characters retain their SVG meaning.
pub fn svg_texture(svg, options = {}) element^ {
    let checked = if (not (svg is element) or name(svg) != 'svg') raise error("scene3d.effects: expected an SVG element");
    <texture *: options, src: "data:image/svg+xml," ++ url_encode(format(svg, 'xml'))>
}
