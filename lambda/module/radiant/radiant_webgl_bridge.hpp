#pragma once
#include <stddef.h>
#include <stdint.h>

// plain command records keep graphics names and borrowed JS storage behind the waist (D7.4.1v2).
enum WebGlOp {
#define WEBGL_METHOD(name, js, signature, arity) WEBGL_##js,
#include "webgl_methods.def"
#undef WEBGL_METHOD
    WEBGL_OP_COUNT
};
static inline const char* webgl_op_name(WebGlOp op) {
    switch(op) {
#define WEBGL_METHOD(native,js,signature,arity) case WEBGL_##js:return #js;
#include "webgl_methods.def"
#undef WEBGL_METHOD
        default:return "unknown";
    }
}
struct WebGlCommand {
    WebGlOp op;
    double n[16];
    uint64_t resource[16];
    const void* data;
    size_t bytes;
    const char* text;
    bool compact_pixels; // host image sources use tightly packed rows, independently of client-view unpack state
    bool source_dimensions; // DOM-source overload supplies an explicit upload rectangle
};
enum WebGlReplyKind { WEBGL_VOID, WEBGL_NUMBER, WEBGL_BOOLEAN, WEBGL_STRING,
    WEBGL_NUMBERS, WEBGL_RESOURCE, WEBGL_LOCATION, WEBGL_ACTIVE_INFO, WEBGL_PRECISION };
struct WebGlReply {
    WebGlReplyKind kind;
    double n[16];
    unsigned count, resource_kind;
    uint64_t resource, generation, program;
    char text[8192];
    const char* source; // copied into a script string before returning to the caller
};
struct WebGlOptions { bool alpha, depth, stencil, antialias, premultiplied_alpha, preserve; };
extern "C" {
uint64_t radiant_webgl_create(void* canvas, const WebGlOptions* options);
bool radiant_webgl_call(uint64_t canvas, const WebGlCommand* command, WebGlReply* reply);
bool radiant_webgl_image(uint64_t canvas, const WebGlCommand* command, void* source, WebGlReply* reply);
bool radiant_webgl_extension_supported(uint64_t canvas, const char* name);
void radiant_webgl_error(uint64_t canvas, unsigned error);
bool radiant_webgl_resize(void* canvas, unsigned width, unsigned height);
bool radiant_webgl_has_context(void* canvas);
}
extern "C" bool radiant_webgl_lose(uint64_t canvas);
extern "C" bool radiant_webgl_restore(uint64_t canvas);

struct NativeGlStats;
extern "C" bool radiant_webgl_stats(void* canvas,NativeGlStats* stats);
