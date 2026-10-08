#include "gl_core.hpp"
#include "../lib/mem.h"
#include "../lib/log.h"
#include "../lib/str.h"
#include "../lib/time_util.h"
#include <math.h>
#include "../lambda/module/radiant/radiant_webgl_bridge.hpp"

// isolate core-profile declarations from the shell's legacy OpenGL headers.
#if !defined(LAMBDA_NO_GUI) && !defined(LAMBDA_HEADLESS)
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#ifdef __APPLE__
#include <OpenGL/gl3.h>
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
    X(void, DrawArraysInstanced, (GLenum, GLint, GLsizei, GLsizei)) \
    X(void, GetFloatv, (GLenum, GLfloat*)) \
    X(void, GetInteger64v, (GLenum, GLint64*)) \
    X(GLboolean, IsEnabled, (GLenum)) \
    X(void, BufferSubData, (GLenum, GLintptr, GLsizeiptr, const void*)) \
    X(void, GetBufferSubData, (GLenum, GLintptr, GLsizeiptr, void*)) \
    X(void, DisableVertexAttribArray, (GLuint)) \
    X(void, VertexAttribIPointer, (GLuint, GLint, GLenum, GLsizei, const void*)) \
    X(void, TexParameterf, (GLenum, GLenum, GLfloat)) \
    X(void, GetTexParameteriv, (GLenum, GLenum, GLint*)) \
    X(void, GetTexParameterfv, (GLenum, GLenum, GLfloat*)) \
    X(void, TexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*)) \
    X(void, TexImage3D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*)) \
    X(void, TexSubImage3D, (GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const void*)) \
    X(void, GenerateMipmap, (GLenum)) \
    X(void, DetachShader, (GLuint, GLuint)) \
    X(void, ValidateProgram, (GLuint)) \
    X(GLint, GetAttribLocation, (GLuint, const GLchar*)) \
    X(void, BindAttribLocation, (GLuint, GLuint, const GLchar*)) \
    X(void, GetActiveAttrib, (GLuint, GLuint, GLsizei, GLsizei*, GLint*, GLenum*, GLchar*)) \
    X(void, RenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei)) \
    X(void, ReadBuffer, (GLenum)) \
    X(void, DrawBuffers, (GLsizei, const GLenum*)) \
    X(void, Scissor, (GLint, GLint, GLsizei, GLsizei)) \
    X(void, ClearDepth, (GLdouble)) \
    X(void, ClearStencil, (GLint)) \
    X(void, ColorMask, (GLboolean, GLboolean, GLboolean, GLboolean)) \
    X(void, DepthFunc, (GLenum)) \
    X(void, LineWidth, (GLfloat)) \
    X(void, PolygonOffset, (GLfloat, GLfloat)) \
    X(void, BlendColor, (GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void, BlendEquation, (GLenum)) \
    X(void, BlendEquationSeparate, (GLenum, GLenum)) \
    X(void, BlendFunc, (GLenum, GLenum)) \
    X(void, StencilFunc, (GLenum, GLint, GLuint)) \
    X(void, StencilMask, (GLuint)) \
    X(void, StencilOp, (GLenum, GLenum, GLenum)) \
    X(void, StencilFuncSeparate, (GLenum, GLenum, GLint, GLuint)) \
    X(void, StencilMaskSeparate, (GLenum, GLuint)) \
    X(void, StencilOpSeparate, (GLenum, GLenum, GLenum, GLenum)) \
    X(void, Flush, ()) \
    X(void, Finish, ()) \
    X(void, GetBufferParameteriv, (GLenum, GLenum, GLint*)) \
    X(void, PrimitiveRestartIndex, (GLuint)) \
    X(GLboolean, IsBuffer, (GLuint)) \
    X(GLboolean, IsVertexArray, (GLuint)) \
    X(GLboolean, IsTexture, (GLuint)) \
    X(GLboolean, IsFramebuffer, (GLuint)) \
    X(GLboolean, IsRenderbuffer, (GLuint)) \
    X(GLboolean, IsShader, (GLuint)) \
    X(GLboolean, IsProgram, (GLuint)) \
    X(void, Uniform1f, (GLint, GLfloat)) \
    X(void, Uniform1iv, (GLint, GLsizei, const GLint*)) \
    X(void, Uniform1ui, (GLint, GLuint)) \
    X(void, Uniform1uiv, (GLint, GLsizei, const GLuint*)) \
    X(void, VertexAttrib1fv, (GLuint, const GLfloat*)) \
    X(void, Uniform2f, (GLint, GLfloat, GLfloat)) \
    X(void, Uniform2fv, (GLint, GLsizei, const GLfloat*)) \
    X(void, Uniform2i, (GLint, GLint, GLint)) \
    X(void, Uniform2iv, (GLint, GLsizei, const GLint*)) \
    X(void, Uniform2ui, (GLint, GLuint, GLuint)) \
    X(void, Uniform2uiv, (GLint, GLsizei, const GLuint*)) \
    X(void, VertexAttrib2fv, (GLuint, const GLfloat*)) \
    X(void, Uniform3f, (GLint, GLfloat, GLfloat, GLfloat)) \
    X(void, Uniform3i, (GLint, GLint, GLint, GLint)) \
    X(void, Uniform3iv, (GLint, GLsizei, const GLint*)) \
    X(void, Uniform3ui, (GLint, GLuint, GLuint, GLuint)) \
    X(void, Uniform3uiv, (GLint, GLsizei, const GLuint*)) \
    X(void, VertexAttrib3fv, (GLuint, const GLfloat*)) \
    X(void, Uniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void, Uniform4i, (GLint, GLint, GLint, GLint, GLint)) \
    X(void, Uniform4iv, (GLint, GLsizei, const GLint*)) \
    X(void, Uniform4ui, (GLint, GLuint, GLuint, GLuint, GLuint)) \
    X(void, Uniform4uiv, (GLint, GLsizei, const GLuint*)) \
    X(void, VertexAttrib4fv, (GLuint, const GLfloat*)) \
    X(void, UniformMatrix2fv, (GLint, GLsizei, GLboolean, const GLfloat*)) \
    X(void, UniformMatrix3fv, (GLint, GLsizei, GLboolean, const GLfloat*))
struct NativeGlFunctions {
#define DECLARE_GL(result, name, args) result (APIENTRY *name) args;
    GL_FUNCTIONS(DECLARE_GL)
#undef DECLARE_GL
};
struct WebGlTextureStorage { size_t levels[6][13]; };
struct NativeGlSlot {
    uint64_t id;
    NativeGlKind kind;
    GLuint name, color, depth, resolve, texture;
    unsigned width, height, samples, immutable_levels;
    size_t bytes, length;
    void* data;
    NativeGlAttribute attributes[16];
    unsigned attribute_count;
    NativeGlResource indices;
    bool initialized, webgl, immutable;
    uint64_t link_generation;
    unsigned attribute_types[16], enabled_attributes;
};
#endif

struct NativeGlContext {
    NativeGlStats stats;
    char diagnostic[2048];
    bool lost;
    uint32_t webgl_errors;
    NativeGlResource webgl_target, webgl_vao;
    WebGlOptions webgl_options;
    unsigned webgl_width, webgl_height;
    bool webgl_dirty, webgl_discard, webgl_flip, webgl_premultiply, webgl_loss_reported;
    bool extension_checked, floating_targets;
    unsigned webgl_colorspace;
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
        size_t shadow=slot.kind==NATIVE_GL_BUFFER?slot.length:slot.kind==NATIVE_GL_SHADER?slot.bytes:0;
        stats->cpu_bytes+=shadow+(slot.kind==NATIVE_GL_TEXTURE&&slot.data?sizeof(WebGlTextureStorage):0);stats->gpu_bytes+=slot.bytes-shadow;
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
            case NATIVE_GL_TEXTURE: gl.DeleteTextures(1, &slot.name); mem_free(slot.data); break;
            case NATIVE_GL_PROGRAM: gl.DeleteProgram(slot.name); break;
            case NATIVE_GL_SHADER: gl.DeleteShader(slot.name); mem_free(slot.data); break;
            case NATIVE_GL_FRAMEBUFFER: gl.DeleteFramebuffers(1, &slot.name); break;
            case NATIVE_GL_RENDERBUFFER: gl.DeleteRenderbuffers(1, &slot.name); break;
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
        !values || !count || count > 1024 || (components != 1 && components != 2 && components != 3 && components != 4 && components != 16))
        { native_gl_error(context,"invalid uniform update or stale program");return false; }
    for (unsigned i = 0; i < components * count; i++) if (!isfinite(values[i]))
        { native_gl_error(context,"uniform value is not finite");return false; }
    NativeGlFunctions& gl = context->gl; gl.UseProgram(slot->name);
    switch (components) {
        case 1: gl.Uniform1fv(uniform.location, count, values); break;
        case 2: gl.Uniform2fv(uniform.location, count, values); break;
        case 3: gl.Uniform3fv(uniform.location, count, values); break;
        case 4: gl.Uniform4fv(uniform.location, count, values); break;
        case 16: gl.UniformMatrix4fv(uniform.location, count, GL_FALSE, values); break;
    }
    return native_gl_check(context, "uniform update");
#else
    return false;
#endif
}

NativeGlResource native_gl_texture(NativeGlContext* context, const ImageSurface* image, const NativeGlSampler& sampler) {
#ifdef NATIVE_GL_ENABLED
    NativeGlScope scope(context);
    if (!scope.valid || !image || !image->pixels || image->alpha_mode!=IMAGE_ALPHA_STRAIGHT || image->width <= 0 || image->height <= 0 ||
        image->width > GL_DIMENSION_LIMIT || image->height > GL_DIMENSION_LIMIT || image->pitch < image->width * 4)
        { native_gl_error(context,"invalid texture dimensions, format or alpha mode");return {}; }
    auto valid_wrap=[](unsigned value) {return value==GL_REPEAT||value==GL_MIRRORED_REPEAT||value==GL_CLAMP_TO_EDGE;};
    bool mipmaps=sampler.min_filter>=GL_NEAREST_MIPMAP_NEAREST&&sampler.min_filter<=GL_LINEAR_MIPMAP_LINEAR;
    if(!valid_wrap(sampler.wrap_s)||!valid_wrap(sampler.wrap_t)||
        (!mipmaps&&sampler.min_filter!=GL_NEAREST&&sampler.min_filter!=GL_LINEAR)||
        (sampler.mag_filter!=GL_NEAREST&&sampler.mag_filter!=GL_LINEAR)) {native_gl_error(context,"invalid texture sampler");return {};}
    size_t bytes = (size_t)image->width * image->height * 4;
    size_t allocated=bytes;
    if(mipmaps) for(unsigned width=image->width,height=image->height;width>1||height>1;) {
        width=width>1?width/2:1;height=height>1?height/2:1;allocated+=(size_t)width*height*4;
    }
    NativeGlSlot* slot = native_gl_allocate(context, NATIVE_GL_TEXTURE, allocated); if (!slot) return {};
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
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, sampler.min_filter); gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, sampler.mag_filter);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, sampler.wrap_s); gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, sampler.wrap_t);
    if(mipmaps) gl.GenerateMipmap(GL_TEXTURE_2D);
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
#ifdef NATIVE_GL_ENABLED
// resolve ignores application scissor/read/draw selections, then restores each FBO's state.
static void native_gl_resolve(NativeGlContext* context,NativeGlSlot* slot) {
    NativeGlFunctions& gl=context->gl;GLint read,draw,read_buffer,draw_buffer;
    gl.GetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);gl.GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);
    bool scissor=gl.IsEnabled(GL_SCISSOR_TEST);gl.Disable(GL_SCISSOR_TEST);
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER,slot->name);gl.GetIntegerv(GL_READ_BUFFER,&read_buffer);
    gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER,slot->resolve);gl.GetIntegerv(GL_DRAW_BUFFER0,&draw_buffer);
    GLenum attachment=GL_COLOR_ATTACHMENT0;gl.ReadBuffer(attachment);gl.DrawBuffers(1,&attachment);
    gl.BlitFramebuffer(0,0,slot->width,slot->height,0,0,slot->width,slot->height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    gl.ReadBuffer(read_buffer);attachment=draw_buffer;gl.DrawBuffers(1,&attachment);
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER,read);gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER,draw);
    if(scissor) gl.Enable(GL_SCISSOR_TEST);
}
#endif
ImageSurface* native_gl_snapshot(NativeGlContext* context, NativeGlResource target) {
#ifdef NATIVE_GL_ENABLED
    NativeGlScope scope(context); NativeGlSlot* slot = native_gl_slot(context, target, NATIVE_GL_TARGET);
    if (!scope.valid || !slot || !slot->initialized) return nullptr;
    NativeGlFunctions& gl = context->gl;
    GLint read, draw, pack, row, skip_rows, skip_pixels, pbo, resolve_read;
    gl.GetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read); gl.GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
    gl.GetIntegerv(GL_PACK_ALIGNMENT, &pack); gl.GetIntegerv(GL_PACK_ROW_LENGTH, &row);
    gl.GetIntegerv(GL_PACK_SKIP_ROWS, &skip_rows); gl.GetIntegerv(GL_PACK_SKIP_PIXELS, &skip_pixels);
    gl.GetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pbo);
    native_gl_resolve(context,slot);
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER,slot->resolve);gl.GetIntegerv(GL_READ_BUFFER,&resolve_read);gl.ReadBuffer(GL_COLOR_ATTACHMENT0);
    gl.BindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    gl.PixelStorei(GL_PACK_ALIGNMENT, 1); gl.PixelStorei(GL_PACK_ROW_LENGTH, 0);
    gl.PixelStorei(GL_PACK_SKIP_ROWS, 0); gl.PixelStorei(GL_PACK_SKIP_PIXELS, 0);
    ImageSurface* surface = image_surface_create(slot->width, slot->height);
    if (surface) gl.ReadPixels(0, 0, slot->width, slot->height, GL_RGBA, GL_UNSIGNED_BYTE, surface->pixels);
    bool success = native_gl_check(context, "drawing buffer readback");
    gl.ReadBuffer(resolve_read);
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
    for (size_t i = 0; !slot->webgl && i < (size_t)slot->width * slot->height; i++) {
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

void native_gl_webgl_error(NativeGlContext* context, unsigned error) {
    if (!context) return;
    if (error >= 0x0500 && error <= 0x0506) context->webgl_errors |= 1u << (error - 0x0500);
}

#ifdef NATIVE_GL_ENABLED
static void webgl_collect_errors(NativeGlContext* context) {
    for (unsigned error; (error = context->gl.GetError()) != GL_NO_ERROR;)
        native_gl_webgl_error(context, error);
}
static NativeGlSlot* webgl_named_slot(NativeGlContext* context, GLuint name, NativeGlKind kind) {
    if (name) for (NativeGlSlot& slot : context->slots)
        if (slot.id && slot.kind == kind && slot.name == name) return &slot;
    return nullptr;
}
static NativeGlSlot* webgl_binding(NativeGlContext* context, GLenum pname, NativeGlKind kind) {
    GLint name = 0; context->gl.GetIntegerv(pname, &name);
    return webgl_named_slot(context, name, kind);
}
static GLenum webgl_buffer_binding(GLenum target) {
    switch (target) {
        case GL_ARRAY_BUFFER: return GL_ARRAY_BUFFER_BINDING;
        case GL_ELEMENT_ARRAY_BUFFER: return GL_ELEMENT_ARRAY_BUFFER_BINDING;
        case GL_PIXEL_PACK_BUFFER: return GL_PIXEL_PACK_BUFFER_BINDING;
        case GL_PIXEL_UNPACK_BUFFER: return GL_PIXEL_UNPACK_BUFFER_BINDING;
        case GL_COPY_READ_BUFFER: return GL_COPY_READ_BUFFER;
        case GL_COPY_WRITE_BUFFER: return GL_COPY_WRITE_BUFFER;
        case GL_UNIFORM_BUFFER: return GL_UNIFORM_BUFFER_BINDING;
        case GL_TRANSFORM_FEEDBACK_BUFFER: return GL_TRANSFORM_FEEDBACK_BUFFER_BINDING;
        default: return 0;
    }
}
static GLenum webgl_texture_binding(GLenum target) {
    if (target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z) target = GL_TEXTURE_CUBE_MAP;
    switch (target) {
        case GL_TEXTURE_2D: return GL_TEXTURE_BINDING_2D;
        case GL_TEXTURE_CUBE_MAP: return GL_TEXTURE_BINDING_CUBE_MAP;
        case GL_TEXTURE_3D: return GL_TEXTURE_BINDING_3D;
        case GL_TEXTURE_2D_ARRAY: return GL_TEXTURE_BINDING_2D_ARRAY;
        default: return 0;
    }
}
static bool webgl_resize_bytes(NativeGlContext* context, NativeGlSlot* slot, size_t bytes) {
    if (bytes > GL_BYTE_LIMIT - (context->stats.allocated_bytes - slot->bytes)) {
        native_gl_webgl_error(context, GL_OUT_OF_MEMORY); return false;
    }
    context->stats.allocated_bytes = context->stats.allocated_bytes - slot->bytes + bytes;
    slot->bytes = bytes; return true;
}
static unsigned webgl_scalar_bytes(unsigned type) {
    switch (type) {
        case GL_BYTE: case GL_UNSIGNED_BYTE: return 1;
        case GL_SHORT: case GL_UNSIGNED_SHORT: case GL_HALF_FLOAT: return 2;
        case GL_INT: case GL_UNSIGNED_INT: case GL_FLOAT: return 4;
        case GL_UNSIGNED_SHORT_5_6_5:case GL_UNSIGNED_SHORT_4_4_4_4:case GL_UNSIGNED_SHORT_5_5_5_1:return 2;
        case GL_UNSIGNED_INT_24_8:case GL_UNSIGNED_INT_2_10_10_10_REV:
        case GL_UNSIGNED_INT_10F_11F_11F_REV:case GL_UNSIGNED_INT_5_9_9_9_REV:return 4;
        case GL_FLOAT_32_UNSIGNED_INT_24_8_REV:return 8;
        default: return 0;
    }
}
struct WebGlStorageFormat {GLenum internal,format,type;bool floating;};
static const WebGlStorageFormat webgl_storage_formats[]={
    {GL_RGBA8,GL_RGBA,GL_UNSIGNED_BYTE,false},{GL_SRGB8_ALPHA8,GL_RGBA,GL_UNSIGNED_BYTE,false},
    {GL_RGB8,GL_RGB,GL_UNSIGNED_BYTE,false},{GL_SRGB8,GL_RGB,GL_UNSIGNED_BYTE,false},
    {GL_R8,GL_RED,GL_UNSIGNED_BYTE,false},{GL_RG8,GL_RG,GL_UNSIGNED_BYTE,false},
    {GL_RGBA16F,GL_RGBA,GL_FLOAT,true},{GL_RGBA32F,GL_RGBA,GL_FLOAT,true},
    {GL_R16F,GL_RED,GL_FLOAT,true},{GL_R32F,GL_RED,GL_FLOAT,true},
    {GL_RG16F,GL_RG,GL_FLOAT,true},{GL_RG32F,GL_RG,GL_FLOAT,true},
    {GL_R11F_G11F_B10F,GL_RGB,GL_FLOAT,true},
    {GL_DEPTH_COMPONENT16,GL_DEPTH_COMPONENT,GL_UNSIGNED_SHORT,false},
    {GL_DEPTH_COMPONENT24,GL_DEPTH_COMPONENT,GL_UNSIGNED_INT,false},
    {GL_DEPTH_COMPONENT32F,GL_DEPTH_COMPONENT,GL_FLOAT,false},
    {GL_DEPTH24_STENCIL8,GL_DEPTH_STENCIL,GL_UNSIGNED_INT_24_8,false},
    {GL_DEPTH32F_STENCIL8,GL_DEPTH_STENCIL,GL_FLOAT_32_UNSIGNED_INT_24_8_REV,false}
};
enum WebGlParameterKind { PARAM_INVALID, PARAM_INTEGER, PARAM_BOOLEAN, PARAM_FLOAT, PARAM_INT64 };
static WebGlParameterKind webgl_parameter_kind(unsigned pname) {
    switch(pname) {
#define WEBGL_PARAMETER(name,value,kind) case value:return PARAM_##kind;
#include "../lambda/module/radiant/webgl_parameters.def"
#undef WEBGL_PARAMETER
        default:return PARAM_INVALID;
    }
}
static bool webgl_capability(unsigned cap) {
    switch(cap) {
        case GL_BLEND: case GL_CULL_FACE: case GL_DEPTH_TEST: case GL_DITHER: case GL_POLYGON_OFFSET_FILL:
        case GL_SAMPLE_ALPHA_TO_COVERAGE: case GL_SAMPLE_COVERAGE: case GL_SCISSOR_TEST: case GL_STENCIL_TEST:
        case GL_RASTERIZER_DISCARD:return true;
        default:return false;
    }
}
static bool webgl_default_bound(NativeGlContext* context,GLenum binding) {
    GLint framebuffer;context->gl.GetIntegerv(binding,&framebuffer);
    NativeGlSlot* target=native_gl_slot(context,context->webgl_target,NATIVE_GL_TARGET);
    return target && framebuffer==(GLint)target->name;
}
static bool webgl_texture_parameter(unsigned pname) {
    switch(pname) {
        case GL_TEXTURE_MIN_FILTER:case GL_TEXTURE_MAG_FILTER:case GL_TEXTURE_WRAP_S:case GL_TEXTURE_WRAP_T:case GL_TEXTURE_WRAP_R:
        case GL_TEXTURE_BASE_LEVEL:case GL_TEXTURE_MAX_LEVEL:case GL_TEXTURE_MIN_LOD:case GL_TEXTURE_MAX_LOD:
        case GL_TEXTURE_COMPARE_MODE:case GL_TEXTURE_COMPARE_FUNC:return true;
        default:return false;
    }
}
struct WebGlPixelLayout { size_t bytes,stride,start,pixel; };
struct WebGlImageUnpackScope {
    NativeGlFunctions* gl;
    GLint values[6];
    const GLenum names[6]={GL_UNPACK_ALIGNMENT,GL_UNPACK_ROW_LENGTH,GL_UNPACK_IMAGE_HEIGHT,
        GL_UNPACK_SKIP_PIXELS,GL_UNPACK_SKIP_ROWS,GL_UNPACK_SKIP_IMAGES};
    WebGlImageUnpackScope(NativeGlFunctions& functions,bool compact):gl(compact?&functions:nullptr) {
        if(gl) for(unsigned i=0;i<6;i++) { gl->GetIntegerv(names[i],&values[i]);gl->PixelStorei(names[i],i?0:1); }
    }
    ~WebGlImageUnpackScope() { if(gl) for(unsigned i=0;i<6;i++) gl->PixelStorei(names[i],values[i]); }
};
static bool webgl_pixel_layout(NativeGlContext* context, unsigned width, unsigned height, unsigned depth,
    unsigned format, unsigned type, bool pack, WebGlPixelLayout* layout) {
    unsigned scalar = webgl_scalar_bytes(type), components;
    switch (format) {
        case GL_RED: case GL_RED_INTEGER: case GL_DEPTH_COMPONENT: case GL_DEPTH_STENCIL: components = 1; break;
        case GL_RG: case GL_RG_INTEGER: components = 2; break;
        case GL_RGB: case GL_RGB_INTEGER: components = 3; break;
        case GL_RGBA: case GL_RGBA_INTEGER: components = 4; break;
        default: native_gl_webgl_error(context, GL_INVALID_ENUM); return false;
    }
    if (!scalar) { native_gl_webgl_error(context, GL_INVALID_ENUM); return false; }
    GLint alignment = 4, row = 0, image = 0, skip_pixels = 0, skip_rows = 0, skip_images = 0;
    NativeGlFunctions& gl = context->gl;
    gl.GetIntegerv(pack ? GL_PACK_ALIGNMENT : GL_UNPACK_ALIGNMENT, &alignment);
    gl.GetIntegerv(pack ? GL_PACK_ROW_LENGTH : GL_UNPACK_ROW_LENGTH, &row);
    gl.GetIntegerv(pack ? GL_PACK_SKIP_PIXELS : GL_UNPACK_SKIP_PIXELS, &skip_pixels);
    gl.GetIntegerv(pack ? GL_PACK_SKIP_ROWS : GL_UNPACK_SKIP_ROWS, &skip_rows);
    if (!pack) { gl.GetIntegerv(GL_UNPACK_IMAGE_HEIGHT, &image); gl.GetIntegerv(GL_UNPACK_SKIP_IMAGES, &skip_images); }
    if ((uint64_t)skip_pixels+width>(row?(unsigned)row:width) ||
        (depth>1 && (uint64_t)skip_rows+height>(image?(unsigned)image:height))) {
        native_gl_webgl_error(context,GL_INVALID_OPERATION);return false;
    }
    bool packed=type==GL_UNSIGNED_SHORT_5_6_5||type==GL_UNSIGNED_SHORT_4_4_4_4||type==GL_UNSIGNED_SHORT_5_5_5_1||
        type==GL_UNSIGNED_INT_24_8||type==GL_UNSIGNED_INT_2_10_10_10_REV||type==GL_UNSIGNED_INT_10F_11F_11F_REV||
        type==GL_UNSIGNED_INT_5_9_9_9_REV||type==GL_FLOAT_32_UNSIGNED_INT_24_8_REV;
    layout->pixel=scalar*(packed?1:components);
    layout->stride=((size_t)(row?row:width)*layout->pixel+alignment-1)&~(size_t)(alignment-1);
    // bound every intermediate before multiplying: pixel-store skips can be INT_MAX.
    uint64_t rows=(uint64_t)skip_images*(image?image:height)+skip_rows;
    uint64_t span=depth?(uint64_t)(depth-1)*(image?image:height)+height:0;
    if (layout->stride && (rows+span)>GL_BYTE_LIMIT/layout->stride) {
        native_gl_webgl_error(context,GL_INVALID_OPERATION);return false;
    }
    layout->start=(size_t)rows*layout->stride+(size_t)skip_pixels*layout->pixel;
    layout->bytes=width&&height&&depth?layout->start+(span-1)*layout->stride+(size_t)width*layout->pixel:0;
    return true;
}
static bool webgl_pixel_bytes(NativeGlContext* context,unsigned width,unsigned height,unsigned depth,
    unsigned format,unsigned type,bool pack,size_t* bytes) {
    WebGlPixelLayout layout={};bool valid=webgl_pixel_layout(context,width,height,depth,format,type,pack,&layout);
    *bytes=layout.bytes;return valid;
}
static bool webgl_texture_allocation(NativeGlContext* context,NativeGlSlot* slot,unsigned target,unsigned level,size_t bytes) {
    if (!slot->data) slot->data=mem_calloc(1,sizeof(WebGlTextureStorage),MEM_CAT_RENDER);
    if (!slot->data) { native_gl_webgl_error(context,GL_OUT_OF_MEMORY);return false; }
    WebGlTextureStorage* storage=(WebGlTextureStorage*)slot->data;
    unsigned face=target>=GL_TEXTURE_CUBE_MAP_POSITIVE_X&&target<=GL_TEXTURE_CUBE_MAP_NEGATIVE_Z?target-GL_TEXTURE_CUBE_MAP_POSITIVE_X:0;
    size_t total=slot->bytes-storage->levels[face][level]+bytes;
    if (!webgl_resize_bytes(context,slot,total)) return false;
    storage->levels[face][level]=bytes;return true;
}
static bool webgl_draw_range(NativeGlContext* context, GLenum mode, int first, int count,
    GLenum type, size_t offset, int instances, bool indexed) {
    if (mode > GL_TRIANGLE_FAN) { native_gl_webgl_error(context, GL_INVALID_ENUM); return false; }
    if (first < 0 || count < 0 || instances < 0) { native_gl_webgl_error(context, GL_INVALID_VALUE); return false; }
    if (!webgl_binding(context, GL_CURRENT_PROGRAM, NATIVE_GL_PROGRAM)) {
        native_gl_webgl_error(context, GL_INVALID_OPERATION); return false;
    }
    NativeGlSlot* vao = webgl_binding(context, GL_VERTEX_ARRAY_BINDING, NATIVE_GL_VERTEX_ARRAY);
    if (!vao) { native_gl_webgl_error(context, GL_INVALID_OPERATION); return false; }
    uint64_t maximum = count ? (uint64_t)first + count - 1 : 0;
    if (indexed) {
        unsigned size = type == GL_UNSIGNED_BYTE ? 1 : type == GL_UNSIGNED_SHORT ? 2 : type == GL_UNSIGNED_INT ? 4 : 0;
        if (!size) { native_gl_webgl_error(context, GL_INVALID_ENUM); return false; }
        NativeGlSlot* indices = webgl_binding(context, GL_ELEMENT_ARRAY_BUFFER_BINDING, NATIVE_GL_BUFFER);
        if (!indices || offset % size || offset > indices->length || (size_t)count > (indices->length - offset) / size) {
            native_gl_webgl_error(context, GL_INVALID_OPERATION); return false;
        }
        maximum = 0;
        for (int i = 0; i < count; i++) {
            uint32_t index = 0; memcpy(&index, (const uint8_t*)indices->data + offset + (size_t)i * size, size);
            // primitive restart is always enabled by WebGL2; its sentinel consumes no vertex.
            if (index != (size == 1 ? 0xffu : size == 2 ? 0xffffu : 0xffffffffu) && index > maximum) maximum = index;
        }
    }
    if (!count || !instances) return true;
    for (unsigned i = 0; i < 16; i++) if (vao->enabled_attributes & (1u << i)) {
        const NativeGlAttribute& a = vao->attributes[i];
        NativeGlSlot* buffer = native_gl_slot(context, a.buffer, NATIVE_GL_BUFFER);
        size_t size = a.components * webgl_scalar_bytes(vao->attribute_types[i]), stride = a.stride ? a.stride : size;
        uint64_t last = a.divisor ? (instances - 1) / a.divisor : maximum;
        if (!buffer || !size || a.offset > buffer->length || size > buffer->length - a.offset ||
            last > (buffer->length - a.offset - size) / stride) {
            native_gl_webgl_error(context, GL_INVALID_OPERATION); return false;
        }
    }
    return true;
}
// a discarded default buffer is cleared lazily, without changing the application's state.
static void webgl_prepare_default(NativeGlContext* context) {
    if (!context->webgl_discard) return;
    NativeGlFunctions& gl = context->gl;
    NativeGlSlot* target = native_gl_slot(context, context->webgl_target, NATIVE_GL_TARGET);
    GLint framebuffer, scissor; GLfloat color[4], depth; GLint stencil, color_mask[4], depth_mask, stencil_mask, stencil_back_mask, draw_buffer;
    gl.GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer); scissor = gl.IsEnabled(GL_SCISSOR_TEST);
    gl.GetFloatv(GL_COLOR_CLEAR_VALUE,color); gl.GetFloatv(GL_DEPTH_CLEAR_VALUE,&depth); gl.GetIntegerv(GL_STENCIL_CLEAR_VALUE,&stencil);
    gl.GetIntegerv(GL_COLOR_WRITEMASK,color_mask); gl.GetIntegerv(GL_DEPTH_WRITEMASK,&depth_mask); gl.GetIntegerv(GL_STENCIL_WRITEMASK,&stencil_mask);gl.GetIntegerv(GL_STENCIL_BACK_WRITEMASK,&stencil_back_mask);
    gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER,target->name);gl.GetIntegerv(GL_DRAW_BUFFER0,&draw_buffer);
    GLenum attachment=GL_COLOR_ATTACHMENT0;gl.DrawBuffers(1,&attachment);gl.Disable(GL_SCISSOR_TEST);
    gl.ColorMask(1,1,1,1); gl.DepthMask(1); gl.StencilMask(~0u); gl.ClearColor(0,0,0,context->webgl_options.alpha?0:1);
    gl.ClearDepth(1); gl.ClearStencil(0); gl.Clear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
    gl.ClearColor(color[0],color[1],color[2],color[3]); gl.ClearDepth(depth); gl.ClearStencil(stencil);
    gl.ColorMask(color_mask[0],color_mask[1],color_mask[2],color_mask[3]); gl.DepthMask(depth_mask);gl.StencilMaskSeparate(GL_FRONT,stencil_mask);gl.StencilMaskSeparate(GL_BACK,stencil_back_mask);
    if (scissor) gl.Enable(GL_SCISSOR_TEST);
    GLenum previous_buffer=draw_buffer;gl.DrawBuffers(1,&previous_buffer);
    gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER,framebuffer); context->webgl_discard=false;
}
#endif

bool native_gl_webgl_init(NativeGlContext* context, unsigned width, unsigned height, const WebGlOptions* options) {
#ifdef NATIVE_GL_ENABLED
    NativeGlScope scope(context); if (!scope.valid || !options) return false;
    context->webgl_options = *options; context->webgl_colorspace = 0x9244;
    NativeGlSlot* vao = native_gl_allocate(context,NATIVE_GL_VERTEX_ARRAY,0); if (!vao) return false;
    context->gl.GenVertexArrays(1,&vao->name); context->webgl_vao={vao->id}; context->gl.BindVertexArray(vao->name);
    context->gl.Enable(GL_PRIMITIVE_RESTART); // fixed index restart is emulated per draw on GL 4.1.
    context->gl.Disable(GL_FRAMEBUFFER_SRGB);
    return native_gl_webgl_resize(context,width,height);
#else
    return false;
#endif
}
bool native_gl_webgl_resize(NativeGlContext* context, unsigned width, unsigned height) {
#ifdef NATIVE_GL_ENABLED
    NativeGlScope scope(context); if (!scope.valid) return false;
    NativeGlFunctions& gl=context->gl;
    GLint read,draw,renderbuffer,texture;
    gl.GetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);gl.GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);
    gl.GetIntegerv(GL_RENDERBUFFER_BINDING,&renderbuffer);gl.GetIntegerv(GL_TEXTURE_BINDING_2D,&texture);
    NativeGlSlot* old=native_gl_slot(context,context->webgl_target,NATIVE_GL_TARGET);
    GLuint old_name=old?old->name:0;
    NativeGlResource replacement=native_gl_target(context,width?width:1,height?height:1,context->webgl_options.antialias?4:1);
    NativeGlSlot* target=native_gl_slot(context,replacement,NATIVE_GL_TARGET);if (!target) return false;
    // WebGL fragment output is already encoded; its drawing buffer must not apply scene3d's sRGB conversion.
    gl.BindRenderbuffer(GL_RENDERBUFFER,target->color);
    gl.RenderbufferStorageMultisample(GL_RENDERBUFFER,target->samples,context->webgl_options.alpha?GL_RGBA8:GL_RGB8,target->width,target->height);
    gl.BindTexture(GL_TEXTURE_2D,target->texture);
    gl.TexImage2D(GL_TEXTURE_2D,0,context->webgl_options.alpha?GL_RGBA8:GL_RGB8,target->width,target->height,0,context->webgl_options.alpha?GL_RGBA:GL_RGB,GL_UNSIGNED_BYTE,nullptr);
    gl.BindFramebuffer(GL_FRAMEBUFFER,target->name);
    gl.FramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_RENDERBUFFER,0);
    if(context->webgl_options.depth || context->webgl_options.stencil) {
        gl.BindRenderbuffer(GL_RENDERBUFFER,target->depth);
        GLenum format=context->webgl_options.depth?(context->webgl_options.stencil?GL_DEPTH24_STENCIL8:GL_DEPTH_COMPONENT24):GL_STENCIL_INDEX8;
        GLenum attachment=context->webgl_options.depth?(context->webgl_options.stencil?GL_DEPTH_STENCIL_ATTACHMENT:GL_DEPTH_ATTACHMENT):GL_STENCIL_ATTACHMENT;
        gl.RenderbufferStorageMultisample(GL_RENDERBUFFER,target->samples,format,target->width,target->height);
        gl.FramebufferRenderbuffer(GL_FRAMEBUFFER,attachment,GL_RENDERBUFFER,target->depth);
    }
    context->webgl_options.antialias=target->samples>1;
    target->webgl=true;target->initialized=true;
    native_gl_release(context,context->webgl_target);context->webgl_target=replacement;
    context->webgl_width=width;context->webgl_height=height;context->webgl_discard=true;
    gl.BindRenderbuffer(GL_RENDERBUFFER,renderbuffer);gl.BindTexture(GL_TEXTURE_2D,texture);
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER,(!read||read==(GLint)old_name)?target->name:read);
    gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER,(!draw||draw==(GLint)old_name)?target->name:draw);
    if(!old_name) { gl.Viewport(0,0,width,height);gl.Scissor(0,0,width,height); }
    webgl_prepare_default(context);context->webgl_dirty=true;return native_gl_check(context,"WebGL drawing buffer resize");
#else
    return false;
#endif
}
ImageSurface* native_gl_webgl_snapshot(NativeGlContext* context) {
#ifdef NATIVE_GL_ENABLED
    if (!context || context->lost || !context->webgl_dirty) return nullptr;
    NativeGlScope scope(context); if (!scope.valid) return nullptr;
    webgl_collect_errors(context);
    ImageSurface* surface=native_gl_snapshot(context,context->webgl_target);
    if (surface) {
        uint8_t* pixels=(uint8_t*)surface->pixels;
        for (size_t i=0;i<(size_t)surface->width*surface->height;i++) {
            if (!context->webgl_options.alpha) pixels[i*4+3]=255;
            else if (!context->webgl_options.premultiplied_alpha)
                for (unsigned c=0;c<3;c++) pixels[i*4+c]=(unsigned)pixels[i*4+c]*pixels[i*4+3]/255u;
        }
        context->webgl_dirty=false;context->webgl_discard=!context->webgl_options.preserve;
    }
    return surface;
#else
    return nullptr;
#endif
}

bool native_gl_webgl_call(NativeGlContext* context, const WebGlCommand* command, WebGlReply* reply) {
    if (!context || !command || !reply) return false;
    *reply = {};
#ifdef NATIVE_GL_ENABLED
    NativeGlScope scope(context);
    if (command->op == WEBGL_isContextLost) { reply->kind=WEBGL_BOOLEAN;reply->n[0]=context->lost;return true; }
    if (context->lost && command->op==WEBGL_getError) {
        reply->kind=WEBGL_NUMBER;reply->n[0]=context->webgl_loss_reported?0:0x9242;
        context->webgl_loss_reported=true;return true;
    }
    if (!scope.valid) return false;
    NativeGlFunctions& gl=context->gl;const double* n=command->n;
    // DOM-source conversion preserves the application's client-array unpack state on every exit.
    WebGlImageUnpackScope image_unpack(gl,command->compact_pixels||command->op==WEBGL_texStorage2D||command->op==WEBGL_texStorage3D);
    webgl_collect_errors(context);
    auto fail=[&](unsigned error) { native_gl_webgl_error(context,error);return true; };
    auto resource=[&](unsigned index, NativeGlKind kind)->NativeGlSlot* {
        NativeGlSlot* slot=native_gl_slot(context,{command->resource[index]},kind);
        if (!slot) native_gl_webgl_error(context,GL_INVALID_OPERATION);
        return slot;
    };
    auto number=[&](double value, bool boolean=false) { reply->kind=boolean?WEBGL_BOOLEAN:WEBGL_NUMBER;reply->n[0]=value; };
    auto create=[&](NativeGlKind kind)->NativeGlSlot* {
        NativeGlSlot* slot=native_gl_allocate(context,kind,0);
        if (!slot) { native_gl_webgl_error(context,GL_OUT_OF_MEMORY);return nullptr; }
        reply->kind=WEBGL_RESOURCE;reply->resource=slot->id;reply->resource_kind=kind;
        reply->generation=context->stats.generation;return slot;
    };
    switch (command->op) {
        case WEBGL_getContextAttributes: {
            reply->kind=WEBGL_NUMBERS;reply->count=6;
            const WebGlOptions& options=context->webgl_options;
            reply->n[0]=options.alpha;reply->n[1]=options.depth;reply->n[2]=options.stencil;
            reply->n[3]=options.antialias;reply->n[4]=options.premultiplied_alpha;reply->n[5]=options.preserve;break;
        }
        case WEBGL_getError: {
            unsigned error=0;
            for (unsigned i=0;i<7;i++) if (context->webgl_errors&(1u<<i)) { error=0x0500+i;context->webgl_errors&=~(1u<<i);break; }
            number(error);return true;
        }
// resource families share creation/deletion/identity rules; only driver names differ.
#define WEBGL_RESOURCE_DELETE_IS(kind,stem) \
        case WEBGL_delete##stem:native_gl_release(context,{command->resource[0]});break; \
        case WEBGL_is##stem: { NativeGlSlot* slot=native_gl_slot(context,{command->resource[0]},kind);number(slot&&gl.Is##stem(slot->name),true);break; }
#define WEBGL_RESOURCE_FAMILY(kind,stem,plural) \
        case WEBGL_create##stem: { NativeGlSlot* slot=create(kind);if(slot) gl.Gen##plural(1,&slot->name);break; } \
        WEBGL_RESOURCE_DELETE_IS(kind,stem)
        WEBGL_RESOURCE_FAMILY(NATIVE_GL_BUFFER,Buffer,Buffers)
        WEBGL_RESOURCE_FAMILY(NATIVE_GL_VERTEX_ARRAY,VertexArray,VertexArrays)
        WEBGL_RESOURCE_FAMILY(NATIVE_GL_TEXTURE,Texture,Textures)
        WEBGL_RESOURCE_FAMILY(NATIVE_GL_FRAMEBUFFER,Framebuffer,Framebuffers)
        WEBGL_RESOURCE_FAMILY(NATIVE_GL_RENDERBUFFER,Renderbuffer,Renderbuffers)
        WEBGL_RESOURCE_DELETE_IS(NATIVE_GL_SHADER,Shader)
        WEBGL_RESOURCE_DELETE_IS(NATIVE_GL_PROGRAM,Program)
#undef WEBGL_RESOURCE_FAMILY
#undef WEBGL_RESOURCE_DELETE_IS
        case WEBGL_createShader: {
            if (n[0]!=GL_VERTEX_SHADER && n[0]!=GL_FRAGMENT_SHADER) return fail(GL_INVALID_ENUM);
            NativeGlSlot* slot=create(NATIVE_GL_SHADER);if(slot) { slot->name=gl.CreateShader(n[0]);slot->samples=n[0]; } break;
        }
        case WEBGL_createProgram: { NativeGlSlot* slot=create(NATIVE_GL_PROGRAM);if(slot) slot->name=gl.CreateProgram();break; }
        case WEBGL_bindBuffer: {
            if (!webgl_buffer_binding(n[0])) return fail(GL_INVALID_ENUM);
            NativeGlSlot* slot=command->resource[1]?resource(1,NATIVE_GL_BUFFER):nullptr;
            if (command->resource[1]&&!slot) break;
            gl.BindBuffer(n[0],slot?slot->name:0);break;
        }
        case WEBGL_bindVertexArray: {
            NativeGlSlot* slot=native_gl_slot(context,{command->resource[0]?command->resource[0]:context->webgl_vao.id},NATIVE_GL_VERTEX_ARRAY);
            if(slot) gl.BindVertexArray(slot->name);else return fail(GL_INVALID_OPERATION);break;
        }
        case WEBGL_bindTexture: {
            if (!webgl_texture_binding(n[0]) || (n[0]>=GL_TEXTURE_CUBE_MAP_POSITIVE_X&&n[0]<=GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)) return fail(GL_INVALID_ENUM);
            NativeGlSlot* slot=command->resource[1]?resource(1,NATIVE_GL_TEXTURE):nullptr;
            if (command->resource[1]&&!slot) break;
            if (slot && slot->samples && slot->samples!=n[0]) return fail(GL_INVALID_OPERATION);
            if (slot) slot->samples=n[0];gl.BindTexture(n[0],slot?slot->name:0);break;
        }
        case WEBGL_bindFramebuffer: {
            if (n[0]!=GL_FRAMEBUFFER && n[0]!=GL_READ_FRAMEBUFFER && n[0]!=GL_DRAW_FRAMEBUFFER) return fail(GL_INVALID_ENUM);
            NativeGlSlot* slot=command->resource[1]?resource(1,NATIVE_GL_FRAMEBUFFER):native_gl_slot(context,context->webgl_target,NATIVE_GL_TARGET);
            if(slot) gl.BindFramebuffer(n[0],slot->name);break;
        }
        case WEBGL_bindRenderbuffer: {
            if(n[0]!=GL_RENDERBUFFER) return fail(GL_INVALID_ENUM);
            NativeGlSlot* slot=command->resource[1]?resource(1,NATIVE_GL_RENDERBUFFER):nullptr;
            if(command->resource[1]&&!slot) break;gl.BindRenderbuffer(n[0],slot?slot->name:0);break;
        }
        case WEBGL_useProgram: {
            NativeGlSlot* slot=command->resource[0]?resource(0,NATIVE_GL_PROGRAM):nullptr;
            if(command->resource[0]&&!slot) break;gl.UseProgram(slot?slot->name:0);break;
        }
        case WEBGL_bufferData: case WEBGL_bufferSubData: {
            GLenum binding=webgl_buffer_binding(n[0]);if(!binding) return fail(GL_INVALID_ENUM);
            NativeGlSlot* slot=webgl_binding(context,binding,NATIVE_GL_BUFFER);if(!slot) return fail(GL_INVALID_OPERATION);
            if (command->op==WEBGL_bufferData) {
                if(n[2]!=GL_STATIC_DRAW&&n[2]!=GL_DYNAMIC_DRAW&&n[2]!=GL_STREAM_DRAW&&n[2]!=GL_STATIC_READ&&n[2]!=GL_DYNAMIC_READ&&n[2]!=GL_STREAM_READ&&n[2]!=GL_STATIC_COPY&&n[2]!=GL_DYNAMIC_COPY&&n[2]!=GL_STREAM_COPY) return fail(GL_INVALID_ENUM);
                size_t bytes=command->bytes;
                if(bytes>GL_BYTE_LIMIT/2) return fail(GL_OUT_OF_MEMORY);
                void* copy=bytes?mem_calloc(1,bytes,MEM_CAT_RENDER):nullptr;
                if(bytes&&!copy) return fail(GL_OUT_OF_MEMORY);
                if(command->data&&bytes) memcpy(copy,command->data,bytes);
                if(!webgl_resize_bytes(context,slot,bytes*2)) { mem_free(copy);break; }
                mem_free(slot->data);slot->data=copy;slot->length=bytes;
                gl.BufferData(n[0],bytes,copy,n[2]);
            } else {
                size_t offset=n[1];
                if(n[1]<0||offset>slot->length||command->bytes>slot->length-offset) return fail(GL_INVALID_VALUE);
                if(command->bytes) memcpy((uint8_t*)slot->data+offset,command->data,command->bytes);
                gl.BufferSubData(n[0],offset,command->bytes,command->data);
            } break;
        }
        case WEBGL_getBufferSubData: {
            GLenum binding=webgl_buffer_binding(n[0]);if(!binding) return fail(GL_INVALID_ENUM);
            NativeGlSlot* slot=webgl_binding(context,binding,NATIVE_GL_BUFFER);if(!slot) return fail(GL_INVALID_OPERATION);
            if(n[1]<0||(size_t)n[1]>slot->length||command->bytes>slot->length-(size_t)n[1]) return fail(GL_INVALID_VALUE);
            if(command->bytes) gl.GetBufferSubData(n[0],n[1],command->bytes,(void*)command->data);break;
        }
        case WEBGL_getBufferParameter: {
            GLenum binding=webgl_buffer_binding(n[0]);if(!binding) return fail(GL_INVALID_ENUM);
            NativeGlSlot* slot=webgl_binding(context,binding,NATIVE_GL_BUFFER);if(!slot) return fail(GL_INVALID_OPERATION);
            if(n[1]==GL_BUFFER_SIZE) number(slot->length);
            else if(n[1]==GL_BUFFER_USAGE) { GLint usage;gl.GetBufferParameteriv(n[0],n[1],&usage);number(usage); }
            else return fail(GL_INVALID_ENUM);break;
        }
        case WEBGL_shaderSource: {
            NativeGlSlot* slot=resource(0,NATIVE_GL_SHADER);if(!slot) break;
            if(!command->text||strlen(command->text)>1024u*1024u) return fail(GL_INVALID_VALUE);
            if(!webgl_resize_bytes(context,slot,strlen(command->text)+1)) break;
            char* original=mem_strdup(command->text,MEM_CAT_RENDER);
            uint64_t started=time_now_us();char* adapted=native_gl_shader_source(command->text,slot->samples==GL_FRAGMENT_SHADER);
            context->stats.shader_normalize_calls++;context->stats.shader_normalize_us+=time_now_us()-started;
            if(!original||!adapted) { mem_free(original);mem_free(adapted);return fail(GL_OUT_OF_MEMORY); }
            mem_free(slot->data);slot->data=original;gl.ShaderSource(slot->name,1,(const char* const*)&adapted,nullptr);mem_free(adapted);break;
        }
        case WEBGL_compileShader: { NativeGlSlot* slot=resource(0,NATIVE_GL_SHADER);if(slot) gl.CompileShader(slot->name);break; }
        case WEBGL_attachShader: case WEBGL_detachShader: {
            NativeGlSlot* program=resource(0,NATIVE_GL_PROGRAM);NativeGlSlot* shader=resource(1,NATIVE_GL_SHADER);
            if(program&&shader) { if(command->op==WEBGL_attachShader) gl.AttachShader(program->name,shader->name);else gl.DetachShader(program->name,shader->name); }break;
        }
        case WEBGL_linkProgram: case WEBGL_validateProgram: {
            NativeGlSlot* program=resource(0,NATIVE_GL_PROGRAM);if(!program) break;
            if(command->op==WEBGL_linkProgram) { gl.LinkProgram(program->name);program->link_generation=native_gl_id(); }
            else gl.ValidateProgram(program->name);break;
        }
        case WEBGL_getShaderSource: { NativeGlSlot* shader=resource(0,NATIVE_GL_SHADER);if(shader) { reply->kind=WEBGL_STRING;reply->source=shader->data?(const char*)shader->data:""; }break; }
        case WEBGL_getShaderInfoLog: case WEBGL_getProgramInfoLog: {
            NativeGlSlot* slot=resource(0,command->op==WEBGL_getShaderInfoLog?NATIVE_GL_SHADER:NATIVE_GL_PROGRAM);if(!slot) break;
            reply->kind=WEBGL_STRING;
            if(command->op==WEBGL_getShaderInfoLog) gl.GetShaderInfoLog(slot->name,sizeof(reply->text),nullptr,reply->text);
            else gl.GetProgramInfoLog(slot->name,sizeof(reply->text),nullptr,reply->text);break;
        }
        case WEBGL_getShaderParameter: case WEBGL_getProgramParameter: {
            bool shader=command->op==WEBGL_getShaderParameter;
            NativeGlSlot* slot=resource(0,shader?NATIVE_GL_SHADER:NATIVE_GL_PROGRAM);if(!slot) break;
            bool boolean=n[1]==GL_COMPILE_STATUS||n[1]==GL_LINK_STATUS||n[1]==GL_VALIDATE_STATUS||n[1]==GL_DELETE_STATUS;
            if(shader?(n[1]!=GL_SHADER_TYPE&&n[1]!=GL_COMPILE_STATUS&&n[1]!=GL_DELETE_STATUS):
                (n[1]!=GL_LINK_STATUS&&n[1]!=GL_VALIDATE_STATUS&&n[1]!=GL_DELETE_STATUS&&n[1]!=GL_ATTACHED_SHADERS&&n[1]!=GL_ACTIVE_UNIFORMS&&n[1]!=GL_ACTIVE_ATTRIBUTES&&n[1]!=GL_ACTIVE_UNIFORM_BLOCKS)) return fail(GL_INVALID_ENUM);
            GLint value=0;if(shader) gl.GetShaderiv(slot->name,n[1],&value);else gl.GetProgramiv(slot->name,n[1],&value);number(value,boolean);break;
        }
        case WEBGL_getActiveUniform: case WEBGL_getActiveAttrib: {
            NativeGlSlot* program=resource(0,NATIVE_GL_PROGRAM);if(!program) break;
            GLint count=0;bool uniform=command->op==WEBGL_getActiveUniform;
            gl.GetProgramiv(program->name,uniform?GL_ACTIVE_UNIFORMS:GL_ACTIVE_ATTRIBUTES,&count);
            if(n[1]>=count) return fail(GL_INVALID_VALUE);
            GLint size=0;GLenum type=0;
            if(uniform) gl.GetActiveUniform(program->name,n[1],sizeof(reply->text),nullptr,&size,&type,reply->text);
            else gl.GetActiveAttrib(program->name,n[1],sizeof(reply->text),nullptr,&size,&type,reply->text);
            reply->kind=WEBGL_ACTIVE_INFO;reply->n[0]=size;reply->n[1]=type;break;
        }
        case WEBGL_getUniformLocation: case WEBGL_getAttribLocation: case WEBGL_bindAttribLocation: {
            NativeGlSlot* program=resource(0,NATIVE_GL_PROGRAM);if(!program||!command->text) break;
            if(command->op==WEBGL_bindAttribLocation) gl.BindAttribLocation(program->name,n[1],command->text);
            else if(command->op==WEBGL_getAttribLocation) number(gl.GetAttribLocation(program->name,command->text));
            else {
                GLint linked;gl.GetProgramiv(program->name,GL_LINK_STATUS,&linked);if(!linked) return fail(GL_INVALID_OPERATION);
                GLint location=gl.GetUniformLocation(program->name,command->text);
                if(location>=0) { reply->kind=WEBGL_LOCATION;reply->n[0]=location;reply->program=program->id;reply->generation=program->link_generation; }
            }break;
        }
        case WEBGL_getShaderPrecisionFormat: {
            if(n[0]!=GL_VERTEX_SHADER&&n[0]!=GL_FRAGMENT_SHADER) return fail(GL_INVALID_ENUM);
            if(n[1]<0x8DF0||n[1]>0x8DF5) return fail(GL_INVALID_ENUM);
            // desktop floats/ints have at least these IEEE-754 / signed-32-bit bounds.
            reply->kind=WEBGL_PRECISION;reply->n[0]=n[1]<=0x8DF2?127:31;reply->n[1]=n[1]<=0x8DF2?127:30;reply->n[2]=n[1]<=0x8DF2?23:0;break;
        }
        case WEBGL_activeTexture: gl.ActiveTexture(n[0]);break;
        case WEBGL_texParameteri:case WEBGL_texParameterf: {
            GLenum binding=webgl_texture_binding(n[0]);if(!binding||!webgl_texture_parameter(n[1])) return fail(GL_INVALID_ENUM);
            if(!webgl_binding(context,binding,NATIVE_GL_TEXTURE)) return fail(GL_INVALID_OPERATION);
            if(command->op==WEBGL_texParameteri) gl.TexParameteri(n[0],n[1],n[2]);else gl.TexParameterf(n[0],n[1],n[2]);break;
        }
        case WEBGL_generateMipmap: gl.GenerateMipmap(n[0]);break;
        case WEBGL_viewport: gl.Viewport(n[0],n[1],n[2],n[3]);break;
        case WEBGL_scissor: gl.Scissor(n[0],n[1],n[2],n[3]);break;
        case WEBGL_clearColor: gl.ClearColor(n[0],n[1],n[2],n[3]);break;
        case WEBGL_clearDepth: gl.ClearDepth(n[0]);break;
        case WEBGL_clearStencil: gl.ClearStencil(n[0]);break;
        case WEBGL_colorMask: gl.ColorMask(n[0],n[1],n[2],n[3]);break;
        case WEBGL_depthMask: gl.DepthMask(n[0]);break;
        case WEBGL_depthFunc: gl.DepthFunc(n[0]);break;
        case WEBGL_enable: if(!webgl_capability(n[0])) return fail(GL_INVALID_ENUM);gl.Enable(n[0]);break;
        case WEBGL_disable: if(!webgl_capability(n[0])) return fail(GL_INVALID_ENUM);gl.Disable(n[0]);break;
        case WEBGL_cullFace: gl.CullFace(n[0]);break;
        case WEBGL_frontFace: gl.FrontFace(n[0]);break;
        case WEBGL_lineWidth: gl.LineWidth(n[0]);break;
        case WEBGL_polygonOffset: gl.PolygonOffset(n[0],n[1]);break;
        case WEBGL_blendColor: gl.BlendColor(n[0],n[1],n[2],n[3]);break;
        case WEBGL_blendEquation: gl.BlendEquation(n[0]);break;
        case WEBGL_blendEquationSeparate: gl.BlendEquationSeparate(n[0],n[1]);break;
        case WEBGL_blendFunc: gl.BlendFunc(n[0],n[1]);break;
        case WEBGL_blendFuncSeparate: gl.BlendFuncSeparate(n[0],n[1],n[2],n[3]);break;
        case WEBGL_stencilFunc: gl.StencilFunc(n[0],n[1],n[2]);break;
        case WEBGL_stencilMask: gl.StencilMask(n[0]);break;
        case WEBGL_stencilOp: gl.StencilOp(n[0],n[1],n[2]);break;
        case WEBGL_stencilFuncSeparate: gl.StencilFuncSeparate(n[0],n[1],n[2],n[3]);break;
        case WEBGL_stencilMaskSeparate: gl.StencilMaskSeparate(n[0],n[1]);break;
        case WEBGL_stencilOpSeparate: gl.StencilOpSeparate(n[0],n[1],n[2],n[3]);break;
        case WEBGL_renderbufferStorage: case WEBGL_renderbufferStorageMultisample: {
            bool multi=command->op==WEBGL_renderbufferStorageMultisample;unsigned wi=multi?3:2;
            if(n[0]!=GL_RENDERBUFFER) return fail(GL_INVALID_ENUM);
            if(n[wi]<0||n[wi+1]<0||n[wi]>4096||n[wi+1]>4096||n[1]<0) return fail(GL_INVALID_VALUE);
            NativeGlSlot* slot=webgl_binding(context,GL_RENDERBUFFER_BINDING,NATIVE_GL_RENDERBUFFER);if(!slot) return fail(GL_INVALID_OPERATION);
            GLint maximum;gl.GetIntegerv(GL_MAX_SAMPLES,&maximum);if(multi&&n[1]>maximum) return fail(GL_INVALID_VALUE);
            size_t samples=multi&&n[1]>0?n[1]:1;
            if(!webgl_resize_bytes(context,slot,(size_t)n[wi]*(size_t)n[wi+1]*16*samples)) break;
            if(multi) gl.RenderbufferStorageMultisample(n[0],n[1],n[2],n[3],n[4]);
            else gl.RenderbufferStorage(n[0],n[1],n[2],n[3]);break;
        }
        case WEBGL_blitFramebuffer: webgl_prepare_default(context);gl.BlitFramebuffer(n[0],n[1],n[2],n[3],n[4],n[5],n[6],n[7],n[8],n[9]);context->webgl_dirty|=webgl_default_bound(context,GL_DRAW_FRAMEBUFFER_BINDING);break;
        case WEBGL_flush: gl.Flush();break;
        case WEBGL_finish: gl.Finish();break;
        case WEBGL_isEnabled: if(!webgl_capability(n[0])) { number(0,true);return fail(GL_INVALID_ENUM); }number(gl.IsEnabled(n[0]),true);break;
        case WEBGL_clear: webgl_prepare_default(context);gl.Clear(n[0]);context->webgl_dirty|=webgl_default_bound(context,GL_DRAW_FRAMEBUFFER_BINDING);break;
        case WEBGL_getTexParameter: {
            GLenum binding=webgl_texture_binding(n[0]);if(!binding) return fail(GL_INVALID_ENUM);
            if(!webgl_binding(context,binding,NATIVE_GL_TEXTURE)) return fail(GL_INVALID_OPERATION);
            if(n[1]==GL_TEXTURE_MIN_LOD||n[1]==GL_TEXTURE_MAX_LOD) { GLfloat value=0;gl.GetTexParameterfv(n[0],n[1],&value);number(value); }
            else if(n[1]==0x912F||n[1]==0x82DF) {
                NativeGlSlot* texture=webgl_binding(context,webgl_texture_binding(n[0]),NATIVE_GL_TEXTURE);
                if(!texture) return fail(GL_INVALID_OPERATION);
                if(n[1]==0x912F) number(texture->immutable,true);else number(texture->immutable?texture->immutable_levels:0);
            } else { if(!webgl_texture_parameter(n[1])) return fail(GL_INVALID_ENUM);GLint value=0;gl.GetTexParameteriv(n[0],n[1],&value);number(value); }break;
        }
        case WEBGL_pixelStorei: {
            if(n[0]==0x9240) context->webgl_flip=n[1]!=0;
            else if(n[0]==0x9241) context->webgl_premultiply=n[1]!=0;
            else if(n[0]==0x9243) { if(n[1]!=0&&n[1]!=0x9244) return fail(GL_INVALID_VALUE);context->webgl_colorspace=n[1]; }
            else {
                switch((unsigned)n[0]) {
                    case GL_PACK_ALIGNMENT:case GL_UNPACK_ALIGNMENT:case GL_PACK_ROW_LENGTH:case GL_PACK_SKIP_PIXELS:case GL_PACK_SKIP_ROWS:
                    case GL_UNPACK_ROW_LENGTH:case GL_UNPACK_IMAGE_HEIGHT:case GL_UNPACK_SKIP_PIXELS:case GL_UNPACK_SKIP_ROWS:case GL_UNPACK_SKIP_IMAGES:gl.PixelStorei(n[0],n[1]);break;
                    default:return fail(GL_INVALID_ENUM);
                }
            }break;
        }
        case WEBGL_texImage2D: case WEBGL_texSubImage2D: case WEBGL_texImage3D: case WEBGL_texSubImage3D: {
            bool sub=command->op==WEBGL_texSubImage2D||command->op==WEBGL_texSubImage3D;
            bool three=command->op==WEBGL_texImage3D||command->op==WEBGL_texSubImage3D;
            unsigned wi=sub?(three?5:4):3,hi=wi+1,fi=three?(sub?8:7):6;
            unsigned width=n[wi],height=n[hi],depth=three?n[hi+1]:1;
            if(n[wi]<0||n[hi]<0||width>4096||height>4096||depth>4096||n[1]<0||n[1]>12) return fail(GL_INVALID_VALUE);
            GLenum binding=webgl_texture_binding(n[0]);if(!binding) return fail(GL_INVALID_ENUM);
            NativeGlSlot* slot=webgl_binding(context,binding,NATIVE_GL_TEXTURE);if(!slot) return fail(GL_INVALID_OPERATION);
            if(!sub&&slot->immutable) return fail(GL_INVALID_OPERATION);
            // this overload borrows client memory, never a PBO offset.
            if(webgl_binding(context,GL_PIXEL_UNPACK_BUFFER_BINDING,NATIVE_GL_BUFFER)) return fail(GL_INVALID_OPERATION);
            if(three&&command->data&&(context->webgl_flip||context->webgl_premultiply)) return fail(GL_INVALID_OPERATION);
            WebGlPixelLayout layout={};if(!webgl_pixel_layout(context,width,height,depth,n[fi],n[fi+1],false,&layout)) break;
            size_t bytes=layout.bytes;
            if(command->data&&bytes>command->bytes) return fail(GL_INVALID_OPERATION);
            if(sub&&!command->data&&bytes) return fail(GL_INVALID_VALUE);
            size_t allocation=(size_t)width*height*depth*16;
            if(!sub&&!webgl_texture_allocation(context,slot,n[0],n[1],allocation)) break;
            // zero allocation removes uninitialized GPU memory from WebGL's observable state.
            void* zero=!command->data&&bytes?mem_calloc(1,bytes,MEM_CAT_RENDER):nullptr;
            if(!command->data&&bytes&&!zero) return fail(GL_OUT_OF_MEMORY);
            void* transformed=nullptr;
            if(command->data&&!three&&(context->webgl_flip||context->webgl_premultiply)) {
                if(context->webgl_premultiply&&!(n[fi]==GL_RGBA&&(n[fi+1]==GL_UNSIGNED_BYTE||n[fi+1]==GL_FLOAT))) { mem_free(zero);return fail(GL_INVALID_OPERATION); }
                transformed=mem_alloc(bytes,MEM_CAT_RENDER);if(bytes&&!transformed) { mem_free(zero);return fail(GL_OUT_OF_MEMORY); }
                if(bytes) memcpy(transformed,command->data,bytes);
                for(unsigned y=0;y<height;y++) {
                    uint8_t* dst=(uint8_t*)transformed+layout.start+y*layout.stride;
                    const uint8_t* src=(const uint8_t*)command->data+layout.start+(context->webgl_flip?height-1-y:y)*layout.stride;
                    memcpy(dst,src,width*layout.pixel);
                    if(context->webgl_premultiply) for(unsigned x=0;x<width;x++) {
                        if(n[fi+1]==GL_UNSIGNED_BYTE) for(unsigned c=0;c<3;c++) dst[x*4+c]=((unsigned)dst[x*4+c]*dst[x*4+3]+127)/255;
                        else { float* pixel=(float*)dst+x*4;for(unsigned c=0;c<3;c++) pixel[c]*=pixel[3]; }
                    }
                }
            }
            const void* data=transformed?transformed:command->data?command->data:zero;
            if(three) {
                if(sub) gl.TexSubImage3D(n[0],n[1],n[2],n[3],n[4],width,height,depth,n[fi],n[fi+1],data);
                else gl.TexImage3D(n[0],n[1],n[2],width,height,depth,n[6],n[fi],n[fi+1],data);
            } else {
                if(sub) gl.TexSubImage2D(n[0],n[1],n[2],n[3],width,height,n[fi],n[fi+1],data);
                else gl.TexImage2D(n[0],n[1],n[2],width,height,n[5],n[fi],n[fi+1],data);
            }
            mem_free(transformed);mem_free(zero);break;
        }
        case WEBGL_texStorage2D: case WEBGL_texStorage3D: {
            bool three=command->op==WEBGL_texStorage3D;
            unsigned width=n[3],height=n[4],depth=three?n[5]:1,levels=n[1];
            if(n[3]<=0||n[4]<=0||n[1]<=0||levels>13||width>4096||height>4096||!depth||depth>4096) return fail(GL_INVALID_VALUE);
            GLenum binding=webgl_texture_binding(n[0]);if(!binding) return fail(GL_INVALID_ENUM);
            NativeGlSlot* slot=webgl_binding(context,binding,NATIVE_GL_TEXTURE);if(!slot||slot->immutable) return fail(GL_INVALID_OPERATION);
            const WebGlStorageFormat* storage_format=nullptr;
            for(const auto& candidate:webgl_storage_formats) if(candidate.internal==(unsigned)n[2]) {storage_format=&candidate;break;}
            if(!storage_format) return fail(GL_INVALID_ENUM);
            GLenum format=storage_format->format,type=storage_format->type;
            unsigned maximum=width>height?width:height;if(n[0]==GL_TEXTURE_3D&&depth>maximum) maximum=depth;
            unsigned max_levels=1;while(maximum>1) { maximum/=2;max_levels++; }
            if(levels>max_levels) return fail(GL_INVALID_OPERATION);
            if(webgl_binding(context,GL_PIXEL_UNPACK_BUFFER_BINDING,NATIVE_GL_BUFFER)) return fail(GL_INVALID_OPERATION);
            if(!webgl_resize_bytes(context,slot,(size_t)width*height*depth*(n[0]==GL_TEXTURE_CUBE_MAP?6:1)*32)) break;
            // GL 4.1 lacks TexStorage; allocate the complete immutable mip chain on the shared provider.
            for(unsigned level=0;level<levels;level++) {
                size_t bytes=0;if(!webgl_pixel_bytes(context,width,height,depth,format,type,false,&bytes)) break;
                void* zeros=mem_calloc(1,bytes,MEM_CAT_RENDER);if(bytes&&!zeros) return fail(GL_OUT_OF_MEMORY);
                if(three) gl.TexImage3D(n[0],level,n[2],width,height,depth,0,format,type,zeros);
                else if(n[0]==GL_TEXTURE_CUBE_MAP) for(unsigned face=0;face<6;face++) gl.TexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,level,n[2],width,height,0,format,type,zeros);
                else gl.TexImage2D(n[0],level,n[2],width,height,0,format,type,zeros);
                mem_free(zeros);width=width>1?width/2:1;height=height>1?height/2:1;
                if(n[0]==GL_TEXTURE_3D) depth=depth>1?depth/2:1;
            }
            slot->immutable=true;slot->immutable_levels=levels;break;
        }
        case WEBGL_enableVertexAttribArray: case WEBGL_disableVertexAttribArray: case WEBGL_vertexAttribPointer: case WEBGL_vertexAttribIPointer: case WEBGL_vertexAttribDivisor: {
            if(n[0]>=16) return fail(GL_INVALID_VALUE);
            unsigned index=n[0];NativeGlSlot* vao=webgl_binding(context,GL_VERTEX_ARRAY_BINDING,NATIVE_GL_VERTEX_ARRAY);if(!vao) return fail(GL_INVALID_OPERATION);
            NativeGlAttribute& a=vao->attributes[index];
            if(command->op==WEBGL_enableVertexAttribArray) { gl.EnableVertexAttribArray(index);vao->enabled_attributes|=1u<<index; }
            else if(command->op==WEBGL_disableVertexAttribArray) { gl.DisableVertexAttribArray(index);vao->enabled_attributes&=~(1u<<index); }
            else if(command->op==WEBGL_vertexAttribDivisor) { gl.VertexAttribDivisor(index,n[1]);a.divisor=n[1]; }
            else {
                bool integer=command->op==WEBGL_vertexAttribIPointer;unsigned si=integer?3:4,oi=si+1;
                unsigned scalar=webgl_scalar_bytes(n[2]);NativeGlSlot* buffer=webgl_binding(context,GL_ARRAY_BUFFER_BINDING,NATIVE_GL_BUFFER);
                if(!scalar) return fail(GL_INVALID_ENUM);
                if(n[1]<1||n[1]>4||n[si]<0||n[si]>255||n[oi]<0) return fail(GL_INVALID_VALUE);
                if(!buffer||(unsigned)n[si]%scalar||(size_t)n[oi]%scalar) return fail(GL_INVALID_OPERATION);
                a.buffer={buffer->id};a.components=n[1];a.stride=n[si];a.offset=n[oi];vao->attribute_types[index]=n[2];
                if(integer) gl.VertexAttribIPointer(index,n[1],n[2],n[si],(const void*)(uintptr_t)a.offset);
                else gl.VertexAttribPointer(index,n[1],n[2],n[3]!=0,n[si],(const void*)(uintptr_t)a.offset);
            }break;
        }
        case WEBGL_drawArrays: case WEBGL_drawArraysInstanced: case WEBGL_drawElements: case WEBGL_drawElementsInstanced: {
            bool indexed=command->op==WEBGL_drawElements||command->op==WEBGL_drawElementsInstanced;
            int count=n[indexed?1:2],instances=(command->op==WEBGL_drawArraysInstanced?n[3]:command->op==WEBGL_drawElementsInstanced?n[4]:1);
            if(!webgl_draw_range(context,n[0],indexed?0:n[1],count,indexed?n[2]:0,indexed?n[3]:0,instances,indexed)) break;
            webgl_prepare_default(context);
            if(indexed) { gl.PrimitiveRestartIndex(n[2]==GL_UNSIGNED_BYTE?0xffu:n[2]==GL_UNSIGNED_SHORT?0xffffu:0xffffffffu);gl.DrawElementsInstanced(n[0],count,n[2],(const void*)(uintptr_t)(size_t)n[3],instances); }
            else gl.DrawArraysInstanced(n[0],n[1],count,instances);
            context->stats.draws++;context->webgl_dirty|=webgl_default_bound(context,GL_DRAW_FRAMEBUFFER_BINDING);break;
        }
        case WEBGL_framebufferTexture2D: case WEBGL_framebufferRenderbuffer: {
            NativeGlSlot* framebuffer=webgl_binding(context,n[0]==GL_READ_FRAMEBUFFER?GL_READ_FRAMEBUFFER_BINDING:GL_DRAW_FRAMEBUFFER_BINDING,NATIVE_GL_FRAMEBUFFER);
            if(!framebuffer) return fail(GL_INVALID_OPERATION);
            bool texture=command->op==WEBGL_framebufferTexture2D;
            NativeGlSlot* slot=command->resource[3]?resource(3,texture?NATIVE_GL_TEXTURE:NATIVE_GL_RENDERBUFFER):nullptr;
            if(command->resource[3]&&!slot) break;
            if(texture) gl.FramebufferTexture2D(n[0],n[1],n[2],slot?slot->name:0,n[4]);
            else gl.FramebufferRenderbuffer(n[0],n[1],n[2],slot?slot->name:0);break;
        }
        case WEBGL_checkFramebufferStatus: number(gl.CheckFramebufferStatus(n[0]));break;
        case WEBGL_drawBuffers: {
            if(command->bytes>16*sizeof(GLenum)) return fail(GL_INVALID_VALUE);
            GLenum buffers[16];memcpy(buffers,command->data,command->bytes);
            GLint framebuffer;gl.GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&framebuffer);
            NativeGlSlot* target=native_gl_slot(context,context->webgl_target,NATIVE_GL_TARGET);
            if(framebuffer==(GLint)target->name) {
                if(command->bytes!=sizeof(GLenum)||(buffers[0]!=GL_BACK&&buffers[0]!=GL_NONE)) return fail(GL_INVALID_OPERATION);
                if(buffers[0]==GL_BACK) buffers[0]=GL_COLOR_ATTACHMENT0;
            }
            gl.DrawBuffers(command->bytes/sizeof(GLenum),buffers);break;
        }
        case WEBGL_readBuffer: {
            GLint framebuffer;gl.GetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&framebuffer);
            NativeGlSlot* target=native_gl_slot(context,context->webgl_target,NATIVE_GL_TARGET);
            GLenum source=n[0];if(framebuffer==(GLint)target->name) { if(source!=GL_BACK&&source!=GL_NONE) return fail(GL_INVALID_OPERATION);if(source==GL_BACK) source=GL_COLOR_ATTACHMENT0; }
            gl.ReadBuffer(source);break;
        }
        case WEBGL_readPixels: {
            if(n[2]<0||n[3]<0||n[2]>4096||n[3]>4096) return fail(GL_INVALID_VALUE);
            if(webgl_binding(context,GL_PIXEL_PACK_BUFFER_BINDING,NATIVE_GL_BUFFER)) return fail(GL_INVALID_OPERATION);
            GLint read_buffer;gl.GetIntegerv(GL_READ_BUFFER,&read_buffer);if(read_buffer==GL_NONE) return fail(GL_INVALID_OPERATION);
            size_t bytes=0;if(!webgl_pixel_bytes(context,n[2],n[3],1,n[4],n[5],true,&bytes)) break;
            if(!command->data||bytes>command->bytes) return fail(GL_INVALID_OPERATION);
            webgl_prepare_default(context);
            GLint read,draw;gl.GetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);gl.GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);
            NativeGlSlot* target=native_gl_slot(context,context->webgl_target,NATIVE_GL_TARGET);
            if(read==(GLint)target->name) {
                native_gl_resolve(context,target);gl.BindFramebuffer(GL_READ_FRAMEBUFFER,target->resolve);
                WebGlPixelLayout layout={};webgl_pixel_layout(context,n[2],n[3],1,n[4],n[5],true,&layout);
                // WebGL readPixels leaves destination bytes outside the framebuffer untouched.
                int x0=n[0]<0?0:n[0],y0=n[1]<0?0:n[1];
                double x_end=n[0]+n[2],y_end=n[1]+n[3];
                int x1=x_end>target->width?target->width:x_end,y1=y_end>target->height?target->height:y_end;
                if(x1>x0&&y1>y0) {
                    GLint row,skip_rows,skip_pixels;gl.GetIntegerv(GL_PACK_ROW_LENGTH,&row);gl.GetIntegerv(GL_PACK_SKIP_ROWS,&skip_rows);gl.GetIntegerv(GL_PACK_SKIP_PIXELS,&skip_pixels);
                    gl.PixelStorei(GL_PACK_ROW_LENGTH,row?row:n[2]);gl.PixelStorei(GL_PACK_SKIP_ROWS,0);gl.PixelStorei(GL_PACK_SKIP_PIXELS,0);
                    void* destination=(uint8_t*)command->data+layout.start+(size_t)(y0-n[1])*layout.stride+(size_t)(x0-n[0])*layout.pixel;
                    gl.ReadPixels(x0,y0,x1-x0,y1-y0,n[4],n[5],destination);
                    gl.PixelStorei(GL_PACK_ROW_LENGTH,row);gl.PixelStorei(GL_PACK_SKIP_ROWS,skip_rows);gl.PixelStorei(GL_PACK_SKIP_PIXELS,skip_pixels);
                }
            } else gl.ReadPixels(n[0],n[1],n[2],n[3],n[4],n[5],(void*)command->data);
            gl.BindFramebuffer(GL_READ_FRAMEBUFFER,read);gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER,draw);break;
        }
        case WEBGL_uniform1f:
        case WEBGL_uniform1fv:
        case WEBGL_uniform1i:
        case WEBGL_uniform1iv:
        case WEBGL_uniform1ui:
        case WEBGL_uniform1uiv:
        case WEBGL_uniform2f:
        case WEBGL_uniform2fv:
        case WEBGL_uniform2i:
        case WEBGL_uniform2iv:
        case WEBGL_uniform2ui:
        case WEBGL_uniform2uiv:
        case WEBGL_uniform3f:
        case WEBGL_uniform3fv:
        case WEBGL_uniform3i:
        case WEBGL_uniform3iv:
        case WEBGL_uniform3ui:
        case WEBGL_uniform3uiv:
        case WEBGL_uniform4f:
        case WEBGL_uniform4fv:
        case WEBGL_uniform4i:
        case WEBGL_uniform4iv:
        case WEBGL_uniform4ui:
        case WEBGL_uniform4uiv:
        case WEBGL_uniformMatrix2fv:
        case WEBGL_uniformMatrix3fv:
        case WEBGL_uniformMatrix4fv:
        {
            if(!command->resource[0]) break; // a null uniform location is an explicit no-op.
            NativeGlSlot* program=resource(0,NATIVE_GL_PROGRAM);
            if(!program||program!=webgl_binding(context,GL_CURRENT_PROGRAM,NATIVE_GL_PROGRAM)||program->link_generation!=command->resource[1]) return fail(GL_INVALID_OPERATION);
            GLint location=n[0];
            switch(command->op) {
#define WEBGL_UNIFORM_COMPONENTS(M,suffix,scalar) \
    M(1,suffix,scalar,n[1]) \
    M(2,suffix,scalar,n[1],n[2]) \
    M(3,suffix,scalar,n[1],n[2],n[3]) \
    M(4,suffix,scalar,n[1],n[2],n[3],n[4])
#define WEBGL_UNIFORM_DISPATCH(components,suffix,scalar,...) \
    case WEBGL_uniform##components##suffix: gl.Uniform##components##suffix(location,__VA_ARGS__);break; \
    case WEBGL_uniform##components##suffix##v: \
        if(command->bytes%(sizeof(scalar)*components)) return fail(GL_INVALID_VALUE); \
        gl.Uniform##components##suffix##v(location,command->bytes/(sizeof(scalar)*components),(const scalar*)command->data);break;
                WEBGL_UNIFORM_COMPONENTS(WEBGL_UNIFORM_DISPATCH,f,GLfloat)
                WEBGL_UNIFORM_COMPONENTS(WEBGL_UNIFORM_DISPATCH,i,GLint)
                WEBGL_UNIFORM_COMPONENTS(WEBGL_UNIFORM_DISPATCH,ui,GLuint)
#undef WEBGL_UNIFORM_DISPATCH
#undef WEBGL_UNIFORM_COMPONENTS
#define WEBGL_MATRIX_DISPATCH(dimension) \
    case WEBGL_uniformMatrix##dimension##fv: \
        if(command->bytes%(sizeof(GLfloat)*dimension*dimension)||n[1]) return fail(GL_INVALID_VALUE); \
        gl.UniformMatrix##dimension##fv(location,command->bytes/(sizeof(GLfloat)*dimension*dimension),GL_FALSE,(const GLfloat*)command->data);break;
                WEBGL_MATRIX_DISPATCH(2)
                WEBGL_MATRIX_DISPATCH(3)
                WEBGL_MATRIX_DISPATCH(4)
#undef WEBGL_MATRIX_DISPATCH
                default:break;
            }break;
        }
#define WEBGL_ATTRIBUTE_VECTOR(components) \
        case WEBGL_vertexAttrib##components##fv: \
            if(n[0]>=16||command->bytes<components*sizeof(GLfloat)) return fail(GL_INVALID_VALUE); \
            gl.VertexAttrib##components##fv(n[0],(const GLfloat*)command->data);break;
        WEBGL_ATTRIBUTE_VECTOR(1)
        WEBGL_ATTRIBUTE_VECTOR(2)
        WEBGL_ATTRIBUTE_VECTOR(3)
        WEBGL_ATTRIBUTE_VECTOR(4)
#undef WEBGL_ATTRIBUTE_VECTOR
        case WEBGL_getParameter: {
            GLenum pname=n[0];GLint value[16]={};reply->kind=WEBGL_NUMBER;
            if(pname==GL_VERSION||pname==GL_SHADING_LANGUAGE_VERSION||pname==GL_VENDOR||pname==GL_RENDERER) {
                reply->kind=WEBGL_STRING;reply->source=pname==GL_VERSION?"WebGL 2.0 (Radiant native OpenGL)":pname==GL_SHADING_LANGUAGE_VERSION?"WebGL GLSL ES 3.00 (Radiant desktop adapter)":(const char*)gl.GetString(pname);break;
            }
            if(pname==0x9240||pname==0x9241||pname==0x9243) { number(pname==0x9240?context->webgl_flip:pname==0x9241?context->webgl_premultiply:context->webgl_colorspace,pname!=0x9243);break; }
            switch(pname) {
                case GL_COPY_READ_BUFFER:case GL_COPY_WRITE_BUFFER:case GL_UNIFORM_BUFFER_BINDING:case GL_TRANSFORM_FEEDBACK_BUFFER_BINDING:case GL_ARRAY_BUFFER_BINDING: case GL_ELEMENT_ARRAY_BUFFER_BINDING: case GL_PIXEL_PACK_BUFFER_BINDING: case GL_PIXEL_UNPACK_BUFFER_BINDING: case GL_CURRENT_PROGRAM: case GL_VERTEX_ARRAY_BINDING: case GL_FRAMEBUFFER_BINDING: case GL_READ_FRAMEBUFFER_BINDING: case GL_RENDERBUFFER_BINDING: case GL_TEXTURE_BINDING_2D: case GL_TEXTURE_BINDING_CUBE_MAP: case GL_TEXTURE_BINDING_3D: case GL_TEXTURE_BINDING_2D_ARRAY: {
                    NativeGlKind kind=(pname==GL_CURRENT_PROGRAM?NATIVE_GL_PROGRAM:pname==GL_VERTEX_ARRAY_BINDING?NATIVE_GL_VERTEX_ARRAY:pname==GL_FRAMEBUFFER_BINDING||pname==GL_READ_FRAMEBUFFER_BINDING?NATIVE_GL_FRAMEBUFFER:pname==GL_RENDERBUFFER_BINDING?NATIVE_GL_RENDERBUFFER:pname==GL_TEXTURE_BINDING_2D||pname==GL_TEXTURE_BINDING_CUBE_MAP||pname==GL_TEXTURE_BINDING_3D||pname==GL_TEXTURE_BINDING_2D_ARRAY?NATIVE_GL_TEXTURE:NATIVE_GL_BUFFER);
                    NativeGlSlot* slot=webgl_binding(context,pname,kind);
                    if(slot&&slot->id!=context->webgl_vao.id) { reply->kind=WEBGL_RESOURCE;reply->resource=slot->id;reply->resource_kind=kind;reply->generation=context->stats.generation; }else reply->kind=WEBGL_VOID;
                    break;
                }
                case GL_VIEWPORT: case GL_SCISSOR_BOX: case GL_MAX_VIEWPORT_DIMS:
                    gl.GetIntegerv(pname,value);reply->kind=WEBGL_NUMBERS;reply->count=pname==GL_MAX_VIEWPORT_DIMS?2:4;for(unsigned i=0;i<reply->count;i++) reply->n[i]=value[i];break;
                case GL_COLOR_CLEAR_VALUE: case GL_BLEND_COLOR: case GL_DEPTH_RANGE: case GL_ALIASED_LINE_WIDTH_RANGE: case GL_ALIASED_POINT_SIZE_RANGE: {
                    GLfloat values[4]={};gl.GetFloatv(pname,values);reply->kind=WEBGL_NUMBERS;reply->count=(pname==GL_COLOR_CLEAR_VALUE||pname==GL_BLEND_COLOR)?4:2;
                    for(unsigned i=0;i<reply->count;i++) reply->n[i]=values[i];reply->resource_kind=1;break;
                }
                case 0x8DFB: gl.GetIntegerv(GL_MAX_VERTEX_UNIFORM_COMPONENTS,value);number(value[0]/4);break;
                case 0x8DFD: gl.GetIntegerv(GL_MAX_FRAGMENT_UNIFORM_COMPONENTS,value);number(value[0]/4);break;
                case 0x8DFC: case GL_MAX_VARYING_COMPONENTS: {
                    // core GL removed MAX_VARYING_COMPONENTS; both stage interfaces bound the usable varyings.
                    GLint outputs,inputs;gl.GetIntegerv(GL_MAX_VERTEX_OUTPUT_COMPONENTS,&outputs);gl.GetIntegerv(GL_MAX_FRAGMENT_INPUT_COMPONENTS,&inputs);
                    number((outputs<inputs?outputs:inputs)/(pname==0x8DFC?4:1));break;
                }
                case GL_MAX_TEXTURE_SIZE: case GL_MAX_CUBE_MAP_TEXTURE_SIZE: case GL_MAX_3D_TEXTURE_SIZE: gl.GetIntegerv(pname,value);number(value[0]>4096?4096:value[0]);break;
                case GL_MAX_VERTEX_ATTRIBS: gl.GetIntegerv(pname,value);number(value[0]>16?16:value[0]);break;
                case GL_DEPTH_TEST: case GL_STENCIL_TEST: case GL_BLEND: case GL_CULL_FACE: case GL_SCISSOR_TEST: case GL_POLYGON_OFFSET_FILL: case GL_SAMPLE_ALPHA_TO_COVERAGE: case GL_SAMPLE_COVERAGE: case GL_DITHER: number(gl.IsEnabled(pname),true);break;
                case GL_DEPTH_CLEAR_VALUE: case GL_LINE_WIDTH: case GL_POLYGON_OFFSET_FACTOR: case GL_POLYGON_OFFSET_UNITS: { GLfloat v;gl.GetFloatv(pname,&v);number(v);break; }
                case GL_DEPTH_WRITEMASK: gl.GetIntegerv(pname,value);number(value[0],true);break;
                case GL_COLOR_WRITEMASK: gl.GetIntegerv(pname,value);reply->kind=WEBGL_NUMBERS;reply->count=4;reply->resource_kind=2;for(unsigned i=0;i<4;i++) reply->n[i]=value[i];break;
                case GL_SAMPLER_BINDING:case GL_TRANSFORM_FEEDBACK_BINDING:reply->kind=WEBGL_VOID;break;
                case GL_COMPRESSED_TEXTURE_FORMATS: reply->kind=WEBGL_NUMBERS;reply->count=0;reply->resource_kind=3;break;
                default: {
                    WebGlParameterKind kind=webgl_parameter_kind(pname);
                    if(kind==PARAM_INVALID) { reply->kind=WEBGL_VOID;return fail(GL_INVALID_ENUM); }
                    if(pname==GL_READ_BUFFER||pname==GL_DRAW_BUFFER0) {
                        gl.GetIntegerv(pname,value);
                        if(value[0]==GL_COLOR_ATTACHMENT0&&webgl_default_bound(context,pname==GL_READ_BUFFER?GL_READ_FRAMEBUFFER_BINDING:GL_DRAW_FRAMEBUFFER_BINDING)) value[0]=GL_BACK;
                        number(value[0]);
                    } else if(pname==0x9247) number(0); // no sync API is exposed by this profile
                    else if(pname==GL_IMPLEMENTATION_COLOR_READ_FORMAT) number(GL_RGBA);
                    else if(pname==GL_IMPLEMENTATION_COLOR_READ_TYPE) number(GL_UNSIGNED_BYTE);
                    else if(kind==PARAM_FLOAT) { GLfloat v=0;gl.GetFloatv(pname,&v);number(v); }
                    else if(kind==PARAM_INT64) { GLint64 v=0;gl.GetInteger64v(pname,&v);number(v); }
                    else { gl.GetIntegerv(pname,value);number(value[0],kind==PARAM_BOOLEAN); }
                    break;
                }
            }break;
        }
        default: return false;
    }
    for(unsigned error;(error=gl.GetError())!=GL_NO_ERROR;) {
        log_debug("webgl driver: %s(%g, %g) generated 0x%x",webgl_op_name(command->op),n[0],n[1],error);
        native_gl_webgl_error(context,error);
    }
    return true;
#else
    return false;
#endif
}
bool native_gl_extension_supported(NativeGlContext* context,const char* name) {
#ifdef NATIVE_GL_ENABLED
    NativeGlScope scope(context);if(!scope.valid||!name) return false;
    bool floating=!strcmp(name,"EXT_color_buffer_float"),linear=!strcmp(name,"OES_texture_float_linear");
    if(!floating&&!linear) return false;
    if(!context->extension_checked) {
        NativeGlFunctions& gl=context->gl;webgl_collect_errors(context);
        GLint texture,read,draw,unpack_buffer;
        gl.GetIntegerv(GL_TEXTURE_BINDING_2D,&texture);gl.GetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);
        gl.GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);gl.GetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&unpack_buffer);
        WebGlImageUnpackScope unpack(gl,true);gl.BindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
        GLuint probe_texture=0,probe_framebuffer=0;gl.GenTextures(1,&probe_texture);gl.GenFramebuffers(1,&probe_framebuffer);
        gl.BindTexture(GL_TEXTURE_2D,probe_texture);gl.BindFramebuffer(GL_FRAMEBUFFER,probe_framebuffer);
        gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        bool supported=true;
        // advertise the extension only after every required color format is actually framebuffer-renderable.
        for(const auto& format:webgl_storage_formats) if(format.floating) {
            gl.TexImage2D(GL_TEXTURE_2D,0,format.internal,1,1,0,format.format,format.type,nullptr);
            gl.FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,probe_texture,0);
            supported=gl.CheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE&&supported;
        }
        while(gl.GetError()!=GL_NO_ERROR) supported=false;
        gl.BindFramebuffer(GL_READ_FRAMEBUFFER,read);gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER,draw);
        gl.BindTexture(GL_TEXTURE_2D,texture);gl.BindBuffer(GL_PIXEL_UNPACK_BUFFER,unpack_buffer);
        gl.DeleteFramebuffers(1,&probe_framebuffer);gl.DeleteTextures(1,&probe_texture);
        context->extension_checked=true;context->floating_targets=supported;
    }
    // native contexts require desktop GL 3.3+, where float texture filtering is core functionality.
    return context->floating_targets;
#else
    return false;
#endif
}
