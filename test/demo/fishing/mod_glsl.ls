// GLSL bodies for the lagoon's shader materials (Scene3D SH1: the host prepends #version, the
// vertex inputs and the camera uniforms). The water surface is painterly rather than physical:
// a depth tint over the lit floor, a fresnel sky sheen, a foam lip at the shore and toon sun
// glints from an animated wave normal. Caustics are a transparent film laid on the floor.
// Colours are linear light and the output is premultiplied, as the native compositor expects.

// world position and the vertex colour channels (r: depth fraction, g: shore foam) for both films
pub let SURFACE_VERTEX = "out vec3 world;
out vec3 info;
void main() {
    vec4 w = model_matrix * vec4(position, 1.0);
    world = w.xyz;
    info = color;
    gl_Position = projection_matrix * view_matrix * w;
}"

let NOISE = "float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 s = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), s.x), mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), s.x), s.y);
}
// slope of one travelling sine wave: direction d, wavelength l, speed c, amplitude a
vec2 wave(vec2 p, float t, vec2 d, float l, float c, float a) {
    float k = 6.2831853 / l;
    return d * (a * k * cos(k * (dot(d, p) - c * t)));
}
"

pub let WATER_FRAGMENT = "uniform float time;
uniform vec3 sun, shallow, deep, sky;
in vec3 world;
in vec3 info;
" ++ NOISE ++ "
void main() {
    vec2 p = world.xz;
    float t = time;
    vec2 g = wave(p, t, normalize(vec2(1.0, 0.35)), 3.4, 0.9, 0.03)
           + wave(p, t, normalize(vec2(-0.6, 1.0)), 2.1, 0.75, 0.022)
           + wave(p, t, normalize(vec2(0.25, -1.0)), 1.3, 0.6, 0.014)
           + wave(p, t, normalize(vec2(-1.0, -0.4)), 0.8, 0.5, 0.009);
    // fine chop: the slope of value noise, moving with the breeze
    vec2 q = p * 2.3 + vec2(t * 0.35, t * 0.18);
    vec2 r = p * vec2(13.0, 9.0) + vec2(-t * 0.9, t * 0.7);
    float e = 0.05;
    float n0 = noise(q), m0 = noise(r);
    g = g * 0.6 + vec2(noise(q + vec2(e, 0.0)) - n0, noise(q + vec2(0.0, e)) - n0) / e * 0.03;
    g += vec2(noise(r + vec2(e, 0.0)) - m0, noise(r + vec2(0.0, e)) - m0) / e * 0.042;
    vec3 n = normalize(vec3(-g.x, 1.0, -g.y));
    vec3 v = normalize(camera_position - world);
    float depth = smoothstep(0.0, 1.0, info.r);
    vec3 tint = mix(shallow, deep, depth);
    float alpha = mix(0.07, 0.42, depth);
    // the sky reflected at grazing angles, and slow broad sheen patches
    float fresnel = pow(1.0 - max(dot(n, v), 0.0), 4.0);
    float sheen = smoothstep(0.55, 0.85, noise(p * 0.12 + vec2(t * 0.015, -t * 0.01)));
    vec3 c = mix(tint, sky, min(1.0, fresnel * 0.9 + sheen * 0.18));
    alpha = max(alpha, fresnel * 0.55 + sheen * 0.08);
    // a broken white lip where the water meets the beach
    float lip = info.g * smoothstep(0.35, 0.75, noise(p * 1.6 + vec2(t * 0.25, t * 0.1)));
    // toon glints: the sun mirrored in the wave normal, cut to hard white flecks
    float spec = pow(max(dot(reflect(-v, n), sun), 0.0), 900.0);
    // the glitter comes in drifting bands rather than one even sheet
    float band = smoothstep(0.5, 0.56, noise(p * vec2(0.22, 0.5) + vec2(t * 0.06, -t * 0.03)));
    float glint = smoothstep(0.42, 0.62, spec) * band;
    float white = max(lip * 0.85, glint);
    c = mix(c, vec3(1.0), white);
    alpha = mix(alpha, 0.96, white);
    fragment_color = vec4(c * alpha, alpha);
}"

pub let CAUSTIC_FRAGMENT = "uniform float time, strength;
in vec3 world;
in vec3 info;
" ++ NOISE ++ "
// two drifting noise fields; light gathers where they cross, leaving thin uneven filaments
float web(vec2 p, float t) {
    float a = noise(p + vec2(t * 0.42, t * 0.11));
    float b = noise(p * 1.31 - vec2(t * 0.17, t * 0.38));
    return smoothstep(0.06, 0.0, abs(a - b));
}
void main() {
    // a slow warp keeps the cells from reading as tiles; a patch noise lets light pool unevenly
    vec2 p = world.xz * 2.2;
    float patch = smoothstep(0.2, 0.8, noise(world.xz * 0.14 + vec2(time * 0.02, 0.0)));
    float l = (web(p, time) * 0.7 + web(p * 1.8 + 5.3, time * 1.3) * 0.4) * (0.3 + 0.7 * patch);
    // brightest in the shallows, fading with depth and at the very edge of the beach
    float fade = smoothstep(0.0, 0.08, info.r) * (1.0 - 0.7 * smoothstep(0.2, 1.0, info.r));
    float a = clamp(l * fade * strength, 0.0, 1.0);
    fragment_color = vec4(vec3(1.0, 0.96, 0.82) * a, a);
}"
