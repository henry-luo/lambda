#include "mvp.hpp"
#include "../interp.hpp"
#include "../mir_emitter_shared.hpp"
#include "../mir_policy.hpp"
#include "../module_registry.h"
#include "../template_registry.h"
#include "../type_contract.hpp"
#include "../ast_build.hpp"
#include "../../js/js_runtime.h"
#include "../../js/js_runtime_state.hpp"
#include "lib/log.h"
#include "lib/memtrack.h"
#include "lib/mem_factory.h"
#include <mir-gen.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif

extern __thread EvalContext* context;
extern int g_js_force_document_interp;
extern "C" {
    void* import_resolver(const char* name);
    extern int g_mir_interp_mode;
    int lambda_mir_lazy_enabled(void);
}

// MIR's native generator initializes mutable process-global target patterns.
// Keep private satellite contexts isolated, but serialize their native passes.
static pthread_mutex_t g_mir_native_codegen_mutex = PTHREAD_MUTEX_INITIALIZER;

class MirNativeCodegenLock {
    bool locked_;

public:
    explicit MirNativeCodegenLock(bool native_codegen) : locked_(native_codegen) {
        if (locked_) pthread_mutex_lock(&g_mir_native_codegen_mutex);
    }

    ~MirNativeCodegenLock() {
        if (locked_) pthread_mutex_unlock(&g_mir_native_codegen_mutex);
    }

    MirNativeCodegenLock(const MirNativeCodegenLock&) = delete;
    MirNativeCodegenLock& operator=(const MirNativeCodegenLock&) = delete;
};

static int lambda_const_fold_compiler_pass(void* opaque) {
    Transpiler* tp = (Transpiler*)opaque;
    // const values are materialized before the folder runs, so the folder only
    // ever sees expressions (RC15).
    if (!lambda_const_materialize_script(tp)) return 0;
    return interp_const_fold_script(tp) ? 1 : 0;
}

typedef struct LambdaMirPipelinePassContext {
    MIR_context_t ctx;
    Transpiler* tp;
    ArrayList** property_keys;
    uint64_t* link_instruction_count;
    uint64_t* link_largest_function_instruction_count;
    uint64_t mir_module_count;
    uint64_t mir_function_count;
    uint64_t mir_instruction_count;
    uint64_t mir_largest_function_instruction_count;
    LambdaMvpModule* mvp;
} LambdaMirPipelinePassContext;

static int lambda_mir_plan_compiler_pass(void* opaque) {
    LambdaMirPipelinePassContext* pass = (LambdaMirPipelinePassContext*)opaque;
    if (!pass || !pass->ctx || !pass->tp || !pass->tp->ast_root) return 0;
    pass->mvp = lambda_mvp_create(pass->tp, pass->ctx);
    return lambda_mvp_plan(pass->mvp);
}

static int lambda_mir_lower_compiler_pass(void* opaque) {
    LambdaMirPipelinePassContext* pass = (LambdaMirPipelinePassContext*)opaque;
    return pass && pass->mvp && lambda_mvp_lower(pass->mvp);
}

static int lambda_mir_finalize_compiler_pass(void* opaque) {
    LambdaMirPipelinePassContext* pass = (LambdaMirPipelinePassContext*)opaque;
    if (!pass || !pass->mvp) return 0;
    LambdaMvpStats stats = {};
    if (!lambda_mvp_finalize(pass->mvp, &stats, pass->property_keys)) return 0;
    pass->mir_module_count = 1;
    pass->mir_function_count = stats.functions;
    pass->mir_instruction_count = stats.instructions;
    pass->mir_largest_function_instruction_count = stats.largest_function;
    if (pass->link_instruction_count) *pass->link_instruction_count = stats.instructions;
    if (pass->link_largest_function_instruction_count)
        *pass->link_largest_function_instruction_count = stats.largest_function;
    return 1;
}

// Each satellite owns distinct BSS symbols in its private MIR context. `find_import` is context-wide (not module-qualified), so using
// the eager module's `_mod_*` names would bind a later function to the first
// satellite's image after a relink. Stable names make every satellite retain
// its own immutable layout while all of them address the same EvalContext slab.
static bool make_satellite_module_names(NamePool* name_pool, uint32_t sequence,
        MirModuleNames* out) {
    if (!name_pool || !out) return false;
    char module[96];
    char consts[96];
    char layout[96];
    char types[96];
    char keys[96];
    snprintf(module, sizeof(module), "lambda_sat_%u", sequence);
    snprintf(consts, sizeof(consts), "_sat_%u_consts", sequence);
    snprintf(layout, sizeof(layout), "_sat_%u_layout", sequence);
    snprintf(types, sizeof(types), "_sat_%u_type_list", sequence);
    snprintf(keys, sizeof(keys), "_sat_%u_property_specs", sequence);
    String* module_name = name_pool_create_len(name_pool, module, strlen(module));
    String* consts_name = name_pool_create_len(name_pool, consts, strlen(consts));
    String* layout_name = name_pool_create_len(name_pool, layout, strlen(layout));
    String* types_name = name_pool_create_len(name_pool, types, strlen(types));
    String* keys_name = name_pool_create_len(name_pool, keys, strlen(keys));
    if (!module_name || !consts_name || !layout_name || !types_name || !keys_name) return false;
    out->module = module_name->chars;
    out->consts_bss = consts_name->chars;
    out->layout_bss = layout_name->chars;
    out->type_list_bss = types_name->chars;
    out->property_specs_bss = keys_name->chars;
    return true;
}

static void register_cross_lang_pub_fns(Runtime* runtime, AstImportNode* imp);

// Encode one satellite's local key table before appending it to the owner's
// context image. The MIR emitter indexes this exact order (D5.6.1).
static bool build_property_key_image(ArrayList* property_keys,
        PropertyKeySpec* specs, uint32_t capacity, uint32_t* out_bytes) {
    uint32_t count = property_keys ? (uint32_t)property_keys->length : 0;
    uint64_t header = (uint64_t)count * sizeof(PropertyKeySpec);
    if (!specs || !out_bytes || header > capacity || header > UINT32_MAX) return false;
    uint32_t bytes_size = (uint32_t)header;
    memset(specs, 0, capacity);
    for (uint32_t index = 0; index < count; index++) {
        MirPropertyKeyEntry* entry = (MirPropertyKeyEntry*)arraylist_get(
            property_keys, (int)index);
        if (!entry || !entry->name) return false;
        PropertyKeySpec* spec = &specs[index];
        spec->predefined_id = entry->predefined_id;
        if (entry->predefined_id == NAME_ID_NONE) {
            uint64_t need = (uint64_t)entry->name->len + 1;
            if (need > UINT32_MAX - bytes_size ||
                    bytes_size + need > capacity) return false;
            spec->name_offset = bytes_size;
            spec->name_length = entry->name->len;
            memcpy((uint8_t*)specs + bytes_size, entry->name->chars,
                entry->name->len);
            bytes_size += entry->name->len;
            ((uint8_t*)specs)[bytes_size++] = '\0';
        }
    }
    *out_bytes = bytes_size;
    return true;
}

// D8.3.3: the module-level definitions reachable from `target` through
// direct calls that a satellite may define beside it. Already compiled or
// pinned definitions keep their own entries (calls to them stay dynamic), as
// do unsupported bodies and anything that is not a module-level function of
// this script (imports, nested definitions, methods).
typedef struct MirSatelliteClusterScan {
    AstScript* root;
    Script* script;
    AstFuncNode** members;
    int count;
    int capacity;
} MirSatelliteClusterScan;

static bool mir_satellite_module_definition(AstScript* root, AstFuncNode* def) {
    if (!root || !def || !def->analysis) return false;
    NameEntry* decl = def->analysis->decl_entry;
    if (!decl || decl->node != (AstNode*)def ||
            decl->binding_storage != BINDING_STORAGE_MODULE) return false;
    for (NameEntry* entry = root->global_vars ? root->global_vars->first : NULL;
            entry; entry = entry->next) {
        if (entry == decl) return true;
    }
    return false;
}

static void mir_satellite_cluster_visit(AstNode* node, void* opaque) {
    MirSatelliteClusterScan* scan = (MirSatelliteClusterScan*)opaque;
    if (!node) return;
    switch (node->node_type) {
    case AST_NODE_FUNC: case AST_NODE_PROC: case AST_NODE_FUNC_EXPR:
    case AST_NODE_ARROW_FUNC:
        return;   // a nested definition's calls are its own
    case AST_NODE_CALL_EXPR: {
        AstFuncNode* direct = ast_direct_call_function((AstCallNode*)node);
        bool supported = direct && mir_satellite_module_definition(scan->root, direct) &&
            interp_satellite_supported(scan->script, direct);
        if (direct && scan->count < scan->capacity &&
                (((AstNode*)direct)->node_type == AST_NODE_FUNC ||
                 ((AstNode*)direct)->node_type == AST_NODE_PROC) &&
                mir_satellite_module_definition(scan->root, direct) &&
                interp_promotion_cell(scan->script, direct) &&
                interp_promotion_cell(scan->script, direct)->state == FN_PROMOTION_INTERP &&
                supported) {
            bool seen = false;
            for (int i = 0; i < scan->count && !seen; i++) seen = scan->members[i] == direct;
            const char* skip = getenv("LAMBDA_SATELLITE_CLUSTER_SKIP");
            if (!seen && skip && direct->name) {
                const char* hit = strstr(skip, direct->name->chars);
                size_t len = direct->name->len;
                while (hit && !((hit == skip || hit[-1] == ',') &&
                        (hit[len] == '\0' || hit[len] == ','))) {
                    hit = strstr(hit + 1, direct->name->chars);
                }
                if (hit) seen = true;
            }
            if (!seen) scan->members[scan->count++] = direct;
        } else if (direct) {
            log_debug("interp-tier: satellite cluster skips function='%s' module_def=%d state=%d supported=%d",
                direct->name ? direct->name->chars : "<anonymous>",
                (int)mir_satellite_module_definition(scan->root, direct),
                interp_promotion_cell(scan->script, direct)
                    ? (int)interp_promotion_cell(scan->script, direct)->state : -1,
                (int)supported);
        }
        break;
    }
    default:
        break;
    }
    interp_visit_children(node, mir_satellite_cluster_visit, opaque);
}

static int mir_satellite_collect_cluster(Script* script, AstScript* root, AstFuncNode* target,
        AstFuncNode** members, int capacity) {
    // diagnostics: LAMBDA_SATELLITE_CLUSTER_CAP=N bounds the cluster (1 = the
    // old one-definition image); LAMBDA_SATELLITE_CLUSTER_SKIP=a,b keeps the
    // named definitions out of it (they stay dynamic targets)
    const char* cap_env = getenv("LAMBDA_SATELLITE_CLUSTER_CAP");
    if (cap_env && atoi(cap_env) > 0 && atoi(cap_env) < capacity) capacity = atoi(cap_env);
    MirSatelliteClusterScan scan = {root, script, members, 0, capacity};
    members[scan.count++] = target;
    // worklist: members appended while scanning are scanned in turn
    for (int i = 0; i < scan.count; i++) {
        if (members[i]->body) mir_satellite_cluster_visit(members[i]->body, &scan);
    }
    // keep source order stable for generated names and diagnostic receipts.
    for (int i = 1; i < scan.count; i++) {
        AstFuncNode* key = members[i];
        int j = i - 1;
        while (j >= 0 && members[j]->source_span.start_byte > key->source_span.start_byte) {
            members[j + 1] = members[j];
            j--;
        }
        members[j + 1] = key;
    }
    return scan.count;
}

static bool finalize_module_property_key_specs(MIR_item_t property_specs_bss,
        LambdaModuleLayout* layout, ArrayList* property_keys);

static bool mvp_compile_function_image(Runtime* runtime, Script* script,
        const AstFuncNode* fn, uint32_t sequence, bool snapshot,
        bool private_context,
        InterpSatelliteCancelProbe cancel_probe, void* cancel_context,
        InterpSatelliteImage** out_image) {
    if (out_image) *out_image = NULL;
    if (!runtime || !script || !fn || !fn->analysis || !out_image ||
            !interp_satellite_supported(script, fn) ||
            (cancel_probe && cancel_probe(cancel_context))) {
        return false;
    }
    // `--mir-interp` runs MIR's own interpreter: jit_init skips MIR_gen_init,
    // so this context has no generator to link against or to finish.
    bool satellite_interp = g_mir_interp_mode != 0;
    MirNativeCodegenLock codegen_lock(!satellite_interp);
    // An evaluator-built satellite owns a separate executable image just as a
    // worker image does.  That ownership is independent from snapshotting:
    // only a worker needs cloned compiler state and a one-definition image.
    MIR_context_t satellite_context = private_context ? NULL : script->jit_context;
    bool context_owned = false;
    Pool* compile_pool = script->pool;
    NamePool* compile_name_pool = script->name_pool;
    ArrayList* compile_const_list = script->const_list;
    ArrayList* compile_type_list = script->type_list;
    Pool* snapshot_pool = NULL;
    NamePool* snapshot_name_pool = NULL;
    ArrayList* snapshot_const_list = NULL;
    ArrayList* snapshot_type_list = NULL;
    int source_const_count = script->const_list ? script->const_list->length : 0;
    int source_type_count = script->type_list ? script->type_list->length : 0;
    MirModuleNames names = {};
    AstFuncNode* members[INTERP_SATELLITE_CLUSTER_CAP] = {};
    int member_count = 0;
    AstFuncNode* copies = NULL;
    AstFuncNode* compiled_target = NULL;
    AstScript satellite_root = {};
    MirModuleArtifacts artifacts = {};
    ArrayList* property_keys = NULL;
    Transpiler mvp_tp = {};
    InterpSatelliteImage* image = NULL;
    void* entry = NULL;
#ifndef NDEBUG
    struct timespec image_t0 = {};
    struct timespec image_t1 = {};
#endif
    AstScript* source_root = NULL;
    StrBuf* entry_name = NULL;
#ifndef NDEBUG
    double image_ms = 0.0;
#endif
    LambdaModuleLayout* layout = NULL;
    ArrayList* image_consts = NULL;
    if (snapshot) {
        // Worker images never extend the Script's compiler-owned pools. A
        // rejected fallback allocation leaves the hot function in T0.
        if (script->cache_cross_lang_tainted) return false;
        snapshot_pool = mem_pool_create(NULL, MEM_ROLE_CODE, "interp.satellite.snapshot");
        // The snapshot owns compiler-generated symbols only. Source AST names
        // remain owned by the Script, so borrowing its pool would let worker
        // teardown release evaluator-owned storage (D8.3.4).
        snapshot_name_pool = snapshot_pool ? name_pool_create(snapshot_pool, NULL) : NULL;
        snapshot_const_list = arraylist_new(source_const_count > 0 ? source_const_count : 1);
        snapshot_type_list = arraylist_new(source_type_count > 0 ? source_type_count : 1);
        if (!snapshot_pool || !snapshot_name_pool || !snapshot_const_list ||
                !snapshot_type_list) goto fail;
        for (int index = 0; index < source_const_count; index++) {
            if (!arraylist_append(snapshot_const_list, script->const_list->data[index])) goto fail;
        }
        for (int index = 0; index < source_type_count; index++) {
            if (!arraylist_append(snapshot_type_list, script->type_list->data[index])) goto fail;
        }
        compile_pool = snapshot_pool;
        compile_name_pool = snapshot_name_pool;
        compile_const_list = snapshot_const_list;
        // Snapshot lowering cannot publish pool-owned descriptors into the
        // Script registry that concurrent workers later read (D8.3.4).
        compile_type_list = snapshot_type_list;
        satellite_context = jit_init(runtime->optimize_level);
        context_owned = true;
    }
    if (cancel_probe && cancel_probe(cancel_context)) goto fail;
    if (!satellite_context) {
        satellite_context = jit_init(runtime->optimize_level);
        // Must mirror jit_init's own branch. Claiming a generator that was
        // never initialized made teardown call MIR_gen_finish on a garbage
        // gen_ctx and segfault in finish_data_flow.
        context_owned = true;
        if (!satellite_context) {
            log_error("interp-tier: could not create satellite MIR context");
            goto fail;
        }
    }

    if (!make_satellite_module_names(compile_name_pool, sequence, &names)) {
        log_error("interp-tier: could not allocate satellite symbol names");
        goto fail;
    }
    if (cancel_probe && cancel_probe(cancel_context)) goto fail;

    // `AstFuncNode::next` is the module declaration chain. The temporary root
    // must contain exactly the compiled definitions or a promotion would
    // accidentally lower the rest of the module again. D8.3.3: those are
    // the target plus its direct-callee CLUSTER (module-level, supported, not
    // yet compiled or pinned), so the calls among them are direct native
    // edges with the eager tier's call-site inference instead of boxed
    // dynamic dispatch through every callee's wrapper (richards/deltablue
    // 1.2-1.4x, diviter 66x on the auto tier).
    source_root = (AstScript*)script->ast_root;
    // A worker image starts with one definition. Collecting a cluster reads
    // other promotion cells, which belong to the evaluator-owned overlay for
    // cached ASTs; retaining that graph expansion for the publication phase
    // keeps the worker snapshot completely read-only.
    if (snapshot) {
        members[0] = (AstFuncNode*)fn;
        member_count = 1;
    } else {
        member_count = mir_satellite_collect_cluster(script, source_root, (AstFuncNode*)fn,
            members, INTERP_SATELLITE_CLUSTER_CAP);
    }
    copies = (AstFuncNode*)mem_calloc((size_t)member_count,
        sizeof(AstFuncNode), MEM_CAT_EVAL);
    if (!copies) goto fail;
    for (int i = 0; i < member_count; i++) {
        copies[i] = *members[i];
        copies[i].next = i + 1 < member_count ? (AstNode*)&copies[i + 1] : NULL;
        if (members[i] == fn) compiled_target = &copies[i];

    }
    if (!compiled_target) goto fail;
    if (cancel_probe && cancel_probe(cancel_context)) goto fail;
    satellite_root.node_type = AST_SCRIPT;
    satellite_root.child = (AstNode*)&copies[0];
    satellite_root.global_vars = source_root ? source_root->global_vars : NULL;

    // A satellite links its own MIR module directly, so it must publish the
    // hosted JS export pointers that the ordinary module compiler registers
    // before MIR_link (D9.3.2).
    for (AstNode* child = source_root ? source_root->child : NULL;
            child; child = child->next) {
        if (child->node_type != AST_NODE_IMPORT) continue;
        AstImportNode* imp = (AstImportNode*)child;
        // A queued snapshot belongs to a worker and cannot mutate the shared
        // JS-export resolver.  A synchronous private image is built on the
        // evaluator, however, and must publish the same boxed import symbols
        // as the former shared-context path before it links (D9.3.2).
        if ((!snapshot || !cancel_probe) && imp->is_cross_lang && lambda_import_is_js(imp)) {
            register_cross_lang_pub_fns(runtime, imp);
        }
    }

#ifndef NDEBUG
    clock_gettime(CLOCK_MONOTONIC, &image_t0);
#endif
    // MVP keeps ABI and binding plans private; retained FnAnalysis is read-only.
    memcpy(&mvp_tp, script, sizeof(Script));
    mvp_tp.ast_index = {};
    mvp_tp.ast_root = (AstNode*)&satellite_root;
    mvp_tp.pool = compile_pool; mvp_tp.name_pool = compile_name_pool;
    mvp_tp.const_list = compile_const_list; mvp_tp.type_list = compile_type_list;
    mvp_tp.script_owner = script; mvp_tp.runtime = runtime;
    mvp_tp.compile_against_interp_slab = true;
    if (!lambda_mvp_compile_satellite(&mvp_tp, satellite_context,
            &names, &artifacts, &property_keys)) goto fail;
    mem_free(copies);
    copies = NULL;
    if (cancel_probe && cancel_probe(cancel_context)) goto fail;
    // Follow the same mode branch the module compiler uses so a satellite
    // matches the interface its context actually carries.
    MIR_link(satellite_context, satellite_interp ? MIR_set_interp_interface :
        MIR_set_gen_interface, import_resolver);
    if (cancel_probe && cancel_probe(cancel_context)) goto fail;
    entry_name = strbuf_new_cap(96);
    write_fn_name_ex(entry_name, (AstFuncNode*)fn, NULL, "_b");
    entry = !entry_name ? NULL
        : (satellite_interp ? find_func(satellite_context, entry_name->str)
                            : jit_gen_func(satellite_context, entry_name->str));
    if (entry_name) strbuf_free(entry_name);
    if (cancel_probe && cancel_probe(cancel_context)) goto fail;
    if (!entry || !artifacts.consts_bss || !artifacts.consts_bss->addr ||
            !artifacts.type_list_bss || !artifacts.type_list_bss->addr ||
            !artifacts.layout_bss || !artifacts.layout_bss->addr) {
        log_error("interp-tier: satellite link did not publish its immutable BSS");
        goto fail;
    }
#ifndef NDEBUG
    clock_gettime(CLOCK_MONOTONIC, &image_t1);
    image_ms = (double)(image_t1.tv_sec - image_t0.tv_sec) * 1000.0 +
        (double)(image_t1.tv_nsec - image_t0.tv_nsec) / 1e6;
    log_debug("interp-tier: satellite image function='%s' members=%d compile_ms=%.1f",
        fn->name ? fn->name->chars : "<anonymous>", member_count, image_ms);
#endif

    // private descriptors cannot extend the running Script's type registry.
    if (snapshot && compile_type_list->length != source_type_count) {
        log_debug("interp-tier: snapshot rejected function='%s' reason=type-growth",
            fn->name ? fn->name->chars : "<anonymous>");
        goto fail;
    }
    // MVP addresses constants through its image BSS. Compiler-only boundary
    // names stay in this image's retained snapshot, never in the live T0 slab.
    image_consts = snapshot ? compile_const_list : script->const_list;
    *(void**)artifacts.consts_bss->addr = image_consts ? image_consts->data : NULL;
    *(void**)artifacts.type_list_bss->addr = script->type_list;
    layout = (LambdaModuleLayout*)artifacts.layout_bss->addr;
    memset(layout, 0, sizeof(*layout));
    layout->module_id = script_module_layout_id(script);
    // Satellites observe the exact T0 slab, including function-value slots.
    layout->var_count = script->interp_slab_count;
    // the key suffix is placed when publication links it (LR01-16)
    layout->reserved = LAMBDA_MODULE_LAYOUT_APPEND_PROPERTY_KEYS;
    if (!finalize_module_property_key_specs(artifacts.property_specs_bss,
            layout, property_keys)) {
        log_error("interp-tier: satellite property key image could not link");
        goto fail;
    }
    if (property_keys) arraylist_free(property_keys);
    property_keys = NULL;

    image = (InterpSatelliteImage*)mem_calloc(1, sizeof(InterpSatelliteImage),
        MEM_CAT_EVAL);
    if (!image) goto fail;
    image->context = satellite_context;
    image->mir_gen_initialized = !satellite_interp;
    image->module_layout = layout;
    image->compiler_pool = snapshot_pool;
    image->compiler_name_pool = snapshot_name_pool;
    image->compiler_const_list = snapshot_const_list;
    image->compiler_type_list = snapshot_type_list;
    image->target = fn;
    image->target_entry = entry;
    image->member_count = (uint32_t)member_count;
    for (int index = 0; index < member_count; index++) {
        image->members[index] = members[index];
        StrBuf* member_name = strbuf_new_cap(96);
        if (member_name) {
            write_fn_name_ex(member_name, members[index], NULL, "_b");
            image->member_entries[index] = find_func(satellite_context, member_name->str);
            strbuf_free(member_name);
        }
    }
    *out_image = image;
    return true;

fail:
    if (property_keys) arraylist_free(property_keys);
    if (copies) mem_free(copies);
    if (context_owned && satellite_context) {
        jit_cleanup_mode(satellite_context, satellite_interp ? 0 : 1);
    }
    if (snapshot_const_list) arraylist_free(snapshot_const_list);
    if (snapshot_type_list) arraylist_free(snapshot_type_list);
    if (snapshot_name_pool) name_pool_release(snapshot_name_pool);
    if (snapshot_pool) pool_destroy(snapshot_pool);
    return false;
}

void interp_satellite_image_destroy(InterpSatelliteImage* image) {
    if (!image) return;
    if (image->context) {
        jit_cleanup_mode(image->context, image->mir_gen_initialized ? 1 : 0);
        image->context = NULL;
    }
    if (image->compiler_const_list) arraylist_free(image->compiler_const_list);
    if (image->compiler_type_list) arraylist_free(image->compiler_type_list);
    if (image->compiler_name_pool) name_pool_release(image->compiler_name_pool);
    if (image->compiler_pool) pool_destroy(image->compiler_pool);
    if (image->continuation_pool) pool_destroy(image->continuation_pool);
    mem_free(image);
}

bool lambda_mvp_compile_function_snapshot(Runtime* runtime, Script* script,
        const AstFuncNode* fn, uint32_t sequence,
        InterpSatelliteCancelProbe cancel_probe, void* cancel_context,
        InterpSatelliteImage** out_image) {
    return mvp_compile_function_image(runtime, script, fn, sequence,
        true, true, cancel_probe, cancel_context, out_image);
}

bool interp_satellite_image_prepare(Script* script, InterpSatelliteImage* image) {
    if (!script || !image || !image->context || !image->module_layout) return false;
    if (image->module_state_prepared) return true;
    LambdaModuleLayout* layout = image->module_layout;
    // The evaluator links the suffix and records property_key_base in this
    // private image; workers cannot derive a stable append position (D8.3.5).
    layout->reserved |= LAMBDA_MODULE_LAYOUT_APPEND_PROPERTY_KEYS;
    image->module_state_prepared = lambda_module_state_bind_static(script->module_state_id,
            script->const_list ? script->const_list->data : NULL,
            script->type_list) && prepare_context_module_state(image->context,
            script->const_list ? script->const_list->data : NULL,
            script->type_list);
    return image->module_state_prepared;
}

bool lambda_mvp_compile_function(Runtime* runtime, Script* script,
        const AstFuncNode* fn, void** out_boxed_entry) {
    if (out_boxed_entry) *out_boxed_entry = NULL;
    if (!runtime || !script || !fn || !out_boxed_entry) return false;

    InterpSatelliteImage* image = NULL;
    uint32_t sequence = ++script->interp_satellite_count;
    if (!mvp_compile_function_image(runtime, script, fn, sequence,
            false, true, NULL, NULL, &image) || !image) {
        return false;
    }
    // Keep the first image as the Script's lookup/teardown anchor.  Later
    // satellites still force a new context above, so this pointer is never a
    // compile target and cannot have its BSS layout reinitialized by MIR_link.
    bool install_anchor = !script->jit_context;
    if (install_anchor) {
        script->jit_context = image->context;
        script->mir_gen_initialized = image->mir_gen_initialized;
    }
    // A synchronous task-context entry is otherwise a private image.
    // Re-linking the anchor would reinitialize its BSS layout, so retain every
    // later image independently before its closure becomes callable
    // (D8.3.5).
    if (!interp_satellite_image_prepare(script, image) ||
            (!install_anchor && !interp_satellite_image_retain(script, image))) {
        log_error("interp-tier: synchronous satellite module-state publish failed");
        if (install_anchor) {
            script->jit_context = NULL;
            script->mir_gen_initialized = false;
        }
        interp_satellite_image_destroy(image);
        return false;
    }
    for (uint32_t index = 0; index < image->member_count; index++) {
        if (image->members[index] != fn && image->member_entries[index]) {
            (void)interp_publish_satellite_member(script,
                (AstFuncNode*)image->members[index], image->member_entries[index]);
        }
    }
    *out_boxed_entry = image->target_entry;
    if (install_anchor) {
        image->context = NULL;
        interp_satellite_image_destroy(image);
    }
    log_debug("interp-tier: satellite compiled function='%s' image=%u",
        fn->name ? fn->name->chars : "<anonymous>", (unsigned)sequence);
    return true;
}

// ============================================================================
// Per-module MIR Direct compilation (used when runtime->use_mir_direct is set)
// ============================================================================

// Registers all pub functions and pub variable BSS addresses from imp's already-compiled
// jit_context into the global dynamic_import_table so import_resolver can resolve them
// when MIR_link() is called for the module that imports this one.
static void register_module_pub_fns(AstImportNode* imp, Script* dependency) {
    if (!dependency) dependency = imp ? imp->script : NULL;
    if (!imp || !dependency || !dependency->jit_context) {
        log_error("mir cache: import '%.*s' has no compiled dependency context "
                  "dependency=%p jit=%p cache-template=%p imports=%d",
                  imp ? (int)imp->module.length : 0,
                  imp ? imp->module.str : "", dependency,
                  dependency ? dependency->jit_context : NULL,
                  dependency ? dependency->cache_template : NULL,
                  dependency && dependency->direct_imports
                      ? dependency->direct_imports->length : -1);
        assert(false && "MIR Direct import dependency must be compiled before importer");
        return;
    }
    AstNode* mod_node = dependency->ast_root;
    if (!mod_node || mod_node->node_type != AST_SCRIPT) return;

    log_info("mir: registering pub symbols from module '%.*s' (index=%d)",
        (int)imp->module.length, imp->module.str, dependency->index);

    AstNode* mod_child = ((AstScript*)mod_node)->child;
    while (mod_child) {
        if (mod_child->node_type == AST_NODE_CONTENT) {
            mod_child = ((AstListNode*)mod_child)->item;
            continue;
        }
        if (mod_child->node_type == AST_NODE_FUNC ||
            mod_child->node_type == AST_NODE_FUNC_EXPR ||
            mod_child->node_type == AST_NODE_PROC) {
            AstFuncNode* fn_node = (AstFuncNode*)mod_child;
            TypeFunc* fn_type = (TypeFunc*)fn_node->type;
            if (fn_type && fn_type->is_public) {
                // Register boxed wrapper (_b suffix) if the function needs one
                // (due to declared typed params OR inferred native params)
                // Use module-local name to find the function in its own MIR context,
                // then register with module-prefixed name for cross-module resolution.
                // This prevents name collisions when different modules have functions
                // with the same name at the same byte offset (e.g. fraction.ls and
                // style.ls both have pub fn render at byte 574 → _render_574).
                StrBuf* fn_name = strbuf_new_cap(64);
                write_fn_name_ex(fn_name, fn_node, NULL, "_b");
                void* fn_ptr = find_func(dependency->jit_context, fn_name->str);
                if (fn_ptr) {
                    StrBuf* reg_name = strbuf_new_cap(64);
                    write_fn_name_for_script_ex(reg_name, fn_node, dependency, "_b");
                    register_dynamic_import(raw_strdup(reg_name->str), fn_ptr); // RAWALLOC_OK: MIR manages param name lifetime
                    log_debug("mir: registered import wrapper fn: %s -> %p", reg_name->str, fn_ptr);
                    strbuf_free(reg_name);
                }
                strbuf_free(fn_name);
            }
        } else if (mod_child->node_type == AST_NODE_PUB_STAM) {
            // Register pub variable BSS addresses.
            // MIR Direct names BSS items "_gvar_<name>" (see prepass_create_global_vars).
            // The consumer imports "m<unit>._<name>" (mir_write_var_name), so a
            // name two dependencies both export stays two symbols.
            AstNode* declare = ((AstLetNode*)mod_child)->declare;
            while (declare) {
                AstDeclaratorNode* named = (AstDeclaratorNode*)declare;
                if (named->name) {
                    // dependency-local name, for the lookup in its own context
                    StrBuf* local_key = strbuf_new_cap(64);
                    write_var_name(local_key, declare, NULL);  // e.g. "_pi"
                    // Import key (what the consumer's MIR_new_import uses)
                    StrBuf* import_key = strbuf_new_cap(64);
                    write_var_name_for_script(import_key, declare, dependency);  // e.g. "m2._pi"
                    // MIR Direct BSS name: "_gvar_<rawname>"
                    StrBuf* gvar = strbuf_create("_gvar_");
                    strbuf_append_str_n(gvar, named->name->chars, named->name->len);
                    MIR_item_t bss_item = find_import(dependency->jit_context, gvar->str);
                    strbuf_free(gvar);
                    if (!bss_item || !bss_item->addr) {
                        bss_item = find_import(dependency->jit_context, local_key->str);
                    }
                    if (bss_item && bss_item->addr) {
                        register_dynamic_import(raw_strdup(import_key->str), bss_item->addr); // RAWALLOC_OK: MIR manages param name lifetime
                        log_debug("mir: registered import var BSS: %s -> %p", import_key->str, bss_item->addr);
                    }
                    strbuf_free(import_key);
                    strbuf_free(local_key);
                }
                declare = declare->next;
            }
        }
        mod_child = mod_child->next;
    }

    // S11.4.11 (LR03-27): an importer calls an imported type's `that` predicate
    // function here, in its declaring module, whatever the function's own
    // visibility. The type list also holds layers other modules declared;
    // their owners export those.
    ArrayList* types = dependency->type_list;
    for (int i = 0; types && i < types->length; i++) {
        Type* type = (Type*)types->data[i];
        if (!lambda_type_is_constrained(type)) continue;
        TypeConstrained* layer = (TypeConstrained*)type;
        if (!layer->predicate_fn || (layer->module != dependency &&
                layer->module != dependency->cache_template)) continue;
        StrBuf* local_name = strbuf_new_cap(64);
        write_fn_name_ex(local_name, layer->predicate_fn, NULL, "_b");
        void* fn_ptr = find_func(dependency->jit_context, local_name->str);
        if (fn_ptr) {
            StrBuf* reg_name = strbuf_new_cap(64);
            write_fn_name_for_script_ex(reg_name, layer->predicate_fn, dependency, "_b");
            register_dynamic_import(raw_strdup(reg_name->str), fn_ptr); // RAWALLOC_OK: MIR manages param name lifetime
            strbuf_free(reg_name);
        }
        strbuf_free(local_name);
    }
}

// Register cross-language (e.g., JS) module exports for MIR import resolution.
// Unlike register_module_pub_fns which looks up compiled MIR functions from a jit_context,
// this reads native function pointers directly from the JS namespace object stored in the
// unified module registry, then registers them with module-prefixed names so that
// import_resolver() can resolve them during MIR_link().
static void register_cross_lang_pub_fns(Runtime* runtime, AstImportNode* imp) {
    if (!imp->script || !imp->script->reference) return;
    AstNode* mod_node = imp->script->ast_root;
    if (!mod_node || mod_node->node_type != AST_SCRIPT) return;

    ModuleDescriptor* desc = module_get_for_runtime(runtime, imp->script->reference);
    if (!desc) {
        log_error("mir: cross-lang module '%s' not found in registry", imp->script->reference);
        return;
    }
    log_debug("mir: registering cross-lang pub symbols from '%.*s' (index=%d)",
        (int)imp->module.length, imp->module.str, imp->script->index);

    AstNode* child = ((AstScript*)mod_node)->child;
    while (child) {
        if (child->node_type == AST_NODE_FUNC) {
            AstFuncNode* fn_node = (AstFuncNode*)child;
            TypeFunc* fn_type = (TypeFunc*)fn_node->type;
            if (fn_type && fn_type->is_public) {
                // The language-specific namespace membrane owns export lookup.
                const char* name_buf = fn_node->name->chars;
                Item fn_item = module_namespace_get(desc, name_buf);
                if (get_type_id(fn_item) == LMD_TYPE_FUNC) {
                    // JS exports must retain their JsFunction wrapper: it owns
                    // the defining module-vars array that a direct raw code
                    // pointer cannot restore for a Lambda-to-JS call.
                    bool is_js_export = lambda_import_is_js(imp);
                    void* fn_ptr = is_js_export ? (void*)fn_item.function
                        : module_namespace_function_ptr(desc, fn_item);
                    if (fn_ptr) {
                        // Register with module-prefixed name (e.g., "m2._add_1000000")
                        StrBuf* reg_name = strbuf_new_cap(64);
                        write_fn_name(reg_name, fn_node, imp);
                        register_dynamic_import(raw_strdup(reg_name->str), fn_ptr); // RAWALLOC_OK: MIR manages param name lifetime
                        strbuf_free(reg_name);
                    } else {
                        log_error("mir: cross-lang fn '%s' has null ptr", name_buf);
                    }
                } else {
                    log_error("mir: cross-lang fn '%s' not found in namespace (type=%d)",
                        name_buf, get_type_id(fn_item));
                }
            }
        }
        child = child->next;
    }
}

// Compile a module whose AST is already built (tp->ast_root set) via MIR Direct.
// Called from transpile_script() when runtime->use_mir_direct is true, for both
// imported modules and the main script.  Depth-first import loading guarantees that
// all of this module's sub-imports are already compiled before we are called.
static bool finalize_module_property_key_specs(MIR_item_t property_specs_bss,
        LambdaModuleLayout* layout, ArrayList* property_keys) {
    if (!layout) return false;
    layout->property_key_count = property_keys ? (uint32_t)property_keys->length : 0;
    layout->property_key_bytes_size = 0;
    layout->property_key_specs = NULL;
    if (!property_keys || property_keys->length == 0) return true;
    if (!property_specs_bss || !property_specs_bss->addr) {
        log_error("module-key-link: property spec BSS missing after link");
        return false;
    }
    uint8_t* bytes = (uint8_t*)property_specs_bss->addr;
    PropertyKeySpec* specs = (PropertyKeySpec*)bytes;
    uint32_t capacity = 0;
    if (!em_module_property_key_image_size(property_keys, &capacity)) return false;
    uint32_t bytes_size = 0;
    if (!build_property_key_image(property_keys, specs, (uint32_t)capacity,
            &bytes_size)) {
        // The BSS size was computed from the same entries; a failure here is
        // an internal image-shape violation rather than a recoverable input.
        return false;
    }
    layout->property_key_bytes_size = bytes_size;
    layout->property_key_specs = specs;
    return true;
}

static void finalize_context_module_layout(MIR_context_t ctx, Script* script,
        ArrayList* property_keys,bool interp_slab) {
    if (!ctx || !script) return;
    uint32_t slot = 0;
    LambdaModuleLayout* layout = NULL;
    for (MIR_module_t module = DLIST_HEAD(MIR_module_t, *MIR_get_module_list(ctx));
         module != NULL; module = DLIST_NEXT(MIR_module_t, module)) {
        for (MIR_item_t item = DLIST_HEAD(MIR_item_t, module->items);
             item != NULL; item = DLIST_NEXT(MIR_item_t, item)) {
            if (item->item_type != MIR_bss_item || !item->u.bss->name || !item->addr) continue;
            if (strcmp(item->u.bss->name, "_mod_layout") == 0) {
                layout = (LambdaModuleLayout*)item->addr;
            } else if (strncmp(item->u.bss->name, "_gvar_", 6) == 0) {
                LambdaModuleVarRef* ref = (LambdaModuleVarRef*)item->addr;
                ref->module_id = script_module_layout_id(script);
                ref->slot = slot++;
            }
        }
    }
    if (!layout) {
        log_error("module-state: missing layout BSS for '%s'",
            script->reference ? script->reference : "<unknown>");
        return;
    }
    layout->module_id = script_module_layout_id(script);
    // native exports over a resident T0 module retain function slots and append their key suffix.
    layout->var_count = interp_slab?script->interp_slab_count:slot;
    layout->reserved = interp_slab?LAMBDA_MODULE_LAYOUT_APPEND_PROPERTY_KEYS:0;
    layout->property_key_base = 0;
    if (!finalize_module_property_key_specs(find_import(ctx,
            "_mod_property_specs"), layout, property_keys)) {
        log_error("module-key-link: failed to seal property keys for '%s'",
            script->reference ? script->reference : "<unknown>");
    }
}

static bool lambda_mir_interp_env_enabled(void) {
    const char* env = getenv("JS_MIR_INTERP");
    return env && (strcmp(env, "1") == 0 || strcmp(env, "true") == 0);
}

typedef struct LambdaMirLinkPassContext {
    MIR_context_t ctx;
    Transpiler* tp;
    uint64_t mir_instruction_count;
    uint64_t mir_largest_function_instruction_count;
    bool use_mir_interp_for_script;
    main_func_t main_func;
} LambdaMirLinkPassContext;

static int lambda_mir_link_compiler_pass(void* opaque) {
    LambdaMirLinkPassContext* pass = (LambdaMirLinkPassContext*)opaque;
    if (!pass || !pass->ctx || !pass->tp) return 0;
    unsigned int opt_level = pass->tp->runtime ? pass->tp->runtime->optimize_level : 2;
    bool explicit_interp = g_mir_interp_mode != 0 || lambda_mir_interp_env_enabled();
    bool auto_interp = !pass->tp->requires_native_mir_exports &&
        !explicit_interp && opt_level == 0 &&
        mir_large_interp_enabled() && pass->tp->source &&
        strlen(pass->tp->source) >= mir_large_source_interp_threshold();
    bool use_interp = explicit_interp || auto_interp;
    bool document_context = pass->tp->runtime && pass->tp->runtime->dom_doc != nullptr;
    MirLinkSelection selection = mir_select_link_interface(
        pass->mir_instruction_count, pass->mir_largest_function_instruction_count,
        document_context || g_js_force_document_interp, opt_level,
        lambda_mir_lazy_enabled());
    if (!use_interp && !pass->tp->requires_native_mir_exports &&
            selection.interface_kind == MIR_LINK_INTERP) {
        use_interp = true;
        log_info("lambda-mir: policy selected interpreter for %llu instructions%s",
            (unsigned long long)pass->mir_instruction_count,
            document_context ? " [document]" : "");
    }
    if (!use_interp && selection.optimize_level != opt_level) {
        log_info("lambda-mir: policy selected opt=%u for large function (was %u)",
            selection.optimize_level, opt_level);
        MIR_gen_set_optimize_level(pass->ctx, selection.optimize_level);
    }
    MIR_link(pass->ctx, use_interp ? MIR_set_interp_interface :
        (selection.interface_kind == MIR_LINK_LAZY_NATIVE
            ? MIR_set_lazy_gen_interface : MIR_set_gen_interface), import_resolver);
    pass->use_mir_interp_for_script = use_interp;
    pass->main_func = (main_func_t)(use_interp ? find_func(pass->ctx, "main")
        : jit_gen_func(pass->ctx, "main"));
    return pass->main_func != NULL;
}

// Recreate execution-owned view/edit dispatch after loading a sealed MIR image.
void lambda_register_mir_view_templates(Script* script) {
    if (!script || !script->main_func || !script->ast_root || !script->jit_context) return;
    if (!g_template_registry) g_template_registry = template_registry_new();
    if (!g_template_registry) return;
    AstScript* ast_root = (AstScript*)script->ast_root;
    MIR_context_t ctx = script->jit_context;
    // Walk the sealed AST because the registry belongs to this execution.
    {
        // Walk through content block to find view/edit nodes
        AstNode* first_child = ast_root->child;
        AstNode* tmpl_child = first_child;
        // If the first child is a content block, walk its items
        if (tmpl_child && tmpl_child->node_type == AST_NODE_CONTENT) {
            tmpl_child = ((AstListNode*)tmpl_child)->item;
        }
        int view_idx = 0;
        while (tmpl_child) {
            AstNode* view_child = tmpl_child;
            AstNode* next_tmpl_child = tmpl_child->next;
            if (tmpl_child->node_type == AST_NODE_CONTENT) {
                view_child = ((AstListNode*)tmpl_child)->item;
            }
            while (view_child) {
                if (view_child->node_type == AST_NODE_VIEW) {
                    AstViewNode* view = (AstViewNode*)view_child;

                    // build the function name used during transpilation
                    char func_name[64];
                    snprintf(func_name, sizeof(func_name), "_view_%d", view_idx++);

                    void* func_ptr = find_func(ctx, func_name);
                    if (func_ptr) {
                        const char* tmpl_name = view->name ? view->name->chars : NULL;
                        template_registry_add(g_template_registry,
                            tmpl_name, view->is_edit, (fn_ptr)func_ptr,
                            TMPL_SPEC_CATCHALL, LMD_TYPE_ANY, NULL, 0, 0, 0);

                        // get the just-added entry (it's the last one)
                        TemplateEntry* tmpl_entry = g_template_registry->last;
                        template_registry_set_view_pattern(tmpl_entry, view);
                        template_registry_set_state_declarations(tmpl_entry, view);

                        // set template_ref for state store keying
                        // func_name is stack-local, so we need a persistent copy
                        if (tmpl_name) {
                            tmpl_entry->template_ref = tmpl_name;
                        } else {
                            // JIT handlers embed this interned pointer. On a
                            // hit, reuse it without mutating the cached pool.
                            String* ref = script->cache_template
                                ? name_pool_lookup_len(script->name_pool,
                                    func_name, strlen(func_name))
                                : name_pool_create_len(script->name_pool,
                                    func_name, strlen(func_name));
                            if (!ref) {
                                log_error("MIR template: missing compiled ref %s", func_name);
                            }
                            tmpl_entry->template_ref = ref ? ref->chars : NULL;
                        }

                        // register event handlers on the template entry
                        int hidx = 0;
                        for (AstEventHandler* h = view->handler; h; h = h->next_handler) {
                            char hname[64];
                            snprintf(hname, sizeof(hname), "%s_h%d", func_name, hidx++);
                            void* hptr = find_func(ctx, hname);
                            if (hptr) {
                                const char* ename = h->event ? h->event->chars : "unknown";
                                template_entry_add_handler(tmpl_entry, ename, (fn_ptr)hptr);
                            } else {
                                log_error("MIR Direct: handler function '%s' not found", hname);
                            }
                        }

                        log_debug("registered template '%s' func=%s spec=%d type=%d handlers=%d",
                            tmpl_name ? tmpl_name : "(anonymous)", func_name,
                            (int)tmpl_entry->specificity, (int)tmpl_entry->match_type_id, hidx);
                    } else {
                        log_error("MIR Direct: view function '%s' not found after JIT", func_name);
                    }
                }
                view_child = (tmpl_child->node_type == AST_NODE_CONTENT) ? view_child->next : NULL;
            }
            tmpl_child = next_tmpl_child;
        }
    }

}

void lambda_mvp_compile_script(Transpiler* tp, Script* script, const char* script_path,
                                   double* out_jit_init_ms,
                                   double* out_transpile_ms,
                                   double* out_mir_gen_ms,
                                   uint64_t* out_mir_module_count,
                                   uint64_t* out_mir_function_count,
                                   uint64_t* out_mir_instruction_count) {
    log_debug("MIR Direct: compiling module '%s'", script_path ? script_path : "<unknown>");

    EvalContext* template_context = tp && tp->runtime
        ? runtime_get_eval_context(tp->runtime) : context;
    if (!template_context) {
        log_error("template-registry: MIR compilation has no canonical EvalContext");
        return;
    }
    // Compilation may initialize an otherwise idle eval thread, but it cannot
    // borrow a different live context and restore the caller afterward.
    if (!eval_context_init(template_context)) return;

    // Ensure template registry exists for post-JIT template registration
    if (!g_template_registry) {
        g_template_registry = template_registry_new();
    }

    bool timing = out_jit_init_ms || out_transpile_ms || out_mir_gen_ms;

    // Clear the dynamic-import table and populate it with this module's direct imports only.
    // Because imports are compiled depth-first, every sub-import's symbols were already
    // registered by their own lambda_mvp_compile_script() call; we clear here so that
    // only THIS module's direct dependencies are visible to import_resolver during MIR_link().
    clear_dynamic_imports();

    AstScript* ast_root = (AstScript*)tp->ast_root;
    transpiler_clear_direct_imports(tp, script);
    tp->cache_cross_lang_tainted = false;
    AstNode* child = ast_root->child;
    while (child) {
        if (child->node_type == AST_NODE_IMPORT) {
            AstImportNode* imp = (AstImportNode*)child;
            if (imp->is_cross_lang) {
                tp->cache_cross_lang_tainted = true;
                register_cross_lang_pub_fns(tp->runtime, imp);
            } else {
                // D8.3.5: parser-owned import ASTs belong to the immutable
                // cache template, while this compilation must link the current
                // Runtime's cloned dependency and its module-state slab.
                Script* dependency = lambda_ast_overlay_import_script(script, imp);
                if (!tp->direct_imports) tp->direct_imports = arraylist_new(4);
                if (dependency) {
                    arraylist_append(tp->direct_imports, dependency);
                    if (dependency->cache_cross_lang_tainted) {
                        tp->cache_cross_lang_tainted = true;
                    }
                }
                register_module_pub_fns(imp, dependency);
            }
        }
        child = child->next;
    }

    // Profiling timestamps (only when profiling is requested)
#ifdef _WIN32
    LARGE_INTEGER pt0, pt1, pt2, pt3, pfreq;
    if (timing) { QueryPerformanceFrequency(&pfreq); QueryPerformanceCounter(&pt0); }
#else
    struct timespec pt0, pt1, pt2, pt3;
    if (timing) clock_gettime(CLOCK_MONOTONIC, &pt0);
#endif

    // Transpile the AST directly to MIR (no C code generated). LambdaJS uses
    // the interpreter for large source at O0 before JIT initialization; mirror
    // that early decision while retaining the explicit --mir-interp/env mode.
    unsigned int opt_level = tp->runtime ? tp->runtime->optimize_level : 2;
    bool explicit_interp = g_mir_interp_mode != 0 || lambda_mir_interp_env_enabled();
    bool auto_interp_for_large_source = !tp->requires_native_mir_exports &&
        !explicit_interp && opt_level == 0 &&
        mir_large_interp_enabled() && tp->source &&
        strlen(tp->source) >= mir_large_source_interp_threshold();
    bool force_interp_init = !g_mir_interp_mode &&
        (lambda_mir_interp_env_enabled() || auto_interp_for_large_source);
    int saved_mir_interp_mode = g_mir_interp_mode;
    bool mir_gen_initialized = saved_mir_interp_mode == 0 && !force_interp_init;
    MirNativeCodegenLock codegen_lock(mir_gen_initialized);
    if (force_interp_init) g_mir_interp_mode = 1;
    MIR_context_t ctx = jit_init(opt_level);
    if (force_interp_init) g_mir_interp_mode = saved_mir_interp_mode;

#ifdef _WIN32
    if (timing) QueryPerformanceCounter(&pt1);
#else
    if (timing) clock_gettime(CLOCK_MONOTONIC, &pt1);
#endif

    ArrayList* property_keys = NULL;
    LambdaMirLinkPassContext link_context = {ctx, tp, 0, 0, false, NULL};
    LambdaMirPipelinePassContext mir_context = {};
    mir_context.ctx = ctx;
    mir_context.tp = tp;
    mir_context.property_keys = &property_keys;
    mir_context.link_instruction_count = &link_context.mir_instruction_count;
    mir_context.link_largest_function_instruction_count =
        &link_context.mir_largest_function_instruction_count;
    // Direct MIR resumes parser work; retained ASTs begin a fresh unit.
    CompilerPassManager* pass_manager = &tp->pass_manager;
    // D8.3.5: an AST-cache shell borrows the template's index. MIR fallback
    // may use the facts but cannot destroy them when it adopts its own image.
    if (script->cache_template) tp->preserve_ast_index = true;
    if (pass_manager->pass_count == 0) {
        compiler_pass_manager_init(pass_manager, COMPILER_FACT_FRONTEND |
            (tp->ast_index.graph_published ? COMPILER_FACT_INDEXED : 0));
    }
    AstIndexPassContext index_context = {&tp->ast_index, tp->ast_root, tp->profile};
    CompilerPassSpec index_pass = {"index", COMPILER_FACT_FRONTEND,
        COMPILER_FACT_INDEXED, ast_index_compiler_pass, &index_context};
    CompilerPassSpec const_fold_pass = {"const-fold", COMPILER_FACT_INDEXED,
        COMPILER_FACT_ANALYZED, lambda_const_fold_compiler_pass, tp};
    CompilerPassSpec plan_pass = {"mir-plan", COMPILER_FACT_ANALYZED,
        COMPILER_FACT_PLANNED, lambda_mir_plan_compiler_pass, &mir_context};
    CompilerPassSpec lower_pass = {"mir-lower",
        COMPILER_FACT_ANALYZED | COMPILER_FACT_PLANNED,
        COMPILER_FACT_MIR_LOWERED, lambda_mir_lower_compiler_pass, &mir_context};
    CompilerPassSpec finalize_pass = {"mir-finalize-load", COMPILER_FACT_MIR_LOWERED,
        COMPILER_FACT_FINALIZED, lambda_mir_finalize_compiler_pass, &mir_context};
    CompilerPassSpec link_pass = {"mir-link-entry", COMPILER_FACT_FINALIZED,
        COMPILER_FACT_LINKED, lambda_mir_link_compiler_pass, &link_context};
    if ((!tp->ast_index.graph_published && !compiler_pass_manager_add(pass_manager,
                &index_pass)) ||
            !compiler_pass_manager_add(pass_manager, &const_fold_pass) ||
            !compiler_pass_manager_add(pass_manager, &plan_pass) ||
            !compiler_pass_manager_add(pass_manager, &lower_pass) ||
            !compiler_pass_manager_add(pass_manager, &finalize_pass) ||
            !compiler_pass_manager_add(pass_manager, &link_pass) ||
            !compiler_pass_manager_run(pass_manager, NULL)) {
        log_error("mir-driver: pass manager rejected module '%s'",
            script_path ? script_path : "<unknown>");
        // finalization can hand off the key table before a later link failure.
        if (property_keys) arraylist_free(property_keys);
        lambda_mvp_destroy(mir_context.mvp);
        jit_cleanup_mode(ctx, mir_gen_initialized ? 1 : 0);
        return;
    }
    if ((pass_manager->facts & COMPILER_FACT_LINKED) == 0) {
        log_error("mir-driver: link pass did not publish module '%s'",
            script_path ? script_path : "<unknown>");
        if (property_keys) arraylist_free(property_keys);
        lambda_mvp_destroy(mir_context.mvp);
        jit_cleanup_mode(ctx, mir_gen_initialized ? 1 : 0);
        return;
    }
    uint64_t mir_module_count = mir_context.mir_module_count;
    uint64_t mir_function_count = mir_context.mir_function_count;
    uint64_t mir_instruction_count = mir_context.mir_instruction_count;

    bool use_mir_interp_for_script = link_context.use_mir_interp_for_script;

#ifdef _WIN32
    if (timing) QueryPerformanceCounter(&pt2);
#else
    if (timing) clock_gettime(CLOCK_MONOTONIC, &pt2);
#endif

    // Store results in the transpiler and propagate back to script
    tp->jit_context = ctx;
    tp->mir_gen_initialized = mir_gen_initialized;
    tp->main_func = link_context.main_func;

#ifdef _WIN32
    if (timing) QueryPerformanceCounter(&pt3);
#else
    if (timing) clock_gettime(CLOCK_MONOTONIC, &pt3);
#endif

    // Set per-module consts BSS AFTER jit_gen_func: jit_gen_func re-invokes MIR_link
    // which reallocates BSS memory, so any earlier write would be overwritten.
    // After jit_gen_func, BSS addresses are final.
    MIR_item_t consts_bss_item = find_import(ctx, "_mod_consts_ptr");
    if (consts_bss_item && consts_bss_item->addr) {
        *(void**)(consts_bss_item->addr) = tp->const_list ? tp->const_list->data : nullptr;
    } else {
        log_error("MIR Direct: _mod_consts_ptr BSS not found or no addr for '%s' (addr=%p)",
                  script_path ? script_path : "<unknown>",
                  consts_bss_item ? consts_bss_item->addr : nullptr);
    }

    // Set per-module type_list BSS (same timing: must be after jit_gen_func).
    MIR_item_t tl_bss_item = find_import(ctx, "_mod_type_list_ptr");
    if (tl_bss_item && tl_bss_item->addr) {
        *(void**)(tl_bss_item->addr) = tp->type_list;
    } else {
        log_error("MIR Direct: _mod_type_list_ptr BSS not found or no addr for '%s'",
                  script_path ? script_path : "<unknown>");
    }

    finalize_context_module_layout(ctx, script, property_keys,tp->compile_against_interp_slab);
    if (property_keys) arraylist_free(property_keys);

    // The same registration runs when a cached MIR graph enters a fresh Runtime.
    lambda_register_mir_view_templates(tp);
    lambda_mvp_destroy(mir_context.mvp);

    if (!tp->main_func) {
        log_error("MIR Direct: failed to generate 'main' for '%s'",
                  script_path ? script_path : "<unknown>");
        jit_cleanup_mode(ctx, mir_gen_initialized ? 1 : 0);
        tp->jit_context = NULL;
    }

    // After the final MIR_link + jit_gen_func the machine code of every
    // function is published and this sealed per-module ctx never links again,
    // so the MIR IR is dead weight — release it, keeping only the executable
    // code. MIR-interp scripts keep their IR: there it IS the executable.
    if (tp->jit_context && tp->main_func && !use_mir_interp_for_script) {
        jit_release_generated_ir(ctx);
    }

    if (out_mir_module_count) *out_mir_module_count = mir_module_count;
    if (out_mir_function_count) *out_mir_function_count = mir_function_count;
    if (out_mir_instruction_count) *out_mir_instruction_count = mir_instruction_count;

    // Lambda MIR currently consumes the indexed AST during the compiler pass;
    // release its malloc-backed table before transferring the Script fields so
    // batch teardown never retains an index whose AST owner is already gone.
    if (!tp->preserve_ast_index) {
        ast_index_destroy(&tp->ast_index);
    }

    // Copy Script-sized portion of Transpiler back to the Script object
    script_adopt_transpiler(script, tp);
    script->jit_context = tp->jit_context;
    script->main_func   = tp->main_func;

    // Write out profiling results
    if (timing) {
#ifdef _WIN32
        double f = 1000.0 / (double)pfreq.QuadPart;
        if (out_jit_init_ms)  *out_jit_init_ms  = (double)(pt1.QuadPart - pt0.QuadPart) * f;
        if (out_transpile_ms) *out_transpile_ms = (double)(pt2.QuadPart - pt1.QuadPart) * f;
        if (out_mir_gen_ms)   *out_mir_gen_ms   = (double)(pt3.QuadPart - pt2.QuadPart) * f;
#else
        auto elapsed = [](struct timespec a, struct timespec b) -> double {
            long sec = b.tv_sec - a.tv_sec;
            long nsec = b.tv_nsec - a.tv_nsec;
            if (nsec < 0) { sec--; nsec += 1000000000L; }
            return sec * 1000.0 + nsec / 1e6;
        };
        if (out_jit_init_ms)  *out_jit_init_ms  = elapsed(pt0, pt1);
        if (out_transpile_ms) *out_transpile_ms = elapsed(pt1, pt2);
        if (out_mir_gen_ms)   *out_mir_gen_ms   = elapsed(pt2, pt3);
#endif
    }

    if (tp->main_func) {
        log_info("MIR Direct: compiled '%s' ctx=%p main=%p",
                 script_path ? script_path : "<unknown>", ctx, tp->main_func);
    }
}
