// GPU programs for the butterflies (SH1, SH5): the swarm simulation, the flexible flapping wing,
// and the two materials that draw it. Lambda only composes these sources and sets their uniforms.
import atlas: .mod_atlas

// --- shared chunks ---

// per-butterfly constants hashed from its record index, so Lambda sends no per-butterfly data;
// species are drawn with the atlas's weights
let TRAITS = "float hash1(float n) { return fract(sin(n * 12.9898 + 78.233) * 43758.5453); }
float trait(int id, float k) { return hash1(float(id) * 1.618 + k * 31.7); }
// mostly small, some medium, a few large
float body_scale(int id) {
    float r = trait(id, 1.0), u = trait(id, 2.0);
    return r < 0.06 ? mix(2.2, 3.8, u) : r < 0.24 ? mix(1.2, 1.9, u) : mix(0.5, 1.1, u);
}
const float WEIGHTS[16] = float[16](" ++ join([for (sp in atlas.SPECIES) string(sp.weight)], ", ") ++ ");
float species_of(int id) {
    float total = 0.0;
    for (int k = 0; k < 16; k++) total += WEIGHTS[k];
    float pick = trait(id, 17.0) * total;
    for (int k = 0; k < 16; k++) { pick -= WEIGHTS[k]; if (pick <= 0.0) return float(k); }
    return 15.0;
}
"

// the flexible wing: a grid vertex of one wing, hinged on the body axis (+y forward); the span bends
// against the stroke (tips trail the root) and the hindwing lags the forewing
let WING = "float stroke(float ph) { return ph + 0.4 * sin(ph); }
float flap_angle(float ph, float amplitude) { float s = 0.5 + 0.5 * sin(stroke(ph)); return mix(-0.35, amplitude, s * s); }
vec3 wing(vec3 q, float ph, float flex, float bend, float lag, float amplitude, out vec3 n) {
    float side = sign(q.x), s = abs(q.x);
    float phc = ph - flex * lag * (0.5 - q.y);
    float a0 = flap_angle(phc, amplitude);
    float k = -flex * bend * cos(stroke(phc));
    float th = a0 + k * s;
    // a constant-curvature arc keeps the wing's length while it bends
    vec2 xz = abs(k) < 1e-3 ? s * vec2(cos(a0), sin(a0)) : vec2(sin(th) - sin(a0), cos(a0) - cos(th)) / k;
    n = vec3(-side * sin(th), 0.0, cos(th));
    // the downstroke lifts the body a little
    return vec3(side * xz.x, q.y, xz.y - 0.05 * flex * sin(stroke(ph)));
}
// a species' cell in the 4 × 4 atlas, head up; v = 1 is the top of the image
vec2 atlas_uv(float cell, vec2 at) {
    vec2 inner = mix(vec2(0.02), vec2(0.98), at);
    return vec2((mod(cell, 4.0) + inner.x) / 4.0, 1.0 - (floor(cell / 4.0) + 1.0 - inner.y) / 4.0);
}
float light_on(vec3 normal) { return 0.62 + 0.38 * abs(dot(normal, normalize(vec3(0.3, 0.6, 0.8)))); }
"

// --- the swarm simulation: 64 × 64 records of position and velocity ---

// gradient noise and its curl: a divergence-free flow, so swarms swirl without bunching up
let FLOW = "vec3 lattice(vec3 i) {
    vec3 p = vec3(dot(i, vec3(127.1, 311.7, 74.7)), dot(i, vec3(269.5, 183.3, 246.1)), dot(i, vec3(113.5, 271.9, 124.6)));
    return fract(sin(p) * 43758.5453) * 2.0 - 1.0;
}
float gnoise(vec3 p) {
    vec3 i = floor(p), f = fract(p), u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float c000 = dot(lattice(i), f), c100 = dot(lattice(i + vec3(1, 0, 0)), f - vec3(1, 0, 0));
    float c010 = dot(lattice(i + vec3(0, 1, 0)), f - vec3(0, 1, 0)), c110 = dot(lattice(i + vec3(1, 1, 0)), f - vec3(1, 1, 0));
    float c001 = dot(lattice(i + vec3(0, 0, 1)), f - vec3(0, 0, 1)), c101 = dot(lattice(i + vec3(1, 0, 1)), f - vec3(1, 0, 1));
    float c011 = dot(lattice(i + vec3(0, 1, 1)), f - vec3(0, 1, 1)), c111 = dot(lattice(i + vec3(1, 1, 1)), f - vec3(1, 1, 1));
    return mix(mix(mix(c000, c100, u.x), mix(c010, c110, u.x), u.y), mix(mix(c001, c101, u.x), mix(c011, c111, u.x), u.y), u.z);
}
vec3 flow(vec3 p) {
    const float e = 0.12;
    vec3 dx = vec3(e, 0, 0), dy = vec3(0, e, 0), dz = vec3(0, 0, e), b = vec3(31.4, -12.7, 7.1), c = vec3(-19.3, 41.9, -23.5);
    float dCdy = gnoise(p + c + dy) - gnoise(p + c - dy), dBdz = gnoise(p + b + dz) - gnoise(p + b - dz);
    float dAdz = gnoise(p + dz) - gnoise(p - dz), dCdx = gnoise(p + c + dx) - gnoise(p + c - dx);
    float dBdx = gnoise(p + b + dx) - gnoise(p + b - dx), dAdy = gnoise(p + dy) - gnoise(p - dy);
    return vec3(dCdy - dBdz, dAdz - dCdx, dBdx - dAdy) / (2.0 * e);
}
"
// five wandering attractors are the clusters' cores; butterflies outrun them, so stragglers stream behind
let FLOCK = "uniform float time;
uniform vec2 bounds;   // the visible half-width and half-height at the swarm's depth
vec3 attractor(float g, float t) {
    vec3 f = vec3(mix(0.05, 0.11, hash1(g * 3.0 + 101.0)), mix(0.07, 0.14, hash1(g * 3.0 + 102.0)), mix(0.05, 0.1, hash1(g * 3.0 + 103.0)));
    vec3 ph = vec3(hash1(g * 5.0 + 201.0), hash1(g * 5.0 + 202.0), hash1(g * 5.0 + 203.0)) * 6.2832;
    return vec3(bounds.x * 0.85 * sin(t * f.x + ph.x), bounds.y * 0.8 * sin(t * f.y + ph.y), 10.0 * sin(t * f.z + ph.z) - 4.0);
}
// each butterfly switches cluster every 40 s or so, at its own moment: ribbons form between swarms
float group_of(int id, float t) { return floor(hash1(float(id) + floor(t / 40.0 + trait(id, 9.0)) * 97.31) * 5.0); }
bool is_large(int id) { return body_scale(id) > 2.0; }
// large ones and a sixth of the rest fly alone
bool is_loner(int id) { return is_large(id) || trait(id, 3.0) < 0.15; }
float pull_of(int id) { return is_loner(id) ? mix(0.05, 0.25, trait(id, 4.0)) : mix(0.8, 1.4, trait(id, 4.0)); }
float speed_of(int id) { return is_large(id) ? mix(5.0, 8.0, trait(id, 5.0)) : mix(7.0, 12.0, trait(id, 5.0)); }
// a personal offset gives each cluster some volume
vec3 offset_of(int id) {
    float r = 3.5 * pow(trait(id, 6.0), 1.0 / 3.0), th = trait(id, 7.0) * 6.2832, ph = acos(trait(id, 8.0) * 2.0 - 1.0);
    return r * vec3(sin(ph) * cos(th), sin(ph) * sin(th), cos(ph));
}
vec3 jitter(int id, float k) { return vec3(trait(id, k), trait(id, k + 1.0), trait(id, k + 2.0)) * 2.0 - 1.0; }
int record() { ivec2 at = ivec2(gl_FragCoord.xy); return at.y * int(resolution.x) + at.x; }
"
// records start empty (w = 0); the first step spawns each one, so a rewind respawns the swarm
pub let POSITION_STEP = TRAITS ++ FLOCK ++ "void main() {
    int id = record();
    vec4 here = texelFetch(position, ivec2(gl_FragCoord.xy), 0);
    if (here.w == 0.0) {
        vec3 j = jitter(id, 11.0), a = attractor(group_of(id, time), time);
        vec2 spread = is_loner(id) ? j.xy * bounds : a.xy + j.xy * 8.4;
        fragment_color = vec4(spread, mix(-12.0, 8.0, trait(id, 14.0)), 1.0);
        return;
    }
    fragment_color = vec4(here.xyz + texelFetch(velocity, ivec2(gl_FragCoord.xy), 0).xyz * delta, 1.0);
}"
pub let VELOCITY_STEP = TRAITS ++ FLOW ++ FLOCK ++ "void main() {
    int id = record();
    vec4 here = texelFetch(position, ivec2(gl_FragCoord.xy), 0);
    vec3 v = texelFetch(velocity, ivec2(gl_FragCoord.xy), 0).xyz;
    if (here.w == 0.0) { fragment_color = vec4(jitter(id, 21.0) * vec3(1.0, 1.0, 0.3), 1.0); return; }
    vec3 p = here.xyz;
    vec3 c = flow(p * 0.045 + vec3(time * 0.06, 0.0, -time * 0.042));
    c.z *= 0.5;
    vec3 dir = c / (length(c) + 1e-6);
    // far from its cluster a butterfly beelines to it; close in, the flow takes over and it swirls
    vec3 to = attractor(group_of(id, time), time) + offset_of(id) - p;
    float d = length(to) + 1e-6;
    dir += to / d * pull_of(id) * clamp((d - 1.5) / 9.0, 0.0, 1.6);
    // soft walls keep loners near the visible area
    dir.x += 1.5 * (step(p.x, -1.15 * bounds.x) - step(1.15 * bounds.x, p.x));
    dir.y += 1.5 * (step(p.y, -1.1 * bounds.y) - step(1.1 * bounds.y, p.y));
    dir.z += 1.5 * (step(p.z, -22.0) - step(14.0, p.z));
    vec3 desired = dir / (length(dir) + 1e-6) * speed_of(id);
    fragment_color = vec4(v + (desired - v) * min(1.0, 2.6 * delta), 1.0);
}"

// --- drawing ---

let SHADED = "uniform sampler2D atlas;
in vec2 surface;
in float shade, fog;
void main() {
    vec4 c = texture(atlas, surface);
    if (c.a < 0.08) discard;
    vec3 color = c.rgb * shade;
    // undersides read paler and duller
    if (!gl_FrontFacing) color = mix(color, vec3(dot(color, vec3(0.3, 0.59, 0.11))), 0.4) * 0.9 + 0.06;
    fragment_color = vec4(mix(color, vec3(1.0), fog * 0.7), c.a);
}"
pub let FRAGMENT = SHADED

// the swarm: one instance per record, placed and turned by the simulation; flex grows with size
pub let SWARM_VERTEX = TRAITS ++ WING ++ "uniform sampler2D positions, velocities;
uniform float time, columns;
out vec2 surface;
out float shade, fog;
void main() {
    int id = gl_InstanceID;
    ivec2 at = ivec2(id % int(columns), id / int(columns));
    vec3 p = texelFetch(positions, at, 0).xyz, v = texelFetch(velocities, at, 0).xyz;
    float scale = body_scale(id), phase = trait(id, 16.0) * 6.2832;
    float flex = clamp(0.35 + (scale - 1.0) * 0.55, 0.35, 1.25);
    vec3 n, q = wing(position, time * mix(16.0, 26.0, trait(id, 15.0)) / sqrt(scale) + phase, flex, 2.6, 1.05, 1.3, n);
    // +y along the velocity, the wing tops tilted toward the viewer, and a little roll
    vec3 f = normalize(v + vec3(0.0, 1e-4, 0.0)), ref = normalize(vec3(0.0, 0.35, 1.0));
    vec3 up = ref - f * dot(f, ref);
    up = length(up) < 0.05 ? vec3(0.0, 1.0, 0.0) : normalize(up);
    vec3 right = cross(f, up);
    float roll = 0.35 * sin(time * 1.7 + phase);
    mat3 basis = mat3(right * cos(roll) - up * sin(roll), f, up * cos(roll) + right * sin(roll));
    vec4 eye = view_matrix * vec4(p + basis * q * scale, 1.0);
    shade = light_on(normalize(basis * n));
    fog = smoothstep(62.0, 95.0, -eye.z);
    surface = atlas_uv(species_of(id), uv);
    gl_Position = projection_matrix * eye;
}"

// one butterfly, its back tilted toward the viewer and its heading swaying slowly so the pattern and
// the bending wings both read; every motion parameter is a uniform the sliders drive
pub let SOLO_VERTEX = WING ++ "uniform float time, species, flex, curl, lag, beat, amplitude;
out vec2 surface;
out float shade, fog;
void main() {
    vec3 n, q = wing(position, time * beat, flex, curl, lag, amplitude, n);
    vec3 back = normalize(vec3(0.0, -0.45, 1.0));
    float sway = 0.55 * sin(time * 0.4);
    vec3 heading = vec3(sin(sway), cos(sway), 0.0);
    vec3 f = normalize(heading - back * dot(heading, back));
    mat3 basis = mat3(cross(f, back), f, back);
    shade = light_on(normalize(basis * n));
    fog = 0.0;
    surface = atlas_uv(species, uv);
    gl_Position = projection_matrix * view_matrix * model_matrix * vec4(basis * q, 1.0);
}"
