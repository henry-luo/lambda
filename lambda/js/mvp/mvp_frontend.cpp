#include "mvp.h"

#include "../js_transpiler.hpp"
#include "../../../lib/mem.h"

#include <string.h>

struct MvpAstUnit {
    JsTranspiler* transpiler;
};

MvpAstUnit* mvp_ast_parse(const char* source, size_t source_length) {
    if (!source || source_length == 0) return NULL;
    JsTranspiler* transpiler = js_transpiler_create(NULL);
    if (!transpiler) return NULL;
    // The parser/binder is a frontend service. Passing NULL deliberately
    // prevents this path from admitting a runtime or old JS execution state.
    if (!js_transpiler_parse_c(transpiler, source, source_length, JS_PARSE_AUTO) ||
            transpiler->has_errors || !transpiler->ast_root) {
        js_transpiler_destroy(transpiler);
        return NULL;
    }
    MvpAstUnit* unit = (MvpAstUnit*)mem_calloc(1, sizeof(MvpAstUnit), MEM_CAT_JS_RUNTIME);
    if (!unit) {
        js_transpiler_destroy(transpiler);
        return NULL;
    }
    unit->transpiler = transpiler;
    return unit;
}

void* mvp_ast_root(const MvpAstUnit* unit) {
    return unit && unit->transpiler ? unit->transpiler->ast_root : NULL;
}

void mvp_ast_destroy(MvpAstUnit* unit) {
    if (!unit) return;
    js_transpiler_destroy(unit->transpiler);
    mem_free(unit);
}
