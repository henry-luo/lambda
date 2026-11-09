#include "loader_runtime_adapter.hpp"
#include "radiant_dom_bridge.hpp"
#include "radiant/loader_runtime_host.hpp"
#include "radiant/radiant.hpp"
#include "radiant/source_position_host.hpp"
#include "../../input/css/dom_element.hpp"
#include "../../input/input.hpp"
#include "../../runtime/transpiler.hpp"
#include "../../runtime/concurrency.h"
#include "../../runtime/activation.h"
#include "../../runtime/lambda-stack.h"
#include "../../runtime/radiant_event_hook.h"
#include "../../runtime/lambda-error.h"
#include "../../runtime/render_map.h"
#include "../../runtime/runtime-state.h"
#include "../../runtime/interp.hpp"
#include "../../runtime/lambda-root-frame.hpp"
#include "../../js/js_runtime.h"
#include "../../js/js_runtime_state.hpp"
#include "../../js/js_event_loop.h"
#include "../../dom/dom.h"
#include "../../dom/dom_events.h"
#include "../../dom/dom_observers.h"
#include "../../io/mark_builder.hpp"
#include "../../lambda.h"
#include "lib/log.h"
#include "lib/mem.h"
#include "lib/mem_factory.h"
#include "lib/time_util.h"
#include <stdio.h>
#include <string.h>

extern __thread EvalContext* context;
extern __thread Context* input_context;
// EO5v2: make `target` the thread's bound EvalContext, releasing whatever is
// bound now. A document — including an iframe's — manages its own Runtime and
// EvalContext, and a thread holds one at a time, so the binding moves between
// documents here.
//
// Legal only at a quiescent point: no handler, nested dispatch or guest code
// may be running, because the outgoing context's frames would still be live.
// The side-stack roots the GC walks are per-Context, so an unbound context
// keeps its own roots; what must not happen is a switch while frames are open.
// This is not the replace-and-restore that `runtime-state.h` forbids: there is
// no restore, the new owner simply holds the thread until the next boundary.
extern "C" bool radiant_eval_context_switch(EvalContext* target) {
    return eval_context_transfer_thread(target);
}


static void release_layout_runtime(Runtime* runtime) {
    if (!runtime) return;
    runtime_cleanup(runtime);
    mem_free(runtime);
}

static Runtime* loader_runtime_get(UiContext* host) {
    if (!host) return nullptr;
    if (!host->loader_runtime) {
        Runtime* runtime = (Runtime*)mem_calloc(1, sizeof(Runtime), MEM_CAT_LAYOUT);
        if (!runtime) return nullptr;
        runtime_init(runtime);
        host->loader_runtime = lam::own(runtime);
        log_info("loader-runtime: created the window loader runtime");
    }
    return host->loader_runtime.get();
}

struct LoaderAttributeRoots : DomDocumentResourceData {
    void* roots;
};

static void loader_attribute_roots_destroy(DomDocumentResourceData* data) {
    LoaderAttributeRoots* held = (LoaderAttributeRoots*)data;
    if (!held) return;
    ui_attribute_roots_release(held->roots);
    mem_free(held);
}

static void loader_runtime_finish(DomDocument* doc, Runtime* loader) {
    if (!loader) return;
    void* roots = runtime_take_ui_attribute_roots(loader);
    runtime_bind_ui_result_input(loader, nullptr);
    if (!roots) return;
    if (!doc) {
        ui_attribute_roots_release(roots);
        return;
    }
    LoaderAttributeRoots* held = (LoaderAttributeRoots*)mem_calloc(
        1, sizeof(LoaderAttributeRoots), MEM_CAT_LAYOUT);
    if (held) held->roots = roots;
    if (!held || !dom_document_add_resource(doc, held, loader_attribute_roots_destroy)) {
        // Keep the published values rooted for the runtime's life on handoff failure.
        log_error("loader-runtime: could not hand attribute roots to the document");
        if (held) mem_free(held);
    }
}

Runtime* layout_document_package_runtime(DomDocument* document, const char* package_module) {
    if (!document) return nullptr;
    Runtime* runtime = dom_document_loader_runtime(document);
    if (runtime) {
        // Package paint roots outlive JS realm teardown and retain their evaluator (D6.3.3).
        if (!document->loader_runtime && !document->lambda_runtime && runtime == document->js.runtime) {
            document->lambda_runtime = runtime;
            document->owns_script_runtime = true;
        }
        return runtime;
    }
    if (document->destroying || lambda_document_lifetime_guard_active(document)) return nullptr;
    // An active render must use its caller's evaluator while its roots are live.
    if (context && context->execution_depth != 0) return context->runtime;
    runtime = (Runtime*)mem_calloc(1, sizeof(Runtime), MEM_CAT_LAYOUT);
    if (!runtime) return nullptr;
    runtime_init(runtime);
    runtime->resource_policy = document->resource_policy;
    if (!radiant_eval_context_switch(runtime_get_eval_context(runtime))) {
        log_error("package-layout-runtime: failed to acquire evaluator for %s", package_module);
        release_layout_runtime(runtime);
        return nullptr;
    }
    Script* package = nullptr;
    if (!run_lambda_package_module(runtime, package_module, &package) || !package) {
        log_error("package-layout-runtime: failed to initialize %s", package_module);
        release_layout_runtime(runtime);
        return nullptr;
    }
    document->lambda_runtime = runtime;
    document->owns_script_runtime = true;
    return runtime;
}

static thread_local char document_load_diagnostic[512];

static void loader_clear_diagnostic() { document_load_diagnostic[0] = '\0'; }

static bool loader_prepare_script() {
    loader_clear_diagnostic();
    if (!context || context->execution_depth == 0) return true;
    log_error("load_lambda_script_doc: evaluator still executing");
    return false;
}

const char* lambda_document_load_diagnostic(void) {
    return document_load_diagnostic[0] ? document_load_diagnostic : nullptr;
}

static void loader_cancel_script(RadiantScriptDocumentLoad* load, bool runtime_released) {
    if (load->shared) loader_runtime_finish(nullptr, load->runtime);
    else if (!runtime_released) release_layout_runtime(load->runtime);
    if (load->result_pool) pool_destroy(load->result_pool);
    *load = {};
}

static bool loader_begin_script(UiContext* host, Url* url, const char* path,
        const LambdaDocumentTransformConfig* transform,
        const LambdaDocumentTransformOption* options, int option_count,
        InputResourcePolicy policy, RadiantScriptDocumentLoad* load) {
    loader_clear_diagnostic();
    if (!url || !path || !load || (context && context->execution_depth != 0)) {
        log_error("load_lambda_script_doc: evaluator still executing or invalid source");
        return false;
    }
    load->stateless = transform && transform->stateless;
    load->runtime = load->stateless ? loader_runtime_get(host) : nullptr;
    load->shared = load->runtime != nullptr;
    if (!load->runtime) {
        load->runtime = (Runtime*)mem_calloc(1, sizeof(Runtime), MEM_CAT_LAYOUT);
        if (!load->runtime) return false;
        runtime_init(load->runtime);
    }
    Runtime* runtime = load->runtime;
    runtime->resource_policy = policy;
    EvalContext* owner = runtime_get_eval_context(runtime);
    if (!owner || !radiant_eval_context_switch(owner)) {
        log_error("load_lambda_script_doc: failed to acquire eval thread");
        loader_cancel_script(load, false);
        return false;
    }
    load->result_pool = mem_pool_create(nullptr, MEM_ROLE_LAYOUT, "cmd_layout");
    load->result_input = load->result_pool ? Input::create(load->result_pool, url) : nullptr;
    if (!load->result_input) {
        loader_cancel_script(load, false);
        return false;
    }
    load->result_input->ui_mode = true;
    runtime_bind_ui_result_input(runtime, load->result_input);
    if (!load->stateless) {
        source_pos_bridge_reset();
        render_map_init();
        render_map_set_path_recorder(&render_map_record_path);
    }
    // Transforms use the decoded local path; scripts receive their navigation URL separately.
    const char* target = transform ? path : url_get_href(url);
    load->script_output = transform
        ? run_lambda_document_transform_with_options(runtime, target, transform, options, option_count)
        : run_script_mir(runtime, nullptr, const_cast<char*>(path), false);
    if (runtime_heap(runtime)) {
        owner->heap = runtime_heap(runtime);
        owner->name_pool = runtime_name_pool(runtime);
        owner->pool = runtime_heap(runtime)->pool;
        runtime_context_follow_ui_result(runtime, owner);
        if (!eval_context_matches(owner)) {
            log_error("load_lambda_script_doc: eval owner changed during execution");
            loader_cancel_script(load, false);
            return false;
        }
        input_context = (Context*)owner;
    }
    if (!load->script_output || !load->script_output->root.item) {
        log_error("[Lambda Script] Failed to evaluate script or script returned null");
        loader_cancel_script(load, false);
        return false;
    }
    Item result = load->script_output->root;
    if (get_type_id(result) == LMD_TYPE_ERROR) {
        LambdaError* error = it2err(result);
        const char* message = error && error->message && error->message[0]
            ? error->message : "the document returned an error";
        snprintf(document_load_diagnostic, sizeof(document_load_diagnostic), "%s", message);
        log_error("document-load: %s", document_load_diagnostic);
        loader_cancel_script(load, false);
        return false;
    }
    return true;
}

static void loader_finish_script(RadiantScriptDocumentLoad* load, DomDocument* doc, Element* root) {
    if (!load->stateless) render_map_set_doc_root(Item{.element = root});
    input_context = nullptr;
    if (load->shared) {
        loader_runtime_finish(doc, load->runtime);
        doc->loader_runtime = lam::up(load->runtime);
    } else {
        doc->lambda_runtime = load->runtime;
    }
    // The document now owns its Input and any non-shared evaluator.
    *load = {};
}

static bool loader_render_embedded(UiContext* host, Input* input,
                                   Runtime** owned, Runtime** shared) {
    if (!input || !owned || !shared) return false;
    Runtime* markdown_runtime = nullptr;
    Runtime* shared_runtime = nullptr;
    Array* embedded_nodes[] = {input->embedded_math, input->embedded_diagrams};
    auto embedded_start = time_now_ns();
    shared_runtime = loader_runtime_get(host);
    Runtime* render_runtime = shared_runtime;
    if (!render_runtime) {
        render_runtime = (Runtime*)mem_calloc(1, sizeof(Runtime), MEM_CAT_LAYOUT);
        if (!render_runtime) {
            log_error("[Lambda Markdown] Failed to allocate embedded-content runtime");
            return false;
        }
        runtime_init(render_runtime);
    }
    EvalContext* render_context = runtime_get_eval_context(render_runtime);
    // D6.4.1: bind a loader only from a quiescent evaluator.
    bool render_bound = render_context && !(context && context->execution_depth != 0) &&
        radiant_eval_context_switch(render_context);
    runtime_bind_ui_result_input(render_runtime, input);
    unsigned replace_count = 0;
    for (unsigned kind = 0; render_bound && kind < 2; kind++) {
        Array* nodes = embedded_nodes[kind];
        if (!nodes || !nodes->length) continue;
        const LambdaDocumentTransformConfig* transform =
            lambda_document_transform_for_input_type(kind == 0 ? "math" : "graph");
        Script* package = nullptr;
        Input* package_result = transform
            ? run_lambda_package_module(render_runtime, transform->package_module, &package)
            : nullptr;
        if (!package_result || !package) {
            log_error("[Lambda Markdown] Embedded %s package initialization failed",
                      kind == 0 ? "math" : "Mermaid");
            continue;
        }
        RuntimeExecutionScope execution_scope(render_context);
        RootFrame roots(4);
        Rooted<Item> options(roots, ItemNull);
        Rooted<Item> option_name(roots, (Item){.item = s2it(heap_strcpy("display", 7))});
        Rooted<Item> argument(roots, ItemNull);
        Rooted<Item> rendered(roots, ItemNull);
        for (int64_t i = 0; i < nodes->length; i++) {
            Element* node = nodes->items[i].element;
            bool display = false;
            options.set(ItemNull);
            if (kind == 0) {
                ConstItem ast = node->get_attr("ast");
                argument.set(*(Item*)&ast);
                if (get_type_id(argument.get()) == LMD_TYPE_NULL) continue;
                String* type = node->get_attr("type").string();
                display = type && strcmp(type->chars, "block") == 0;
                options.set(vmap_new());
                if (get_type_id(options.get()) != LMD_TYPE_VMAP ||
                    item_is_error(vmap_set(options.get(), option_name.get(),
                        (Item){.item = b2it(display ? 1 : 0)}))) {
                    log_error("[Lambda Markdown] Failed to construct math render options");
                    break;
                }
            } else {
                if (!node->length) continue;
                argument.set(node->items[0]);
            }
            Item args[2] = {argument.get(), options.get()};
            rendered.set(interp_call_module_export(render_runtime, package,
                kind == 0 ? transform->function_name : "from_mermaid", args, 2));
            if (get_type_id(rendered.get()) != LMD_TYPE_ELEMENT) {
                log_error("[Lambda Markdown] Embedded %s rendering failed; retaining source",
                          kind == 0 ? "math" : "Mermaid");
                continue;
            }
            Item rendered_item = rendered.get();
            if (display) {
                MarkBuilder builder(input);
                ElementBuilder wrapper = builder.element("div");
                wrapper.attr("class", "math-display-container");
                wrapper.child(rendered_item);
                rendered_item = wrapper.final();
            }
            // Preserve each parent's pointer while replacing its embedded source node.
            *node = *rendered_item.element;
            replace_count++;
        }
    }
    input_context = nullptr;
    // Rendered shapes belong to the loaded packages, so retain their runtime.
    if (!shared_runtime && replace_count > 0) {
        markdown_runtime = render_runtime;
        render_runtime = nullptr;
    }
    if (!shared_runtime) release_layout_runtime(render_runtime);
    log_info("[TIMING] Markdown embedded rendering: %.1fms",
             time_elapsed_ms_f(embedded_start, time_now_ns()));
    *owned = markdown_runtime;
    *shared = shared_runtime;
    return true;
}

static void loader_begin_document_timing() {
    js_mir_begin_document_phase_timing();
}

static void loader_end_document_timing(RadiantScriptPhaseSummary* summary) {
    JsMirPhaseTiming timing = {};
    js_mir_end_document_phase_timing(&timing);
    *summary = {timing.parse_us, timing.ast_us, timing.early_us, timing.imports_us,
                timing.mir_us, timing.link_us, timing.execute_us, timing.cleanup_us,
                timing.total_us, timing.preamble_us};
}

bool radiant_bind_document_script_host(UiContext* uicon, DomDocument* doc) {
    if (!doc || !context || !js_runtime_state_init(context)) return false;
    // Lambda-only handlers share the same timing support, without claiming a JS realm.
    if (doc->js.virtual_clock_enabled && !js_event_loop_virtual_clock_enabled()) {
        js_event_loop_set_virtual_clock(true, doc->js.virtual_clock_ms);
    }
    dom_set_host_driven_loop(doc->js.host_driven_loop);
    dom_set_ui_context(uicon);
    dom_set_document(doc);
    return true;
}

static bool loader_bind_event_document(UiContext* ui, DomDocument* document,
                                       bool allow_transfer, bool rebind_same) {
    Runtime* runtime = dom_document_script_runtime(document);
    if (!runtime) return false;
    EvalContext* owner = runtime_get_eval_context(runtime);
    if (!owner || !runtime_heap(runtime) || !runtime_name_pool(runtime)) return false;
    owner->heap = runtime_heap(runtime);
    owner->name_pool = runtime_name_pool(runtime);
    owner->type_list = runtime_type_list(runtime);
    owner->pool = runtime_heap(runtime)->pool;
    // A nested event keeps its outer evaluator and cannot transfer live frames.
    if (!rebind_same) {
        // A newly bound iframe owner must refresh its worker-thread stack limit.
        if (context != owner && (!allow_transfer || !radiant_eval_context_switch(owner))) return false;
    } else if (context && context != owner) {
        if (!allow_transfer || !radiant_eval_context_switch(owner)) return false;
    } else {
        if (!eval_context_init(owner)) return false;
    }
    return radiant_bind_document_script_host(ui, document);
}

static bool loader_cleanup_owners(DomDocument* doc, RadiantDocumentCleanupOwners* owners) {
    Runtime* timer_runtime = doc->js.runtime ? doc->js.runtime : doc->lambda_runtime;
    Runtime* state_runtime = dom_document_script_runtime(doc);
    Runtime* loader_runtime = dom_document_loader_runtime(doc);
    // A partially retired evaluator cannot safely service document teardown.
    if ((timer_runtime && timer_runtime->cleanup_incomplete) ||
            (state_runtime && state_runtime->cleanup_incomplete) ||
            (loader_runtime && loader_runtime->cleanup_incomplete)) {
        log_error("free_document: document evaluator cleanup is incomplete; document retained");
        return false;
    }
    owners->timer = timer_runtime ? runtime_get_eval_context(timer_runtime) : nullptr;
    owners->state = state_runtime == timer_runtime ? owners->timer :
        (state_runtime ? runtime_get_eval_context(state_runtime) : nullptr);
    owners->loader = loader_runtime == state_runtime ? owners->state :
        (loader_runtime == timer_runtime ? owners->timer :
            (loader_runtime ? runtime_get_eval_context(loader_runtime) : nullptr));
    if ((timer_runtime && !owners->timer) || (state_runtime && !owners->state) ||
            (loader_runtime && !owners->loader)) {
        log_error("free_document: could not obtain document evaluator; document retained");
        return false;
    }
    if (owners->timer && owners->state && owners->timer != owners->state) {
        // One document cannot multiplex independent evaluators on one host thread.
        log_error("free_document: Lambda and JS runtimes have different EvalContexts; document retained");
        return false;
    }
    // The current owner cannot release the roots or stack executing this close.
    bool active = (owners->timer && owners->timer->execution_depth) ||
        (owners->state && owners->state->execution_depth) ||
        (owners->loader && owners->loader->execution_depth);
#ifndef LAMBDA_NO_TASKS
    active = active || ((owners->timer || owners->state || owners->loader) && activation_current());
#endif
    if (active) {
        log_error("free_document: document evaluator is active; document retained");
        return false;
    }
    return true;
}

static void loader_release_observers(DomDocument* document) {
    dom_observers_release_document(document);
}

static void loader_release_dom(DomDocument* document) {
    radiant_dom_invalidate_document(document);
    dom_events_release_document(document);
}

static void loader_cancel_timers(DomDocument* document, EvalContext* owner) {
    if (!owner || !js_runtime_state_for(owner)) return;
    // A JS batch owns its loop, so abandon its timers without re-entering it.
    if (script_runner_js_batch_cleanup_unsafe()) js_event_loop_abandon_document_timers(document);
    else js_event_loop_cancel_document_timers(document);
}

static bool loader_prepare_cleanup(EvalContext* timer, EvalContext* state) {
    // Timer and state roots must be accessed through the correct evaluator.
    return !(timer && js_runtime_state_for(timer) && !js_runtime_state_init(timer)) &&
        !(state && (!eval_context_matches(state) ||
            (js_runtime_state_for(state) && !js_runtime_state_init(state))));
}

static bool loader_bind_owner(EvalContext* owner) {
    return radiant_eval_context_switch(owner) &&
        (!js_runtime_state_for(owner) || js_runtime_state_init(owner));
}

static EvalContext* loader_runtime_owner(Runtime* runtime) { return runtime_get_eval_context(runtime); }
static bool loader_runtime_is_cleaning(Runtime* runtime) { return runtime && runtime->cleanup_in_progress; }
static void loader_detach_document(Runtime* runtime, DomDocument* document) {
    if (runtime && runtime->dom_doc == document) runtime->dom_doc = nullptr;
}
static void loader_cleanup_runtime(Runtime* runtime) {
    // The native document owns an opaque runtime until its complete teardown.
    runtime_cleanup(runtime);
    runtime->~Runtime();
    mem_free(runtime);
}

static RadiantRuntimeCleanupResult loader_try_cleanup_runtime(Runtime* runtime, bool wait, bool pinned) {
    RuntimeCleanupResult result = pinned
        ? (wait ? runtime_try_cleanup_retirement_pinned(runtime) : runtime_try_cleanup_retirement_pinned_nonblocking(runtime))
        : (wait ? runtime_try_cleanup(runtime) : runtime_try_cleanup_nonblocking(runtime));
    switch (result) {
    case RUNTIME_CLEANUP_COMPLETE: return RADIANT_RUNTIME_CLEANUP_COMPLETE;
    case RUNTIME_CLEANUP_RETRY: return RADIANT_RUNTIME_CLEANUP_RETRY;
    case RUNTIME_CLEANUP_INCOMPLETE: return RADIANT_RUNTIME_CLEANUP_INCOMPLETE;
    }
    return RADIANT_RUNTIME_CLEANUP_INCOMPLETE;
}

static int loader_release_document_tasks(DomDocument* document) {
    return lambda_document_tasks_release(document);
}

static bool loader_pending_native_work(EvalContext* owner, DomDocument* document) {
    return owner && lambda_document_tasks_have_pending_native_work(owner->scheduler, document);
}

static bool loader_cancel_document_tasks(EvalContext* owner, DomDocument* document) {
    if (!owner || lambda_scheduler_has_document_retirement_guards(owner->scheduler)) return false;
    return !owner->scheduler || lambda_document_tasks_cancel(owner->scheduler, document) >= 0;
}

static RadiantDocumentJoinProgress loader_progress_document_tasks(EvalContext* owner, DomDocument* document,
                                                                  LambdaDocumentLifetimeGuard* guard) {
    if (!owner) return RADIANT_DOCUMENT_JOIN_ERROR;
    if (!owner->scheduler) return RADIANT_DOCUMENT_JOIN_DONE;
    switch (lambda_document_tasks_progress(owner->scheduler, document, guard)) {
    case LAMBDA_DOCUMENT_JOIN_ERROR: return RADIANT_DOCUMENT_JOIN_ERROR;
    case LAMBDA_DOCUMENT_JOIN_DONE: return RADIANT_DOCUMENT_JOIN_DONE;
    case LAMBDA_DOCUMENT_JOIN_WAITING: return RADIANT_DOCUMENT_JOIN_WAITING;
    case LAMBDA_DOCUMENT_JOIN_ADVANCED: return RADIANT_DOCUMENT_JOIN_ADVANCED;
    }
    return RADIANT_DOCUMENT_JOIN_ERROR;
}

static bool loader_release_navigation_evaluator() {
    if (!context) return true;
    EvalContext* previous = context;
    if (previous->execution_depth != 0) {
        log_error("navigation-exec: document load attempted during active evaluation");
        return false;
    }
    // Link policy has returned, so a new document may claim the thread rather
    // than nesting a second Runtime under the source document's evaluator.
    if (js_runtime_state_thread_matches(previous) &&
        !js_runtime_state_shutdown(previous)) {
        log_error("navigation-exec: could not release source JS evaluator");
        return false;
    }
    if (input_context == (Context*)previous) input_context = nullptr;
    if (!eval_context_shutdown(previous)) {
        log_error("navigation-exec: could not release source evaluator");
        return false;
    }
    return true;
}

static bool loader_finish_navigation_worker() {
    bool released = loader_release_navigation_evaluator();
    // Worker-local input and side-stack state must not outlive its thread.
    input_context = nullptr;
    lambda_stack_detach_thread();
    return released;
}

static void* loader_running_document() { return dom_get_document(); }

static bool loader_register_event_hooks(RadiantEventEmitFn emit) {
    // Document-owned tasks follow the active Lambda DOM realm (S13.3.1v2).
    lambda_radiant_event_register(emit);
    lambda_set_document_probe(loader_running_document);
    return true;
}

bool lambda_loader_runtime_host_register() {
    static const RadiantLoaderRuntimeHostOps operations = {
        loader_runtime_get, loader_runtime_finish, release_layout_runtime,
        loader_begin_script, loader_cancel_script, loader_finish_script, loader_prepare_script,
        loader_render_embedded, lambda_document_transform_for_input_type,
        execute_document_scripts_profiled, collect_and_compile_event_handlers,
        loader_begin_document_timing, loader_end_document_timing, script_runner_cleanup_js_state,
        script_runner_defer_js_state_cleanup,
        loader_cleanup_owners, loader_release_observers, loader_release_dom, loader_cancel_timers,
        loader_prepare_cleanup, loader_bind_owner, radiant_eval_context_switch,
        loader_release_navigation_evaluator, loader_finish_navigation_worker, radiant_bind_document_script_host,
        loader_bind_event_document, loader_register_event_hooks,
        loader_runtime_owner, loader_runtime_is_cleaning, loader_detach_document,
        loader_cleanup_runtime, loader_try_cleanup_runtime,
        loader_release_document_tasks, loader_pending_native_work, runtime_document_retirement_pin,
        runtime_document_retirement_unpin, loader_cancel_document_tasks, loader_progress_document_tasks,
    };
    return radiant_loader_runtime_host_register(&operations);
}
