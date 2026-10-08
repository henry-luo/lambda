#include "gl_core.hpp"
#include "../lib/mem.h"
#include "../lib/log.h"
#include "../lib/str.h"
#include <math.h>

// isolate core-profile declarations from the shell's legacy OpenGL headers.
#if !defined(LAMBDA_NO_GUI) && !defined(LAMBDA_HEADLESS)
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#ifdef __APPLE__
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#else
#include <GL/glcorearb.h>
#endif
#include <pthread.h>
#define NATIVE_GL_ENABLED 1
#endif

#include "view.hpp"

static constexpr size_t GL_BYTE_LIMIT = 128u * 1024u * 1024u;
static constexpr unsigned GL_RESOURCE_LIMIT = 4096;
static constexpr unsigned GL_DIMENSION_LIMIT = 4096;
static uint64_t native_gl_next_id = 1;
static unsigned native_gl_context_count;

static uint64_t native_gl_id() { return __atomic_fetch_add(&native_gl_next_id, 1, __ATOMIC_RELAXED); }

#ifdef NATIVE_GL_ENABLED
// the same table supplies native scenes and future imperative clients.
#define GL_FUNCTIONS(X) \
    X(const GLubyte*, GetString, (GLenum)) \
    X(void, GetIntegerv, (GLenum, GLint*)) \
    X(GLenum, GetError, ()) \
    X(void, GenBuffers, (GLsizei, GLuint*)) \
    X(void, DeleteBuffers, (GLsizei, const GLuint*)) \
    X(void, BindBuffer, (GLenum, GLuint)) \
    X(void, BufferData, (GLenum, GLsizeiptr, const void*, GLenum)) \
    X(void, GenVertexArrays, (GLsizei, GLuint*)) \
    X(void, DeleteVertexArrays, (GLsizei, const GLuint*)) \
    X(void, BindVertexArray, (GLuint)) \
    X(void, EnableVertexAttribArray, (GLuint)) \
    X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)) \
    X(void, VertexAttribDivisor, (GLuint, GLuint)) \
    X(void, GenTextures, (GLsizei, GLuint*)) \
    X(void, DeleteTextures, (GLsizei, const GLuint*)) \
    X(void, BindTexture, (GLenum, GLuint)) \
    X(void, ActiveTexture, (GLenum)) \
    X(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*)) \
    X(void, TexParameteri, (GLenum, GLenum, GLint)) \
    X(GLuint, CreateShader, (GLenum)) \
    X(void, ShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint*)) \
    X(void, CompileShader, (GLuint)) \
    X(void, GetShaderiv, (GLuint, GLenum, GLint*)) \
    X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*)) \
    X(void, DeleteShader, (GLuint)) \
    X(GLuint, CreateProgram, ()) \
    X(void, AttachShader, (GLuint, GLuint)) \
    X(void, LinkProgram, (GLuint)) \
    X(void, GetProgramiv, (GLuint, GLenum, GLint*)) \
    X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*)) \
    X(void, DeleteProgram, (GLuint)) \
    X(void, UseProgram, (GLuint)) \
    X(GLint, GetUniformLocation, (GLuint, const GLchar*)) \
    X(void, GetActiveUniform, (GLuint, GLuint, GLsizei, GLsizei*, GLint*, GLenum*, GLchar*)) \
    X(void, Uniform1i, (GLint, GLint)) \
    X(void, Uniform1fv, (GLint, GLsizei, const GLfloat*)) \
    X(void, Uniform3fv, (GLint, GLsizei, const GLfloat*)) \
    X(void, Uniform4fv, (GLint, GLsizei, const GLfloat*)) \
    X(void, UniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat*)) \
    X(void, GenFramebuffers, (GLsizei, GLuint*)) \
    X(void, DeleteFramebuffers, (GLsizei, const GLuint*)) \
    X(void, BindFramebuffer, (GLenum, GLuint)) \
    X(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) \
    X(GLenum, CheckFramebufferStatus, (GLenum)) \
    X(void, GenRenderbuffers, (GLsizei, GLuint*)) \
    X(void, DeleteRenderbuffers, (GLsizei, const GLuint*)) \
    X(void, BindRenderbuffer, (GLenum, GLuint)) \
    X(void, RenderbufferStorageMultisample, (GLenum, GLsizei, GLenum, GLsizei, GLsizei)) \
    X(void, FramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint)) \
    X(void, BlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum)) \
    X(void, Viewport, (GLint, GLint, GLsizei, GLsizei)) \
    X(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void, Clear, (GLbitfield)) \
    X(void, Enable, (GLenum)) \
    X(void, Disable, (GLenum)) \
    X(void, DepthMask, (GLboolean)) \
    X(void, CullFace, (GLenum)) \
    X(void, FrontFace, (GLenum)) \
    X(void, BlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum)) \
    X(void, PixelStorei, (GLenum, GLint)) \
    X(void, ReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)) \
    X(void, DrawElementsInstanced, (GLenum, GLsizei, GLenum, const void*, GLsizei)) \
    X(void, DrawArraysInstanced, (GLenum, GLint, GLsizei, GLsizei))
struct NativeGlFunctions {
#define DECLARE_GL(result, name, args) result (APIENTRY *name) args;
    GL_FUNCTIONS(DECLARE_GL)
#undef DECLARE_GL
};
struct NativeGlSlot {
    uint64_t id;
    NativeGlKind kind;
    GLuint name, color, depth, resolve, texture;
    unsigned width, height, samples;
    size_t bytes, length;
    void* data;
    NativeGlAttribute attributes[16];
    unsigned attribute_count;
    NativeGlResource indices;
    bool initialized;
};
#endif

struct NativeGlContext {
    NativeGlStats stats;
    char diagnostic[2048];
    bool lost;
#ifdef NATIVE_GL_ENABLED
    GLFWwindow* window;
    pthread_t thread;
    NativeGlFunctions gl;
    NativeGlSlot slots[GL_RESOURCE_LIMIT];
#endif
};

static void native_gl_error(NativeGlContext* context, const char* message) {
    if (context) str_copy(context->diagnostic, sizeof(context->diagnostic), message, strlen(message));
    log_error("scene3d graphics: %s", message);
}

#ifdef NATIVE_GL_ENABLED
struct NativeGlScope {
    GLFWwindow* previous;
    bool valid;
    NativeGlScope(NativeGlContext* context) : previous(nullptr), valid(false) {
        if (!context || context->lost || !pthread_equal(context->thread, pthread_self())) return;
        previous = glfwGetCurrentContext();
        glfwMakeContextCurrent(context->window);
        valid = glfwGetCurrentContext() == context->window;
    }
    ~NativeGlScope() { if (valid) glfwMakeContextCurrent(previous); }
};

static NativeGlSlot* native_gl_slot(NativeGlContext* context, NativeGlResource resource, NativeGlKind kind) {
    if (!context || context->lost || !resource.id) return nullptr;
    for (NativeGlSlot& slot : context->slots)
        if (slot.id == resource.id && slot.kind == kind) return &slot;
    return nullptr;
}

static NativeGlSlot* native_gl_allocate(NativeGlContext* context, NativeGlKind kind, size_t bytes) {
    if (!context || context->lost || bytes > GL_BYTE_LIMIT - context->stats.allocated_bytes) {
        native_gl_error(context, "resource byte quota exceeded"); return nullptr;
    }
    for (NativeGlSlot& slot : context->slots) if (!slot.id) {
        slot = {}; slot.id = native_gl_id(); slot.kind = kind; slot.bytes = bytes;
        context->stats.resources++; context->stats.allocated_bytes += bytes;
        return &slot;
    }
    native_gl_error(context, "resource count quota exceeded"); return nullptr;
}

static bool native_gl_check(NativeGlContext* context, const char* operation) {
    GLenum error = context->gl.GetError();
    if (error == GL_NO_ERROR) return true;
    char message[256]; snprintf(message, sizeof(message), "%s failed (OpenGL 0x%x)", operation, error);
    native_gl_error(context, message); return false;
}

static GLuint native_gl_compile(NativeGlContext* context, GLenum stage, const char* source) {
    if (!source || strlen(source) > 1024u * 1024u) { native_gl_error(context, "shader source quota exceeded"); return 0; }
    NativeGlFunctions& gl = context->gl;
    GLuint shader = gl.CreateShader(stage);
    gl.ShaderSource(shader, 1, &source, nullptr); gl.CompileShader(shader);
    GLint success = 0; gl.GetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success) return shader;
    char log[1800] = {}; gl.GetShaderInfoLog(shader, sizeof(log), nullptr, log);
    native_gl_error(context, log[0] ? log : "shader compilation failed");
    gl.DeleteShader(shader); return 0;
}
#endif

NativeGlContext* native_gl_create(bool enabled, char* diagnostic, size_t capacity) {
    const char* failure = "native graphics unavailable: enable the full Radiant GLFW host";
    NativeGlContext* context = nullptr;
#ifdef NATIVE_GL_ENABLED
    if (enabled && native_gl_context_count < 32) {
        context = (NativeGlContext*)mem_calloc(1, sizeof(NativeGlContext), MEM_CAT_RENDER);
        if (!context) return nullptr;
        context->thread = pthread_self(); context->stats.generation = native_gl_id();
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
#ifdef __APPLE__
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
#endif
        context->window = glfwCreateWindow(1, 1, "Radiant native graphics", nullptr, nullptr);
        glfwDefaultWindowHints();
        if (context->window) {
            native_gl_context_count++;
            NativeGlScope scope(context);
            bool loaded = scope.valid;
#define LOAD_GL(result, name, args) \
            context->gl.name = (result (APIENTRY *) args)glfwGetProcAddress("gl" #name); \
            loaded = loaded && context->gl.name;
            GL_FUNCTIONS(LOAD_GL)
#undef LOAD_GL
            if (loaded) {
                NativeGlFunctions& gl = context->gl;
                str_copy(context->stats.driver, sizeof(context->stats.driver), (const char*)gl.GetString(GL_RENDERER), strlen((const char*)gl.GetString(GL_RENDERER)));
                str_copy(context->stats.version, sizeof(context->stats.version), (const char*)gl.GetString(GL_VERSION), strlen((const char*)gl.GetString(GL_VERSION)));
                str_copy(context->stats.shading_language, sizeof(context->stats.shading_language), (const char*)gl.GetString(GL_SHADING_LANGUAGE_VERSION), strlen((const char*)gl.GetString(GL_SHADING_LANGUAGE_VERSION)));
                log_info("scene3d provider: %s; GL %s; GLSL %s", context->stats.driver, context->stats.version, context->stats.shading_language);
                return context;
            }
            failure = "native graphics entry points unavailable";
        } else failure = "native graphics core context creation failed";
    } else if (enabled) failure = "native graphics context quota exceeded";
    if (context) native_gl_destroy(context);
#endif
    if (diagnostic && capacity) str_copy(diagnostic, capacity, failure, strlen(failure));
    native_gl_error(nullptr, failure); return nullptr;
}

const char* native_gl_diagnostic(NativeGlContext* context) { return context ? context->diagnostic : "native graphics unavailable"; }
bool native_gl_stats(NativeGlContext* context, NativeGlStats* stats) {
    if (!context || !stats) return false;
    *stats = context->stats;stats->cpu_bytes=sizeof(*context);
#ifdef NATIVE_GL_ENABLED
    // driver-private allocations are not observable; separate owned upload shadows from GPU storage.
    for (const NativeGlSlot& slot:context->slots) if (slot.id) {
        size_t shadow=slot.kind==NATIVE_GL_BUFFER?slot.length:0;
        stats->cpu_bytes+=shadow;stats->gpu_bytes+=slot.bytes-shadow;
    }
#endif
    return true;
}
bool native_gl_valid(NativeGlContext* context, NativeGlResource resource, NativeGlKind kind) {
#ifdef NATIVE_GL_ENABLED
    return native_gl_slot(context, resource, kind) != nullptr;
#else
    return false;
#endif
}

void native_gl_release(NativeGlContext* context, NativeGlResource resource) {
#ifdef NATIVE_GL_ENABLED
    NativeGlScope scope(context); if (!scope.valid) return;
    for (NativeGlSlot& slot : context->slots) if (slot.id && slot.id == resource.id) {
        NativeGlFunctions& gl = context->gl;
        switch (slot.kind) {
            case NATIVE_GL_BUFFER: gl.DeleteBuffers(1, &slot.name); mem_free(slot.data); break;
            case NATIVE_GL_VERTEX_ARRAY: gl.DeleteVertexArrays(1, &slot.name); break;
            case NATIVE_GL_TEXTURE: gl.DeleteTextures(1, &slot.name); break;
            case NATIVE_GL_PROGRAM: gl.DeleteProgram(slot.name); break;
            case NATIVE_GL_TARGET:
                gl.DeleteFramebuffers(1, &slot.name); gl.DeleteFramebuffers(1, &slot.resolve);
                gl.DeleteRenderbuffers(1, &slot.color); gl.DeleteRenderbuffers(1, &slot.depth);
                gl.DeleteTextures(1, &slot.texture); break;
        }
        context->stats.resources--; context->stats.allocated_bytes -= slot.bytes; slot = {}; return;
    }
#endif
}
void native_gl_lose(NativeGlContext* context) {
    if (!context || context->lost) return;
#ifdef NATIVE_GL_ENABLED
    for (NativeGlSlot& slot : context->slots) if (slot.id) native_gl_release(context, {slot.id});
#endif
    context->lost = true; context->stats.generation = native_gl_id();
    native_gl_error(context, "native graphics generation lost");
}
void native_gl_destroy(NativeGlContext* context) {
    if (!context) return;
#ifdef NATIVE_GL_ENABLED
    // ordinary teardown retires resources without reporting a device failure.
    if (!context->lost) for (NativeGlSlot& slot : context->slots)
        if (slot.id) native_gl_release(context, {slot.id});
    if (context->window) { glfwDestroyWindow(context->window); native_gl_context_count--; }
#endif
    mem_free(context);
}

NativeGlResource native_gl_buffer(NativeGlContext* context, const void* data, size_t bytes) {
#ifdef NATIVE_GL_ENABLED
    NativeGlScope scope(context); if (!scope.valid || !data || !bytes) return {};
    if (bytes > GL_BYTE_LIMIT / 2) { native_gl_error(context, "buffer byte quota exceeded"); return {}; }
    // retain bounded upload bytes so indexed draws can validate the exact referenced range.
    NativeGlSlot* slot = native_gl_allocate(context, NATIVE_GL_BUFFER, bytes * 2); if (!slot) return {};
    slot->length = bytes; slot->data = mem_alloc(bytes, MEM_CAT_RENDER);
    if (!slot->data) { native_gl_error(context,"buffer upload copy allocation failed");native_gl_release(context, {slot->id}); return {}; }
    memcpy(slot->data, data, bytes);
    NativeGlFunctions& gl = context->gl;
    gl.GenBuffers(1, &slot->name); gl.BindBuffer(GL_ARRAY_BUFFER, slot->name);
    gl.BufferData(GL_ARRAY_BUFFER, bytes, data, GL_STATIC_DRAW);
    if (native_gl_check(context, "buffer upload")) return {slot->id};
    native_gl_release(context, {slot->id});
#endif
    return {};
}
NativeGlResource native_gl_vertices(NativeGlContext* context, const NativeGlAttribute* attributes,
    unsigned count, NativeGlResource indices) {
#ifdef NATIVE_GL_ENABLED
    NativeGlScope scope(context); if (!scope.valid || !attributes || !count || count > 16) return {};
    NativeGlSlot* index = indices.id ? native_gl_slot(context, indices, NATIVE_GL_BUFFER) : nullptr;
    if (indices.id && !index) return {};
    unsigned locations = 0;
    for (unsigned i = 0; i < count; i++) {
        const NativeGlAttribute& a = attributes[i];
        NativeGlSlot* buffer = native_gl_slot(context, a.buffer, NATIVE_GL_BUFFER);
        if (!buffer || a.location >= 16 || !a.components || a.components > 4 ||
            a.offset > buffer->length || a.components * sizeof(float) > buffer->length - a.offset ||
            a.stride > 2048 || (locations & (1u << a.location))) { native_gl_error(context, "invalid vertex attribute upload"); return {}; }
        locations |= 1u << a.location;
    }
    NativeGlSlot* slot = native_gl_allocate(context, NATIVE_GL_VERTEX_ARRAY, 0); if (!slot) return {};
    slot->attribute_count = count; slot->indices = indices;
    memcpy(slot->attributes, attributes, count * sizeof(NativeGlAttribute));
    NativeGlFunctions& gl = context->gl; gl.GenVertexArrays(1, &slot->name); gl.BindVertexArray(slot->name);
    for (unsigned i = 0; i < count; i++) {
        const NativeGlAttribute& a = attributes[i];
        gl.BindBuffer(GL_ARRAY_BUFFER, native_gl_slot(context, a.buffer, NATIVE_GL_BUFFER)->name);
        gl.EnableVertexAttribArray(a.location);
        gl.VertexAttribPointer(a.location, a.components, GL_FLOAT, GL_FALSE, a.stride, (const void*)(uintptr_t)a.offset);
        gl.VertexAttribDivisor(a.location, a.divisor);
    }
    if (index) gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, index->name);
    if (native_gl_check(context, "vertex array upload")) return {slot->id};
    native_gl_release(context, {slot->id});
#endif
    return {};
}

NativeGlResource native_gl_program(NativeGlContext* context, const char* vertex, const char* fragment) {
#ifdef NATIVE_GL_ENABLED
    NativeGlScope scope(context); if (!scope.valid) return {};
    NativeGlFunctions& gl = context->gl;
    GLuint vs = native_gl_compile(context, GL_VERTEX_SHADER, vertex); if (!vs) return {};
    GLuint fs = native_gl_compile(context, GL_FRAGMENT_SHADER, fragment);
    if (!fs) { gl.DeleteShader(vs); return {}; }
    GLuint program = gl.CreateProgram(); gl.AttachShader(program, vs); gl.AttachShader(program, fs);
    gl.LinkProgram(program); gl.DeleteShader(vs); gl.DeleteShader(fs);
    GLint success = 0; gl.GetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[1800] = {}; gl.GetProgramInfoLog(program, sizeof(log), nullptr, log);
        native_gl_error(context, log[0] ? log : "program link failed"); gl.DeleteProgram(program); return {};
    }
    NativeGlSlot* slot = native_gl_allocate(context, NATIVE_GL_PROGRAM, strlen(vertex) + strlen(fragment));
    if (!slot) { gl.DeleteProgram(program); return {}; }
    slot->name = program; return {slot->id};
#endif
    return {};
}
unsigned native_gl_uniform_count(NativeGlContext* context, NativeGlResource program) {
#ifdef NATIVE_GL_ENABLED
    NativeGlScope scope(context); NativeGlSlot* slot = native_gl_slot(context, program, NATIVE_GL_PROGRAM);
    if (scope.valid && slot) { GLint count = 0; context->gl.GetProgramiv(slot->name, GL_ACTIVE_UNIFORMS, &count); return count; }
#endif
    return 0;
}
bool native_gl_uniform_info(NativeGlContext* context, NativeGlResource program, unsigned index,
    char* name, size_t capacity, unsigned* type, int* size) {
#ifdef NATIVE_GL_ENABLED
    NativeGlScope scope(context); NativeGlSlot* slot = native_gl_slot(context, program, NATIVE_GL_PROGRAM);
    if (!scope.valid || !slot || !name || !capacity || capacity > 4096 || !type || !size || index >= native_gl_uniform_count(context, program)) return false;
    context->gl.GetActiveUniform(slot->name, index, capacity, nullptr, size, type, name); return true;
#else
    return false;
#endif
}
NativeGlUniform native_gl_uniform(NativeGlContext* context, NativeGlResource program, const char* name) {
#ifdef NATIVE_GL_ENABLED
    NativeGlScope scope(context); NativeGlSlot* slot = native_gl_slot(context, program, NATIVE_GL_PROGRAM);
    if (scope.valid && slot && name) return {program, context->gl.GetUniformLocation(slot->name, name), context->stats.generation};
#endif
    return {{}, -1, 0};
}
bool native_gl_uniform_set(NativeGlContext* context, NativeGlUniform uniform,
    const float* values, unsigned components, unsigned count) {
#ifdef NATIVE_GL_ENABLED
    NativeGlScope scope(context); NativeGlSlot* slot = native_gl_slot(context, uniform.program, NATIVE_GL_PROGRAM);
    if (!scope.valid || !slot || uniform.generation != context->stats.generation || uniform.location < 0 ||
        !values || !count || count > 1024 || (components != 1 && components != 3 && components != 4 && components != 16))
        { native_gl_error(context,"invalid uniform update or stale program");return false; }
    for (unsigned i = 0; i < components * count; i++) if (!isfinite(values[i]))
        { native_gl_error(context,"uniform value is not finite");return false; }
    NativeGlFunctions& gl = context->gl; gl.UseProgram(slot->name);
    switch (components) {
        case 1: gl.Uniform1fv(uniform.location, count, values); break;
        case 3: gl.Uniform3fv(uniform.location, count, values); break;
        case 4: gl.Uniform4fv(uniform.location, count, values); break;
        case 16: gl.UniformMatrix4fv(uniform.location, count, GL_FALSE, values); break;
    }
    return native_gl_check(context, "uniform update");
#else
    return false;
#endif
}

NativeGlResource native_gl_texture(NativeGlContext* context, const ImageSurface* image) {
#ifdef NATIVE_GL_ENABLED
    NativeGlScope scope(context);
    if (!scope.valid || !image || !image->pixels || image->alpha_mode!=IMAGE_ALPHA_STRAIGHT || image->width <= 0 || image->height <= 0 ||
        image->width > GL_DIMENSION_LIMIT || image->height > GL_DIMENSION_LIMIT || image->pitch < image->width * 4)
        { native_gl_error(context,"invalid texture dimensions, format or alpha mode");return {}; }
    size_t bytes = (size_t)image->width * image->height * 4;
    NativeGlSlot* slot = native_gl_allocate(context, NATIVE_GL_TEXTURE, bytes); if (!slot) return {};
    // decoded images are top-down; native UV (0,0) is the lower-left corner.
    uint8_t* pixels = (uint8_t*)mem_alloc(bytes, MEM_CAT_RENDER);
    if (!pixels) { native_gl_error(context,"texture staging allocation failed");native_gl_release(context, {slot->id}); return {}; }
    for (unsigned y = 0; y < (unsigned)image->height; y++)
        memcpy(pixels + (size_t)y * image->width * 4,
            (const uint8_t*)image->pixels + (size_t)(image->height - 1 - y) * image->pitch, (size_t)image->width * 4);
    NativeGlFunctions& gl = context->gl;
    gl.GenTextures(1, &slot->name); gl.BindTexture(GL_TEXTURE_2D, slot->name);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1); gl.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, image->width, image->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    mem_free(pixels);
    if (native_gl_check(context, "texture upload")) return {slot->id};
    native_gl_release(context, {slot->id});
#endif
    return {};
}

NativeGlResource native_gl_target(NativeGlContext* context, unsigned width, unsigned height, unsigned samples) {
#ifdef NATIVE_GL_ENABLED
    NativeGlScope scope(context);
    if (!scope.valid || !width || !height || width > GL_DIMENSION_LIMIT || height > GL_DIMENSION_LIMIT || samples > 4) {
        native_gl_error(context, "invalid drawing buffer dimensions or samples"); return {};
    }
    NativeGlFunctions& gl = context->gl; GLint max_samples = 0; gl.GetIntegerv(GL_MAX_SAMPLES, &max_samples);
    samples = samples > (unsigned)max_samples ? max_samples : samples; if (!samples) samples = 1;
    NativeGlSlot* slot = native_gl_allocate(context, NATIVE_GL_TARGET, (size_t)width * height * (8u * samples + 4u));
    if (!slot) return {};
    slot->width = width; slot->height = height; slot->samples = samples;
    gl.GenFramebuffers(1, &slot->name); gl.BindFramebuffer(GL_FRAMEBUFFER, slot->name);
    gl.GenRenderbuffers(1, &slot->color); gl.BindRenderbuffer(GL_RENDERBUFFER, slot->color);
    gl.RenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_SRGB8_ALPHA8, width, height);
    gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, slot->color);
    gl.GenRenderbuffers(1, &slot->depth); gl.BindRenderbuffer(GL_RENDERBUFFER, slot->depth);
    gl.RenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, width, height);
    gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, slot->depth);
    bool complete = gl.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    gl.GenFramebuffers(1, &slot->resolve); gl.BindFramebuffer(GL_FRAMEBUFFER, slot->resolve);
    gl.GenTextures(1, &slot->texture); gl.BindTexture(GL_TEXTURE_2D, slot->texture);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, slot->texture, 0);
    complete = complete && gl.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (complete && native_gl_check(context, "drawing buffer allocation")) return {slot->id};
    native_gl_error(context, "drawing buffer framebuffer incomplete"); native_gl_release(context, {slot->id});
#endif
    return {};
}
bool native_gl_begin(NativeGlContext* context, NativeGlResource target, const float background[4]) {
#ifdef NATIVE_GL_ENABLED
    NativeGlScope scope(context); NativeGlSlot* slot = native_gl_slot(context, target, NATIVE_GL_TARGET);
    if (!scope.valid || !slot || !background) return false;
    for (unsigned c = 0; c < 4; c++) if (!isfinite(background[c]) || background[c] < 0 || background[c] > 1) return false;
    NativeGlFunctions& gl = context->gl;
    gl.BindFramebuffer(GL_FRAMEBUFFER, slot->name); gl.Viewport(0, 0, slot->width, slot->height);
    gl.Enable(GL_FRAMEBUFFER_SRGB); gl.Enable(GL_DEPTH_TEST); gl.Disable(GL_SCISSOR_TEST); gl.DepthMask(GL_TRUE);
    gl.ClearColor(background[0] * background[3], background[1] * background[3], background[2] * background[3], background[3]);
    gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    slot->initialized=native_gl_check(context, "drawing buffer clear");return slot->initialized;
#else
    return false;
#endif
}
bool native_gl_draw(NativeGlContext* context, const NativeGlDraw* draw) {
#ifdef NATIVE_GL_ENABLED
    NativeGlScope scope(context); if (!scope.valid || !draw || !draw->count || !draw->instances || draw->count > 3000000 || draw->instances > 65536) return false;
    NativeGlSlot* program = native_gl_slot(context, draw->program, NATIVE_GL_PROGRAM);
    NativeGlSlot* vertices = native_gl_slot(context, draw->vertices, NATIVE_GL_VERTEX_ARRAY);
    NativeGlSlot* texture = draw->texture.id ? native_gl_slot(context, draw->texture, NATIVE_GL_TEXTURE) : nullptr;
    if (!program || !vertices || (draw->texture.id && !texture) || draw->side < 0 || draw->side > 2) return false;
    uint32_t maximum = draw->count - 1;
    if (draw->indexed) {
        NativeGlSlot* indices = native_gl_slot(context, vertices->indices, NATIVE_GL_BUFFER);
        if (!indices || draw->count > indices->length / sizeof(uint32_t)) {
            native_gl_error(context, "index draw range exceeds upload"); return false;
        }
        maximum = 0;
        for (unsigned i = 0; i < draw->count; i++) {
            uint32_t index; memcpy(&index, (const uint8_t*)indices->data + i * sizeof(index), sizeof(index));
            if (index > maximum) maximum = index;
        }
    }
    for (unsigned i = 0; i < vertices->attribute_count; i++) {
        const NativeGlAttribute& a = vertices->attributes[i];
        NativeGlSlot* buffer = native_gl_slot(context, a.buffer, NATIVE_GL_BUFFER);
        size_t element = a.components * sizeof(float), stride = a.stride ? a.stride : element;
        uint64_t last = a.divisor ? (draw->instances - 1) / a.divisor : maximum;
        if (!buffer || a.offset > buffer->length || element > buffer->length - a.offset ||
            last > (buffer->length - a.offset - element) / stride) {
            native_gl_error(context, "vertex or instance draw range exceeds upload"); return false;
        }
    }
    NativeGlFunctions& gl = context->gl; gl.UseProgram(program->name); gl.BindVertexArray(vertices->name);
    gl.ActiveTexture(GL_TEXTURE0); gl.BindTexture(GL_TEXTURE_2D, texture ? texture->name : 0);
    GLint sampler = gl.GetUniformLocation(program->name, "image"); if (sampler >= 0) gl.Uniform1i(sampler, 0);
    if (draw->transparent) { gl.Enable(GL_BLEND); gl.BlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA); }
    else gl.Disable(GL_BLEND);
    gl.DepthMask(draw->transparent ? GL_FALSE : GL_TRUE);
    if (draw->side == 2) gl.Disable(GL_CULL_FACE);
    else { gl.Enable(GL_CULL_FACE); gl.CullFace(draw->side == 1 ? GL_FRONT : GL_BACK); }
    gl.FrontFace(draw->clockwise ? GL_CW : GL_CCW);
    if (draw->indexed) gl.DrawElementsInstanced(GL_TRIANGLES, draw->count, GL_UNSIGNED_INT, nullptr, draw->instances);
    else gl.DrawArraysInstanced(GL_TRIANGLES, 0, draw->count, draw->instances);
    context->stats.draws++; return native_gl_check(context, "draw submission");
#else
    return false;
#endif
}

static float native_gl_srgb(float value, bool encode) {
    if (encode) return value <= .0031308f ? value * 12.92f : 1.055f * powf(value, 1.0f / 2.4f) - .055f;
    return value <= .04045f ? value / 12.92f : powf((value + .055f) / 1.055f, 2.4f);
}
ImageSurface* native_gl_snapshot(NativeGlContext* context, NativeGlResource target) {
#ifdef NATIVE_GL_ENABLED
    NativeGlScope scope(context); NativeGlSlot* slot = native_gl_slot(context, target, NATIVE_GL_TARGET);
    if (!scope.valid || !slot || !slot->initialized) return nullptr;
    NativeGlFunctions& gl = context->gl;
    GLint read, draw, pack, row, skip_rows, skip_pixels, pbo;
    gl.GetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read); gl.GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
    gl.GetIntegerv(GL_PACK_ALIGNMENT, &pack); gl.GetIntegerv(GL_PACK_ROW_LENGTH, &row);
    gl.GetIntegerv(GL_PACK_SKIP_ROWS, &skip_rows); gl.GetIntegerv(GL_PACK_SKIP_PIXELS, &skip_pixels);
    gl.GetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pbo);
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, slot->name); gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, slot->resolve);
    gl.BlitFramebuffer(0, 0, slot->width, slot->height, 0, 0, slot->width, slot->height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, slot->resolve); gl.BindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    gl.PixelStorei(GL_PACK_ALIGNMENT, 1); gl.PixelStorei(GL_PACK_ROW_LENGTH, 0);
    gl.PixelStorei(GL_PACK_SKIP_ROWS, 0); gl.PixelStorei(GL_PACK_SKIP_PIXELS, 0);
    ImageSurface* surface = image_surface_create(slot->width, slot->height);
    if (surface) gl.ReadPixels(0, 0, slot->width, slot->height, GL_RGBA, GL_UNSIGNED_BYTE, surface->pixels);
    bool success = native_gl_check(context, "drawing buffer readback");
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, read); gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, draw);
    gl.BindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
    gl.PixelStorei(GL_PACK_ALIGNMENT, pack); gl.PixelStorei(GL_PACK_ROW_LENGTH, row);
    gl.PixelStorei(GL_PACK_SKIP_ROWS, skip_rows); gl.PixelStorei(GL_PACK_SKIP_PIXELS, skip_pixels);
    if (!surface || !success) {
        if (!surface) native_gl_error(context,"snapshot readback allocation failed");
        image_surface_destroy(surface); return nullptr;
    }
    uint8_t* pixels = (uint8_t*)surface->pixels;
    for (unsigned y = 0; y < slot->height / 2; y++) {
        uint8_t* a = pixels + (size_t)y * surface->pitch;
        uint8_t* b = pixels + (size_t)(slot->height - 1 - y) * surface->pitch;
        for (unsigned x = 0; x < slot->width * 4; x++) { uint8_t value = a[x]; a[x] = b[x]; b[x] = value; }
    }
    // resolve contains encoded premultiplied linear light; ImageSurface wants premultiplied sRGB.
    for (size_t i = 0; i < (size_t)slot->width * slot->height; i++) {
        uint8_t* pixel = pixels + i * 4; float alpha = pixel[3] / 255.0f;
        for (unsigned c = 0; c < 3; c++) {
            float straight = alpha > 0 ? native_gl_srgb(pixel[c] / 255.0f, false) / alpha : 0;
            pixel[c] = (uint8_t)lroundf(fminf(1.0f, native_gl_srgb(fminf(1.0f, straight), true)) * alpha * 255.0f);
        }
    }
    surface->alpha_mode = IMAGE_ALPHA_PREMULTIPLIED; context->stats.frames++; return surface;
#else
    return nullptr;
#endif
}
