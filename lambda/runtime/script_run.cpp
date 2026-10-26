// script_run.cpp — tier-neutral entry points that run a Lambda script, a
// package module or a document transform. T0 runs a planned script directly;
// only the remaining whole-module branch needs MIR (D8.1.1v15), so a T0-only
// build (LAMBDA_NO_MIR, vibe/Lambda_Design_Mobile.md MB2) keeps this file.

#include "transpiler.hpp"
#include "ast_build.hpp"
#include "interp.hpp"
#include "runtime-state.h"
#include "recovery_frame.h"
#include "lambda-stack.h"
#include "concurrency.h"
#include "../js/js_runtime.h"
#include "../js/js_runtime_state.hpp"
#include "../../lib/log.h"
#include "../../lib/mem_factory.h"
#include "../../lib/url.h"
#include "../../lib/arraylist.h"
#include <string.h>

#ifndef _WIN32
// A recursive T0 call keeps its continuation in the native C++ evaluator
// stack. The normal CLI thread has an OS-sized stack that is sufficient for
// ordinary Lambda recursion but not for the deliberately deep `ack2` corpus
// row. Run the interpreter on a bounded worker stack so the existing recovery
// boundary can report a language fault instead of receiving a raw guard-page
// hit (D5.3.3, S7.4.3).
struct InterpWorkerArgs {
    Runner* runner;
    bool run_main;
};

static Item interp_worker_run_script(void* opaque) {
    InterpWorkerArgs* args = (InterpWorkerArgs*)opaque;
    if (!args || !args->runner || !args->runner->context) return ItemError;
    EvalContext* eval = args->runner->context;
    // The large-stack worker has independent JS TLS; bind the shared capsule
    // before an async bridge (for example toPromise) can allocate GC-owned JS
    // values, preserving the owner/thread invariant (D5.3.3).
    bool js_state_initialized = !js_runtime_state_for(eval) ||
        js_runtime_state_init(eval);
    if (!js_state_initialized) {
        log_error("interp-worker: failed to bind JavaScript runtime state");
        return ItemError;
    }
    Item result = interp_run_script(args->runner, args->run_main);
    if (js_state_initialized && js_runtime_state_for(eval) &&
            !js_runtime_state_shutdown(eval)) {
        log_error("interp-worker: failed to release JavaScript runtime state");
    }
    return result;
}

static Item interp_run_with_worker_stack(Runner* runner, bool run_main) {
    if (!runner || !runner->context) return ItemError;
    InterpWorkerArgs args = {runner, run_main};
    return interp_run_on_large_stack(runner->context, interp_worker_run_script, &args);
}

typedef struct InterpStackScan {
    String* functions[128];
    String* calls[256];
    int function_count;
    int call_count;
    bool has_handler;
} InterpStackScan;

static void interp_stack_scan_record(AstNode* node, InterpStackScan* scan) {
    if (!node || !scan) return;
    if (node->node_type == AST_NODE_HANDLER_EXPR ||
            node->node_type == AST_NODE_HANDLER_STAM) {
        scan->has_handler = true;
    } else if ((node->node_type == AST_NODE_FUNC ||
            node->node_type == AST_NODE_FUNC_EXPR ||
            node->node_type == AST_NODE_PROC ||
            node->node_type == AST_NODE_ARROW_FUNC ||
            node->node_type == AST_NODE_METHOD) && scan->function_count < 128) {
        String* name = ((AstFuncNode*)node)->name;
        if (name) scan->functions[scan->function_count++] = name;
    } else if (node->node_type == AST_NODE_CALL_EXPR && scan->call_count < 256) {
        AstNode* function = ast_unwrap_primary(((AstCallNode*)node)->function);
        if (function && function->node_type == AST_NODE_IDENT) {
            String* name = ((AstIdentNode*)function)->name;
            if (name) scan->calls[scan->call_count++] = name;
        }
    }
}

static void interp_stack_scan_node(AstNode* node, AstNode* parent, void* opaque) {
    (void)parent;
    InterpStackScan* scan = (InterpStackScan*)opaque;
    interp_stack_scan_record(node, scan);
    interp_visit_children(node, [](AstNode* child, void* ctx) {
        interp_stack_scan_node(child, NULL, ctx);
    }, opaque);
}

static bool interp_script_needs_large_stack(const Script* script) {
    if (!script || !script->ast_root) return false;
    InterpStackScan scan = {};
    interp_stack_scan_record(script->ast_root, &scan);
    interp_visit_children(script->ast_root, [](AstNode* child, void* ctx) {
        interp_stack_scan_node(child, NULL, ctx);
    }, &scan);
    bool has_recursive_call = false;
    for (int call_index = 0; call_index < scan.call_count && !has_recursive_call;
            call_index++) {
        for (int function_index = 0; function_index < scan.function_count;
                function_index++) {
            String* call_name = scan.calls[call_index];
            String* function_name = scan.functions[function_index];
            if (call_name && function_name && call_name->len == function_name->len &&
                    memcmp(call_name->chars, function_name->chars,
                        call_name->len) == 0) {
                has_recursive_call = true;
                break;
            }
        }
    }
    // Fault-catching recursive procedures intentionally rely on the ordinary
    // stack guard to publish S7.4.3; moving them to the large worker would let
    // ASan consume the expanded stack before the recovery landing runs.
    return has_recursive_call && !scan.has_handler;
}
#endif

Input* run_script_mir(Runtime *runtime, const char* source, char* script_path,
        bool run_main, Script** out_script) {
    if (out_script) *out_script = NULL;
    LambdaTier tier = lambda_tier_selected();
    log_debug("lambda-script: starting tier=%s backend=mir-direct",
        tier == LAMBDA_TIER_JIT ? "jit" :
        tier == LAMBDA_TIER_INTERP ? "interp" : "auto");

    // Initialize runner
    Runner runner;
    runner_init(runtime, &runner);

    // Enable MIR Direct mode for every module (main + all imports).
    // Each compile_script_as_mir_direct() call clears dynamic_imports, registers the module's
    // direct sub-imports' pub symbols, then does transpile_mir_ast() + MIR_link().
    // Imports are compiled depth-first, so by the time any module is linked its dependencies
    // are already in dynamic_imports.
    bool was_mir_direct = runtime->use_mir_direct;
    runtime->use_mir_direct = true;

    if (source) {
        runner.script = load_script(runtime, script_path, source, false);
    } else {
        runner.script = load_script(runtime, script_path, NULL, false);
    }

    runtime->use_mir_direct = was_mir_direct;  // restore for any nested/reentrant calls

    if (!runner.script || !runner.script->ast_root) {
        log_error("Failed to parse script");
        Pool* error_pool = mem_pool_create(NULL, MEM_ROLE_AST, "script.result");
        Input* output = Input::create(error_pool, nullptr);
        if (!output) {
            log_error("Failed to create error output Input");
            if (error_pool) pool_destroy(error_pool);
            return nullptr;
        }
        output->root = ItemError;
        return output;
    }
    if (out_script) *out_script = runner.script;

    // T0: a planned script has no `main` by construction — module init is the
    // interpreter's job. Imports still fall back to JIT in P0/P1, so a planned
    // main script with an import cone is not yet reachable (P1.5).
    if (runner.script->interp_supported) {
        runner_setup_context(&runner);
        if (!runner.context) return nullptr;
        Item result;
#ifndef _WIN32
        result = interp_script_needs_large_stack(runner.script)
            ? interp_run_with_worker_stack(&runner, run_main)
            : interp_run_script(&runner, run_main);
#else
        result = interp_run_script(&runner, run_main);
#endif
        if ((!runtime || !runtime->no_task_drain) && runner.context->scheduler) {
            // The JIT output path drains spawned tasks before its Runtime is
            // reused. T0 must do the same: a pending async satellite retains
            // its Script/AST pool, so batch heap/script teardown otherwise
            // leaves the next worker with a dangling task callback (D5.4.3).
            lambda_scheduler_drain(runner.context->scheduler);
        }
        preserve_context_last_error(result);
        Pool* output_pool = mem_pool_create(NULL, MEM_ROLE_AST, "script.result");
        Input* output = Input::create(output_pool, nullptr);
        if (!output) {
            log_error("interp: failed to create output Input");
            if (output_pool) pool_destroy(output_pool);
            return nullptr;
        }
        resolve_sys_paths_recursive(result);
        if (runner.context->cwd) {
            url_destroy((Url*)runner.context->cwd);
            runner.context->cwd = NULL;
        }
        output->root = result;
        return output;
    }

    if (!runner.script->main_func) {
#ifdef LAMBDA_NO_MIR
        // a T0-only build compiles no native main, so an unplanned script ends here
        log_error("interp-profile: '%s' was not planned for T0 and MIR is excluded",
            script_path);
#else
        log_error("MIR Direct: 'main' missing after compilation of '%s'", script_path);
#endif
        Pool* error_pool = mem_pool_create(NULL, MEM_ROLE_AST, "script.result");
        Input* output = Input::create(error_pool, nullptr);
        if (output) output->root = ItemError;
        return output;
    }

#ifndef LAMBDA_NO_MIR
    ArrayList* import_cone = collect_import_cone(runner.script);
    if (import_cone && import_cone->length > 0) {
        // Set up context for module initialization
        runner_setup_context(&runner);
        if (!runner.context) return nullptr;
        RuntimeExecutionScope execution_scope(runner.context);
        // Imported entries share this Context but must run only module init.
        runner.context->run_main = false;

        // Instantiate every module's fixed context-owned slabs before any
        // initialization code reads or writes a module binding.
        for (int i = 0; i < import_cone->length; i++) {
            Script* imp_script = (Script*)import_cone->data[i];
            if (imp_script && imp_script->jit_context) {
                if (!prepare_context_module_state((void*)imp_script->jit_context,
                        imp_script->const_list ? imp_script->const_list->data : nullptr,
                        imp_script->type_list)) return nullptr;
            }
        }
        if (!prepare_context_module_state((void*)runner.script->jit_context,
                runner.script->const_list ? runner.script->const_list->data : nullptr,
                runner.script->type_list)) return nullptr;

        RuntimeJitModuleStateScope module_scope(runner.context);

        Item result = ItemError;
        LambdaRecoveryFrame* recovery_frame = lambda_recovery_frame_begin_for(
            (Context*)runner.context, LAMBDA_RECOVERY_CAP_EXECUTION_BOUNDARY);
        if (!recovery_frame) {
            log_error("mir-cache: failed to allocate recovery frame");
            result = runtime_publish_result(runner.context,
                lambda_recovery_publish_fault_item((Context*)runner.context,
                    LAMBDA_FAULT_OUT_OF_MEMORY, ERR_OK));
        } else if (LAMBDA_RECOVERY_FRAME_SETJMP(recovery_frame)) {
            Item recovered = ItemError;
            if (!lambda_recovery_frame_restore_landing(recovery_frame)) {
                log_error("mir-cache: recovery frame landing invariant failed");
                recovered = lambda_recovery_publish_fault_item((Context*)runner.context,
                    LAMBDA_FAULT_RUNTIME_BOUNDARY_DEFECT, ERR_OK);
            } else {
                recovered = lambda_recovery_frame_fault_item((Context*)runner.context,
                    recovery_frame);
            }
            _lambda_stack_overflow_flag = false;
            lambda_recovery_frame_end(recovery_frame);
            result = runtime_publish_result(runner.context, recovered);
        } else {
            if (!lambda_recovery_frame_arm(recovery_frame)) {
                log_error("mir-cache: failed to arm recovery frame");
                lambda_recovery_frame_end(recovery_frame);
                result = runtime_publish_result(runner.context,
                    lambda_recovery_publish_fault_item((Context*)runner.context,
                        LAMBDA_FAULT_RUNTIME_BOUNDARY_DEFECT, ERR_OK));
            } else {
                // Module initialization is transactional. A fault first lands
                // in this barrier, resets partial module slabs, and then
                // forwards to the still-armed outer execution boundary.
                bool module_init_failed = false;
                for (int i = 0; i < import_cone->length; i++) {
                    Script* imp_script = (Script*)import_cone->data[i];
                    if (!imp_script || !imp_script->main_func) continue;
                    LambdaRecoveryFrame* module_frame = lambda_recovery_frame_begin_for(
                        (Context*)runner.context,
                        LAMBDA_RECOVERY_CAP_TRANSACTION_BARRIER);
                    if (!module_frame) {
                        log_error("mir-cache: failed to allocate module transaction frame");
                        if (!lambda_recovery_frame_raise_fault(
                                LAMBDA_FAULT_OUT_OF_MEMORY, ERR_OK)) {
                            result = runtime_publish_result(runner.context,
                                lambda_recovery_publish_fault_item((Context*)runner.context,
                                    LAMBDA_FAULT_OUT_OF_MEMORY, ERR_OK));
                            module_init_failed = true;
                        }
                        break;
                    }
                    if (LAMBDA_RECOVERY_FRAME_SETJMP(module_frame)) {
                        LambdaFaultReason reason = LAMBDA_FAULT_RUNTIME_BOUNDARY_DEFECT;
                        LambdaErrorCode prior_error_code = ERR_OK;
                        if (!lambda_recovery_frame_restore_landing(module_frame)) {
                            log_error("mir-cache: module transaction landing invariant failed");
                        } else {
                            reason = module_frame->fault.reason;
                            prior_error_code = module_frame->fault.prior_error_code;
                        }
                        lambda_recovery_frame_end(module_frame);
                        // An abandoned initializer may have registered roots or
                        // mutated module bindings, so a local handler must not resume it.
                        lambda_module_state_reset();
                        if (!lambda_recovery_frame_raise_fault(reason, prior_error_code)) {
                            result = runtime_publish_result(runner.context,
                                lambda_recovery_publish_fault_item((Context*)runner.context,
                                    reason, prior_error_code));
                            module_init_failed = true;
                        }
                        break;
                    }
                    if (!lambda_recovery_frame_arm(module_frame)) {
                        log_error("mir-cache: failed to arm module transaction frame");
                        lambda_recovery_frame_end(module_frame);
                        if (!lambda_recovery_frame_raise_fault(
                                LAMBDA_FAULT_RUNTIME_BOUNDARY_DEFECT, ERR_OK)) {
                            result = runtime_publish_result(runner.context,
                                lambda_recovery_publish_fault_item((Context*)runner.context,
                                    LAMBDA_FAULT_RUNTIME_BOUNDARY_DEFECT, ERR_OK));
                            module_init_failed = true;
                        }
                        break;
                    }
                    log_info("mir cache: running imported module main index=%d", imp_script->index);
                    (void)module_scope.activate(imp_script->module_state_id);
                    runner.context->consts = imp_script->const_list
                        ? imp_script->const_list->data : nullptr;
                    runner.context->type_list = imp_script->type_list;
                    Item module_result = imp_script->main_func(runner.context);
                    imp_script->module_initialized = !item_is_error(module_result);
                    lambda_recovery_frame_end(module_frame);
                    // D7.2.2: a module whose init ends in an ordinary error
                    // never becomes importable, so its importer does not run,
                    // as on T0 (interp_execute). The result used to be dropped
                    // and main ran against half-set module slots (LR07-17).
                    if (item_is_error(module_result)) {
                        result = runtime_publish_result(runner.context, module_result);
                        module_init_failed = true;
                        break;
                    }
                }
                // Restore the main module only after every import barrier has
                // retired normally; otherwise the outer landing has run.
                runner.context->consts = runner.script->const_list
                    ? runner.script->const_list->data : nullptr;
                runner.context->type_list = runner.script->type_list;
                log_debug("Executing JIT compiled code...");
                runner.context->run_main = run_main;
                if (!module_init_failed) {
                    (void)module_scope.activate(runner.script->module_state_id);
                    result = runtime_publish_result(runner.context,
                        runner.script->main_func(runner.context));
                    runner.script->module_initialized = !item_is_error(result);
                }
                lambda_recovery_frame_end(recovery_frame);
            }
        }
        preserve_context_last_error(result);

        // Create output
        Pool* output_pool = mem_pool_create(NULL, MEM_ROLE_AST, "script.result");
        Input* output = Input::create(output_pool, nullptr);
        if (!output) {
            log_error("Failed to create output Input");
            if (output_pool) pool_destroy(output_pool);
            if (runner.context->cwd) {
                url_destroy((Url*)runner.context->cwd);
                runner.context->cwd = NULL;
            }
            arraylist_free(import_cone);
            return nullptr;
        }
        resolve_sys_paths_recursive(result);
        if (runner.context->cwd) {
            url_destroy((Url*)runner.context->cwd);
            runner.context->cwd = NULL;
        }

        // Return result directly on the GC heap — no deep_copy needed.
        // With GC-managed memory the heap is retained across the session;
        // the caller is responsible for calling runtime_cleanup() when done.
        output->root = result;
        arraylist_free(import_cone);
        return output;
    }
    if (import_cone) arraylist_free(import_cone);

    // Execute (no imports — use standard path)
    Input* output = execute_script_and_create_output(&runner, run_main);

    return output;
#else
    return nullptr;  // unreachable: main_func is never set without MIR
#endif
}

// Document loaders select this fixed native contract instead of generated code.
static const LambdaDocumentTransformConfig lambda_document_transforms[] = {
    {"pdf", "lambda.doc.pdf.pdf", "pdf_to_document", LAMBDA_DOCUMENT_TRANSFORM_SOURCE_PARSED, true},
    {"eps", "lambda.doc.eps.eps", "eps_to_svg", LAMBDA_DOCUMENT_TRANSFORM_SOURCE_PARSED, true},
    // the package parses its own source so the engine sees the adapter registry (D7.2.7)
    {"latex", "lambda.doc.latex.latex", "render_document_file", LAMBDA_DOCUMENT_TRANSFORM_SOURCE_PATH, true},
    {"docx", "lambda.doc.docx.view", "render_document_file", LAMBDA_DOCUMENT_TRANSFORM_SOURCE_PATH, true},
    {"psd", "lambda.doc.image", "render_document_file", LAMBDA_DOCUMENT_TRANSFORM_SOURCE_PATH, true},
    // Reader controls and publication resources retain a runtime per document (S12.1.3).
    {"ebook", "lambda.doc.ebook.view", "render_document_file", LAMBDA_DOCUMENT_TRANSFORM_SOURCE_PATH, false},
    {"diff", "lambda.doc.diff.view", "render_document_file", LAMBDA_DOCUMENT_TRANSFORM_SOURCE_PATH, false},
    {"docbook", "lambda.doc.docbook.view", "render_document_file", LAMBDA_DOCUMENT_TRANSFORM_SOURCE_PATH, true},
    {"tikz", "lambda.doc.tikz.tikz", "render_document", LAMBDA_DOCUMENT_TRANSFORM_SOURCE_PARSED, true},
    {"graph", "lambda.graph.document", "to_html", LAMBDA_DOCUMENT_TRANSFORM_SOURCE_PARSED, true},
    {"math", "lambda.doc.math.math", "render_math", LAMBDA_DOCUMENT_TRANSFORM_SOURCE_PARSED, true},
    // the presenter keeps playback state and handlers, so it owns a runtime per document
    {"slides", "lambda.slide.present", "present", LAMBDA_DOCUMENT_TRANSFORM_SOURCE_PARSED, false},
    // edit mode selects the application; lambda.edit's registry selects the
    // format adapter and reads the source itself (Radiant_Design_Edit_Mode §4).
    {"edit", "lambda.edit.edit", "open_document", LAMBDA_DOCUMENT_TRANSFORM_SOURCE_PATH, false},
};

const LambdaDocumentTransformConfig* lambda_document_transform_for_input_type(
        const char* input_type) {
    if (!input_type) return NULL;
    for (size_t index = 0;
            index < sizeof(lambda_document_transforms) / sizeof(lambda_document_transforms[0]);
            index++) {
        const LambdaDocumentTransformConfig* transform =
            &lambda_document_transforms[index];
        if (strcmp(transform->input_type, input_type) == 0) return transform;
    }
    return NULL;
}

Input* run_lambda_package_module(Runtime* runtime, const char* package_module,
        Script** out_package) {
    if (out_package) *out_package = NULL;
    if (!runtime || !package_module || !package_module[0]) {
        log_error("package-loader: incomplete package configuration");
        return NULL;
    }
    char* package_path = lambda_resolve_import_module_path("./",
        strview_from_cstr(package_module));
    if (!package_path) {
        log_error("package-loader: could not resolve package '%s'", package_module);
        return NULL;
    }
    Input* output = run_script_mir(runtime, NULL, package_path, false, out_package);
    mem_free(package_path);
    return output;
}

Input* run_lambda_document_transform_with_options(Runtime* runtime,
        const char* input_target, const LambdaDocumentTransformConfig* transform,
        const LambdaDocumentTransformOption* options, int option_count) {
    if (!runtime || !input_target || !transform || !transform->input_type ||
            !transform->package_module || !transform->function_name || option_count < 0 ||
            (option_count > 0 && !options)) {
        log_error("document-transform: incomplete transform configuration");
        return NULL;
    }
    Script* package = NULL;
    Input* output = run_lambda_package_module(runtime, transform->package_module, &package);
    if (!output || !package) return output;

    EvalContext* eval_context = runtime_get_eval_context(runtime);
    if (!eval_context) {
        log_error("document-transform: package '%s' did not create an evaluation context",
            transform->package_module);
        output->root = ItemError;
        return output;
    }
    RuntimeExecutionScope execution_scope(eval_context);
    RootFrame roots(7);
    Rooted<Item> target(roots, (Item){.item = s2it(heap_strcpy(input_target,
        (int64_t)strlen(input_target)))});
    Rooted<Item> type(roots, (Item){.item = s2it(heap_strcpy(transform->input_type,
        (int64_t)strlen(transform->input_type)))});
    // A path-source export owns its read, so native code parses nothing here.
    Rooted<Item> document(roots,
        transform->source == LAMBDA_DOCUMENT_TRANSFORM_SOURCE_PATH
            ? target.get() : fn_input2(target.get(), type.get()));
    Rooted<Item> option_map(roots, ItemNull);
    Rooted<Item> option_name(roots, ItemNull);
    Rooted<Item> option_value(roots, ItemNull);
    Rooted<Item> result(roots, ItemNull);
    if (item_is_error(document.get())) {
        output->root = document.get();
        return output;
    }
    if (option_count > 0 || transform->source == LAMBDA_DOCUMENT_TRANSFORM_SOURCE_PARSED) {
        option_map.set(vmap_new());
        if (get_type_id(option_map.get()) != LMD_TYPE_VMAP) {
            log_error("document-transform: could not create transform options");
            output->root = ItemError;
            return output;
        }
        for (int index = 0; index < option_count; index++) {
            const LambdaDocumentTransformOption* option = &options[index];
            if (!option->name || !option->name[0] ||
                    (option->kind != LAMBDA_DOCUMENT_TRANSFORM_OPTION_STRING &&
                     option->kind != LAMBDA_DOCUMENT_TRANSFORM_OPTION_BOOL) ||
                    (option->kind == LAMBDA_DOCUMENT_TRANSFORM_OPTION_STRING &&
                     !option->string_value)) {
                log_error("document-transform: invalid transform option at index %d", index);
                output->root = ItemError;
                return output;
            }
            option_name.set((Item){.item = s2it(heap_strcpy(option->name,
                (int64_t)strlen(option->name)))});
            option_value.set(option->kind == LAMBDA_DOCUMENT_TRANSFORM_OPTION_BOOL
                ? (Item){.item = b2it(option->bool_value ? 1 : 0)}
                : (Item){.item = s2it(heap_strcpy(option->string_value,
                    (int64_t)strlen(option->string_value)))});
            if (item_is_error(vmap_set(option_map.get(), option_name.get(),
                    option_value.get()))) {
                log_error("document-transform: could not set transform option '%s'", option->name);
                output->root = ItemError;
                return output;
            }
        }
        if (transform->source == LAMBDA_DOCUMENT_TRANSFORM_SOURCE_PARSED) {
            // parsed document adapters need the original path to resolve local resources.
            option_name.set((Item){.item = s2it(heap_strcpy("source_path", 11))});
            option_value.set(target.get());
            if (item_is_error(vmap_set(option_map.get(), option_name.get(),
                    option_value.get()))) {
                log_error("document-transform: could not set source_path");
                output->root = ItemError;
                return output;
            }
        }
    }
    Item args[2] = {document.get(), option_map.get()};
    result.set(interp_call_module_export(runtime, package, transform->function_name, args, 2));
    output->root = result.get();
    return output;
}
