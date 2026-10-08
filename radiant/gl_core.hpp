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
struct NativeGlDraw {
    NativeGlResource program, vertices, texture;
    unsigned count, instances;
    bool indexed, transparent;
    int side; // 0 front, 1 back, 2 double
    bool clockwise;
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
NativeGlResource native_gl_buffer(NativeGlContext* context, const void* data, size_t bytes);
NativeGlResource native_gl_vertices(NativeGlContext* context, const NativeGlAttribute* attributes,
    unsigned count, NativeGlResource indices);
struct NativeGlSampler {
    unsigned wrap_s=0x812f,wrap_t=0x812f; // CLAMP_TO_EDGE
    unsigned min_filter=0x2601,mag_filter=0x2601; // LINEAR
};
NativeGlResource native_gl_texture(NativeGlContext* context, const ImageSurface* image,
    const NativeGlSampler& sampler = {});
NativeGlResource native_gl_program(NativeGlContext* context, const char* vertex, const char* fragment);
unsigned native_gl_uniform_count(NativeGlContext* context, NativeGlResource program);
bool native_gl_uniform_info(NativeGlContext* context, NativeGlResource program, unsigned index,
    char* name, size_t capacity, unsigned* type, int* size);
NativeGlUniform native_gl_uniform(NativeGlContext* context, NativeGlResource program, const char* name);
bool native_gl_uniform_set(NativeGlContext* context, NativeGlUniform uniform,
    const float* values, unsigned components, unsigned count = 1);
NativeGlResource native_gl_target(NativeGlContext* context, unsigned width, unsigned height,
    unsigned samples = 4);
bool native_gl_begin(NativeGlContext* context, NativeGlResource target, const float background[4]);
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
