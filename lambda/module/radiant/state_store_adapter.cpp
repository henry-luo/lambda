#include "state_store_adapter.hpp"
#include "radiant/state_store_host.hpp"
#include "radiant/event.hpp"
#include "lambda/input/css/dom_lifecycle.hpp"
#include "../../runtime/runtime-state.h"
#include "../../runtime/transpiler.hpp"
#include "../../runtime/heap_api.h"
#include "../../runtime/template_state.h"
#include "../../runtime/render_map.h"

extern __thread EvalContext* context;

static bool initialize(DomDocument* document, DocState* state, EvalContext** owner) {
    Runtime* runtime = dom_document_script_runtime(document);
    *owner = runtime ? runtime_get_eval_context(runtime) : nullptr;
    if (!*owner) return true;
    // Runtime maps are bound only after claiming the document's owning context.
    if (!eval_context_init(*owner)) {
        *owner = nullptr;
        return false;
    }
    tmpl_state_init();
    state->template_state_map = tmpl_state_get_map();
    render_map_init();
    state->render_map = render_map_get_map();
    render_map_set_path_recorder(&render_map_record_path);
    return true;
}

static bool enter(EvalContext* owner) {
    return eval_context_init(owner);
}

static EvalContext* current() { return context; }

static void release_root(EvalContext* owner, uint64_t* root) {
    heap_unregister_gc_root_for((Context*)owner, root);
}

static bool register_current_root(uint64_t* root) { return heap_try_register_gc_root(root); }
static void release_current_root(uint64_t* root) { heap_unregister_gc_root(root); }

static void detach_maps(DocState* state) {
    if (state->template_state_map) tmpl_state_set_map(nullptr);
    if (state->render_map) render_map_set_map(nullptr);
}

bool lambda_state_store_host_register() {
    static const RadiantStateStoreHostOps operations = {
        initialize, enter, current, release_root, register_current_root, release_current_root, detach_maps,
    };
    return radiant_state_store_host_register(&operations);
}
