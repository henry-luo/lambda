#pragma once

#include <stddef.h>
#include <stdint.h>

struct ImageSurface;
struct NativeGlContext;

// native names never cross the graphics waist (D7.4.1v2); IDs are context branded.
struct NativeGlResource { uint64_t id; };
enum NativeGlKind { NATIVE_GL_BUFFER, NATIVE_GL_VERTEX_ARRAY, NATIVE_GL_TEXTURE,
    NATIVE_GL_PROGRAM, NATIVE_GL_TARGET, NATIVE_GL_SHADER, NATIVE_GL_FRAMEBUFFER, NATIVE_GL_RENDERBUFFER };
struct NativeGlStats {
    uint64_t generation, allocated_bytes, draws, frames;
    uint64_t gpu_bytes, cpu_bytes, shader_normalize_calls, shader_normalize_us;
    uint32_t resources;
    char driver[256], version[128], shading_language[128];
};
struct NativeGlAttribute {
    NativeGlResource buffer;
    unsigned location, components, stride, offset, divisor;
};
// What a draw samples: a texture (native_gl_texture or native_gl_data_texture), the
// color cube of a cube target, the depth of a depth target read through a
// comparison sampler (sampler2DShadow / samplerCubeShadow), or the color of a float target.
enum NativeGlBindingKind { NATIVE_GL_BIND_TEXTURE, NATIVE_GL_BIND_CUBE, NATIVE_GL_BIND_SHADOW, NATIVE_GL_BIND_FLOAT,
    NATIVE_GL_BIND_TARGET, NATIVE_GL_BIND_MULTISAMPLE }; // resolved or per-sample drawing-buffer color
struct NativeGlBinding {
    const char* sampler; // the shader's sampler uniform; a static string
    NativeGlResource resource;
    NativeGlBindingKind kind;
};
static constexpr unsigned NATIVE_GL_BINDING_LIMIT = 12;
struct NativeGlDraw {
    NativeGlResource program, vertices;
    unsigned count, instances;
    bool indexed, transparent;
    int side; // 0 front, 1 back, 2 double
    bool clockwise;
    bool depth_only = false;
    // fragment alpha becomes multisample coverage: cut-out edges antialias without depth sorting
    bool alpha_to_coverage = false;
    // Binding i takes texture unit i and sets its sampler to it. GL rejects a draw
    // whose samplers of different types share a unit, so a program's samplers
    // must all be bound; samplers it lacks are skipped.
    NativeGlBinding bindings[NATIVE_GL_BINDING_LIMIT] = {};
    unsigned binding_count = 0;
    bool depth_test = true;
};
struct NativeGlUniform { NativeGlResource program; int location; uint64_t generation; };

NativeGlContext* native_gl_create(bool enabled, char* diagnostic, size_t capacity);
void native_gl_destroy(NativeGlContext* context);
void native_gl_lose(NativeGlContext* context);
bool native_gl_stats(NativeGlContext* context, NativeGlStats* stats);
const char* native_gl_diagnostic(NativeGlContext* context);
bool native_gl_extension_supported(NativeGlContext* context, const char* name);
bool native_gl_valid(NativeGlContext* context, NativeGlResource resource, NativeGlKind kind);
void native_gl_release(NativeGlContext* context, NativeGlResource resource);
NativeGlResource native_gl_buffer(NativeGlContext* context, const void* data, size_t bytes, bool retain_index_data=true);
NativeGlResource native_gl_vertices(NativeGlContext* context, const NativeGlAttribute* attributes,
    unsigned count, NativeGlResource indices);
struct NativeGlSampler {
    unsigned wrap_s=0x812f,wrap_t=0x812f; // CLAMP_TO_EDGE
    unsigned min_filter=0x2601,mag_filter=0x2601; // LINEAR
    unsigned max_size=4096; // bound the uploaded resolution without changing the source image
    bool srgb=true; // false for data maps (normals, roughness, occlusion), sampled without sRGB decoding
};
NativeGlResource native_gl_texture(NativeGlContext* context, const ImageSurface* image,
    const NativeGlSampler& sampler = {});
// A float data texture a shader reads with texelFetch: `height` records of
// `width` RGBA32F texels, nearest-sampled and clamped, or linearly filtered
// when `filtered` (a lookup table read with texture()). Contents arrive through
// native_gl_data_texture_update, a whole number of records at a time.
NativeGlResource native_gl_data_texture(NativeGlContext* context, unsigned width, unsigned height, bool filtered = false);
bool native_gl_data_texture_update(NativeGlContext* context, NativeGlResource texture,
    const float* rgba, unsigned rows);
NativeGlResource native_gl_program(NativeGlContext* context, const char* vertex, const char* fragment);
unsigned native_gl_uniform_count(NativeGlContext* context, NativeGlResource program);
bool native_gl_uniform_info(NativeGlContext* context, NativeGlResource program, unsigned index,
    char* name, size_t capacity, unsigned* type, int* size);
NativeGlUniform native_gl_uniform(NativeGlContext* context, NativeGlResource program, const char* name);
bool native_gl_uniform_set(NativeGlContext* context, NativeGlUniform uniform,
    const float* values, unsigned components, unsigned count = 1);
NativeGlResource native_gl_target(NativeGlContext* context, unsigned width, unsigned height,
    unsigned samples = 4, bool hdr = false);
bool native_gl_begin(NativeGlContext* context, NativeGlResource target, const float background[4]);
// transfer opaque visibility to the display target before forward transparent draws.
bool native_gl_copy_depth(NativeGlContext* context, NativeGlResource source, NativeGlResource target);
// A depth texture to render shadow casters into: `size` square, or six faces when
// `cube`. It samples with depth comparison; outside its edges it reads as lit.
NativeGlResource native_gl_depth_target(NativeGlContext* context, unsigned size, bool cube);
// An RGBA16F cube map with `levels` mip levels and a depth buffer, for rendering
// and prefiltering environments. Its values are linear and may exceed 1.
NativeGlResource native_gl_cube_target(NativeGlContext* context, unsigned size, unsigned levels);
// Begin drawing into one `face` (0..5 as +X −X +Y −Y +Z −Z; 0 for a 2D depth
// target) at mip `level` of a depth or cube target. A depth target clears depth
// only and ignores `background`; a cube target clears to it.
bool native_gl_begin_layer(NativeGlContext* context, NativeGlResource target, unsigned face, unsigned level,
    const float background[4]);
// An RGBA16F 2D texture drawn region by region and sampled linearly (an atlas such as
// a prefiltered environment), with a depth buffer when `depth`.
NativeGlResource native_gl_float_target(NativeGlContext* context, unsigned width, unsigned height, bool depth,
    bool exact = false);
// `exact` targets hold RGBA32F records sampled nearest (GPU simulation state); this replaces
// the whole color image with `width` × `height` RGBA float records.
bool native_gl_float_target_update(NativeGlContext* context, NativeGlResource target, const float* rgba);
// Begin drawing into one region of a float target: the viewport and scissor cover it,
// and with a `background` the region's color (and depth) is cleared to it.
bool native_gl_begin_region(NativeGlContext* context, NativeGlResource target, unsigned x, unsigned y,
    unsigned width, unsigned height, const float* background);
bool native_gl_draw(NativeGlContext* context, const NativeGlDraw* draw);
ImageSurface* native_gl_snapshot(NativeGlContext* context, NativeGlResource target);

struct WebGlCommand;
struct WebGlReply;
struct WebGlOptions;
char* native_gl_shader_source(const char* source, bool fragment);
bool native_gl_webgl_init(NativeGlContext*, unsigned, unsigned, const WebGlOptions*);
bool native_gl_webgl_resize(NativeGlContext*, unsigned, unsigned);
bool native_gl_webgl_call(NativeGlContext*, const WebGlCommand*, WebGlReply*);
void native_gl_webgl_error(NativeGlContext*, unsigned);
ImageSurface* native_gl_webgl_snapshot(NativeGlContext*);
