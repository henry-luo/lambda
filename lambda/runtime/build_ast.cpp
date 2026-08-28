#include "transpiler.hpp"
#include "../core/lambda-decimal.hpp"
#include "lambda-number-types.hpp"
#include "lambda-error.h"
#include "type_contract.hpp"
#include "type_build.hpp"
#include "ast_build.hpp"
#include "parse_type_pattern.hpp"
#include "parse_path_expr.hpp"
#ifndef SIMPLE_SCHEMA_PARSER
#include "module_registry.h"
#include "../jube/jube_language.h"
#include "../jube/jube_registry.h"
#endif
#include "../../lib/hashmap.h"
#include "../../lib/datetime.h"
#include "../../lib/log.h"
#include "../../lib/memtrack.h"
#include "../../lib/mem_factory.h"
#include <errno.h>
#include <stdlib.h>

// C16 ruling 9 (revised): an unsuffixed literal's type is LEXICAL, and an
// exponent makes it a float -- so an integer token is now only digits or hex,
// and the exponent scaling this used to perform is gone with the grammar rule
// that produced `10e1` as an integer token. Returns false when the value leaves the
// ingestion band (ruling 6).
static bool lambda_parse_int_literal(const char* text, int64_t* out) {
    char* endptr = NULL;
    errno = 0;
    int64_t mantissa = strtoll(text, &endptr, 0);
    if (errno == ERANGE) return false;
    *out = mantissa;
    return mantissa >= INT53_MIN && mantissa <= INT53_MAX;
}


#include "../../lib/str.h"
#include "../../lib/strview.h"
#include "../../lib/arraylist.h"
#include "../../lib/file.h"
#include "../../lib/recursion_guard.hpp"
#include <errno.h>
#include <stdlib.h>

static StrView source_span_text(Transpiler* tp, SourceSpan span) {
    return (StrView){.str = tp->source + span.start_byte,
        .length = lambda_source_span_length(span)};
}

static StrView ast_node_source(Transpiler* tp, const AstNode* node) {
    SourceSpan span = node ? node->source_span : (SourceSpan){0, 0};
    return source_span_text(tp, span);
}

// keep fallible literal source copies in one checked path.
static char* ast_copy_source_text(Transpiler* tp, StrView source,
        SourceSpan diagnostic_span) {
    char* copy = (char*)mem_alloc(source.length + 1, MEM_CAT_AST);
    if (!copy) {
        record_semantic_error_span(tp, diagnostic_span, ERR_OUT_OF_MEMORY,
            "out of memory while reading literal source");
        return NULL;
    }
    memcpy(copy, source.str, source.length);
    copy[source.length] = '\0';
    return copy;
}

static LambdaSourcePoint ast_node_start_point(Transpiler* tp, const AstNode* node) {
    SourceSpan span = node ? node->source_span : (SourceSpan){0, 0};
    return lambda_source_span_start_point(tp->source, span);
}

static StaticBoundaryResult static_boundary_relation(Type* source, Type* target);
bool lambda_ast_validate_call_arguments(Transpiler* tp, AstCallNode* call,
    SourceSpan diagnostic_span, int arg_count);

// Forward declaration for imported module resolution
static const char* resolve_imported_module(Transpiler* tp, StrView* name);

// S16.6.8 operand guard; defined with the branch-homogeneity validators below.
static void reject_procedural_block_operand(Transpiler* tp, AstNode* operand,
        const char* position);

// Shared element namespace desugaring helpers used by the direct parser.
static AstNode* build_ns_attr_map_from_parts(Transpiler* tp, StrView attr_name,
        AstNode* val_expr, SourceSpan span);
static void merge_ns_attr_maps(Transpiler* tp, AstNode* dst_item, AstNode* src_item);
static AstNamedNode* find_existing_named_item(AstNode* first_item, String* name);

// Defined with the E228 traversal below; match construction uses the same
// pattern classification for the implicit-parameter dead-arm lint.
static bool match_arm_is_error_handler(AstMatchArm* arm);
static bool match_has_error_handler(AstMatchNode* match);

typedef bool (*LambdaAstVisitor)(AstNode* node, void* data);
static void walk_lambda_ast(AstNode* node, LambdaAstVisitor visitor, void* data,
                            bool descend_functions);

// System function definitions — single source of truth in sys_func_registry.cpp
// See lambda/sys_func_registry.cpp for the complete table.
extern SysFuncInfo sys_func_defs[];
extern const int sys_func_def_count;

// ============================================================================
// O(1) System Function Lookup via Hashmap
// ============================================================================
// Two hashmaps:
//   1. sys_func_map: composite key (name, arg_count) → SysFuncInfo*
//      Used by get_sys_func_info() and get_sys_func_for_method()
//   2. sys_func_name_set: name-only key → bool sentinel
//      Used by is_sys_func_name()
// Both are lazily initialized on first access.

// Composite key for (name, arg_count) lookups
typedef struct {
    const char* name;
    int name_len;
    int arg_count;
    SysFuncInfo* info;
} SysFuncEntry;

// Name-only key for existence checks
typedef struct {
    const char* name;
    int name_len;
} SysFuncNameEntry;

static struct hashmap* sys_func_map = NULL;       // (name, arg_count) → SysFuncInfo*
static struct hashmap* sys_func_name_set = NULL;   // name → exists

typedef struct JubeSysFuncRecord {
    SysFuncInfo info;
    char name[128];
    char c_func_name[128];
} JubeSysFuncRecord;

#ifndef SIMPLE_SCHEMA_PARSER
static JubeSysFuncRecord* jube_sys_func_records = NULL;
static int jube_sys_func_record_count = 0;
#endif

static uint64_t sys_func_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const SysFuncEntry* e = (const SysFuncEntry*)item;
    // hash the name and mix in arg_count
    uint64_t h = hashmap_xxhash3(e->name, e->name_len, seed0, seed1);
    h ^= (uint64_t)(e->arg_count + 2) * 0x9E3779B97F4A7C15ULL;  // +2 to keep -1 distinct
    return h;
}

static int sys_func_compare(const void* a, const void* b, void* udata) {
    const SysFuncEntry* ea = (const SysFuncEntry*)a;
    const SysFuncEntry* eb = (const SysFuncEntry*)b;
    if (ea->arg_count != eb->arg_count) return ea->arg_count - eb->arg_count;
    if (ea->name_len != eb->name_len) return ea->name_len - eb->name_len;
    return strncmp(ea->name, eb->name, ea->name_len);
}

static uint64_t sys_func_name_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const SysFuncNameEntry* e = (const SysFuncNameEntry*)item;
    return hashmap_xxhash3(e->name, e->name_len, seed0, seed1);
}

static int sys_func_name_compare(const void* a, const void* b, void* udata) {
    const SysFuncNameEntry* ea = (const SysFuncNameEntry*)a;
    const SysFuncNameEntry* eb = (const SysFuncNameEntry*)b;
    if (ea->name_len != eb->name_len) return ea->name_len - eb->name_len;
    return strncmp(ea->name, eb->name, ea->name_len);
}

static void register_sys_func_info(SysFuncInfo* info) {
    if (!info || !info->name) return;
    int name_len = (int)strlen(info->name);

    SysFuncEntry entry = {
        .name = info->name,
        .name_len = name_len,
        .arg_count = info->arg_count,
        .info = info
    };
    hashmap_set(sys_func_map, &entry);

    SysFuncNameEntry name_entry = {
        .name = info->name,
        .name_len = name_len
    };
    hashmap_set(sys_func_name_set, &name_entry);
}

#ifndef SIMPLE_SCHEMA_PARSER
static int jube_signature_arg_count(const char* signature) {
    if (!signature) return -1;
    const char* open = strchr(signature, '(');
    const char* close = open ? strchr(open, ')') : NULL;
    if (!open || !close || close < open) return -1;

    const char* p = open + 1;
    while (p < close && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    if (p >= close) return 0;

    int count = 1;
    while (p < close) {
        if (*p == ',') count++;
        p++;
    }
    return count;
}

static bool jube_type_name_matches(const char* text, const char* name) {
    size_t name_len = strlen(name);
    if (strncmp(text, name, name_len) != 0) return false;
    char next = text[name_len];
    return next == '\0' || next == ' ' || next == '\t' || next == '\n' ||
        next == '\r' || next == ',' || next == ')';
}

static TypeId jube_signature_type_id(const char* text) {
    if (!text) return LMD_TYPE_ANY;
    while (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r') text++;

    if (jube_type_name_matches(text, "null")) return LMD_TYPE_NULL;
    if (jube_type_name_matches(text, "bool")) return LMD_TYPE_BOOL;
    if (jube_type_name_matches(text, "int64")) return LMD_TYPE_INT64;
    if (jube_type_name_matches(text, "int")) return LMD_TYPE_INT;
    if (jube_type_name_matches(text, "float")) return LMD_TYPE_FLOAT;
    if (jube_type_name_matches(text, "complex")) return LMD_TYPE_COMPLEX;
    if (jube_type_name_matches(text, "string")) return LMD_TYPE_STRING;

    int module_count = jube_static_module_count();
    for (int i = 0; i < module_count; i++) {
        const JubeModuleDef* module = jube_static_module_at(i);
        if (!module || !module->types || module->type_count <= 0) continue;
        for (int j = 0; j < module->type_count; j++) {
            const JubeTypeDef* type = &module->types[j];
            if (type->name && jube_type_name_matches(text, type->name)) {
                return LMD_TYPE_VMAP;
            }
        }
    }
    return LMD_TYPE_ANY;
}

static TypeId jube_signature_first_param_type_id(const char* signature) {
    if (!signature) return LMD_TYPE_ANY;
    const char* open = strchr(signature, '(');
    const char* close = open ? strchr(open, ')') : NULL;
    if (!open || !close || close < open) return LMD_TYPE_ANY;

    const char* p = open + 1;
    while (p < close && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    if (p >= close) return LMD_TYPE_ANY;

    const char* colon = p;
    while (colon < close && *colon != ':' && *colon != ',') colon++;
    if (colon >= close || *colon != ':') return LMD_TYPE_ANY;
    return jube_signature_type_id(colon + 1);
}

static Type* jube_type_from_type_id(TypeId type_id) {
    switch (type_id) {
    case LMD_TYPE_NULL: return &TYPE_NULL;
    case LMD_TYPE_BOOL: return &TYPE_BOOL;
    case LMD_TYPE_INT: return &TYPE_INT;
    case LMD_TYPE_INT64: return &TYPE_INT64;
    case LMD_TYPE_FLOAT: return &TYPE_FLOAT;
    case LMD_TYPE_COMPLEX: return &TYPE_COMPLEX;
    case LMD_TYPE_STRING: return &TYPE_STRING;
    default: return &TYPE_ANY;
    }
}

static Type* jube_signature_return_type(const char* signature) {
    if (!signature) return &TYPE_ANY;
    const char* arrow = strstr(signature, "->");
    if (!arrow) return &TYPE_ANY;
    return jube_type_from_type_id(jube_signature_type_id(arrow + 2));
}

static bool jube_extract_native_c_name(const char* native_signature, char* out, size_t out_size) {
    if (!native_signature || !out || out_size == 0) return false;
    const char* open = strchr(native_signature, '(');
    if (!open) return false;

    const char* end = open;
    while (end > native_signature && (*(end - 1) == ' ' || *(end - 1) == '\t')) end--;
    const char* start = end;
    while (start > native_signature) {
        char ch = *(start - 1);
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '_') {
            start--;
        } else {
            break;
        }
    }
    if (start == end) return false;

    size_t len = (size_t)(end - start);
    if (len >= out_size) len = out_size - 1;
    memcpy(out, start, len);
    out[len] = '\0';
    return true;
}

static int jube_count_module_functions(void) {
    jube_register_builtin_modules();

    int count = 0;
    int module_count = jube_static_module_count();
    for (int i = 0; i < module_count; i++) {
        const JubeModuleDef* module = jube_static_module_at(i);
        if (!module || !module->functions || module->function_count <= 0) continue;
        count += module->function_count;
    }
    return count;
}

static void register_jube_sys_funcs(void) {
    if (jube_sys_func_records) return;

    int total = jube_count_module_functions();
    if (total <= 0) return;

    jube_sys_func_records = (JubeSysFuncRecord*)calloc((size_t)total, sizeof(JubeSysFuncRecord));
    if (!jube_sys_func_records) {
        log_error("JUBE_AST: failed to allocate %d descriptor sys-func records", total);
        return;
    }

    int out = 0;
    int module_count = jube_static_module_count();
    for (int i = 0; i < module_count; i++) {
        const JubeModuleDef* module = jube_static_module_at(i);
        if (!module || !module->name || !module->functions || module->function_count <= 0) continue;
        for (int j = 0; j < module->function_count; j++) {
            const JubeFuncDef* fn = &module->functions[j];
            if (!fn->name || (!fn->native_func && !fn->func)) continue;

            JubeSysFuncRecord* record = &jube_sys_func_records[out++];
            snprintf(record->name, sizeof(record->name), "%s_%s", module->name, fn->name);
            if (!jube_extract_native_c_name(fn->native_signature,
                    record->c_func_name, sizeof(record->c_func_name))) {
                snprintf(record->c_func_name, sizeof(record->c_func_name), "%s", record->name);
            }

            record->info.fn = SYSFUNC_JUBE_MODULE;
            record->info.name = record->name;
            record->info.arg_count = jube_signature_arg_count(fn->signature);
            record->info.return_type = jube_signature_return_type(fn->signature);
            record->info.is_proc = false;
            record->info.is_overloaded = false;
            record->info.is_method_eligible = (fn->flags & JUBE_FN_METHOD_ELIGIBLE) != 0;
            record->info.first_param_type = jube_signature_first_param_type_id(fn->signature);
            record->info.can_raise = false;
            record->info.c_ret_type = C_RET_ITEM;
            record->info.c_arg_conv = C_ARG_ITEM;
            record->info.c_func_name = record->c_func_name;
            record->info.func_ptr = fn->native_func ? fn->native_func : fn->func;
            record->info.native_c_name = NULL;
            record->info.native_func_ptr = NULL;
            record->info.native_returns_float = false;
            record->info.native_arg_count = 0;
            record->info.success_type = record->info.return_type;
            record->info.may_return_error = false;

            // Descriptor functions are registered as module-prefixed sys funcs
            // so legacy MIR call lowering sees the same metadata shape.
            register_sys_func_info(&record->info);
        }
    }
    jube_sys_func_record_count = out;
}
#endif

static void init_sys_func_maps() {
    if (sys_func_map) return;  // already initialized

    const size_t count = (size_t)sys_func_def_count;

    sys_func_map = hashmap_new(sizeof(SysFuncEntry), count * 2,
        0, 0, sys_func_hash, sys_func_compare, NULL, NULL);

    sys_func_name_set = hashmap_new(sizeof(SysFuncNameEntry), count * 2,
        0, 0, sys_func_name_hash, sys_func_name_compare, NULL, NULL);

    for (size_t i = 0; i < count; i++) {
        register_sys_func_info(&sys_func_defs[i]);
    }

    int dynamic_count = 0;
#ifndef SIMPLE_SCHEMA_PARSER
    register_jube_sys_funcs();
    dynamic_count = jube_sys_func_record_count;
#endif

    // Release logging strips log_info arguments, so keep this counter observed under -Werror.
    (void)dynamic_count;
    log_info("sys_func maps initialized: %zu static entries, %d Jube entries, %zu unique names",
             count, dynamic_count, hashmap_count(sys_func_name_set));
}

void ensure_sys_func_maps_initialized() {
    init_sys_func_maps();
}

extern "C" fn_ptr find_dynamic_sys_func_import(const char* c_func_name) {
    if (!c_func_name) return NULL;
    init_sys_func_maps();
#ifndef SIMPLE_SCHEMA_PARSER
    for (int i = 0; i < jube_sys_func_record_count; i++) {
        SysFuncInfo* info = &jube_sys_func_records[i].info;
        if (info->c_func_name && strcmp(info->c_func_name, c_func_name) == 0) {
            return info->func_ptr;
        }
    }
#endif
    return NULL;
}

// Check if a name matches any system function (regardless of arg count)
// Returns true if the name is reserved for system functions
bool is_sys_func_name(const char* name, int name_len) {
    init_sys_func_maps();
    SysFuncNameEntry key = { .name = name, .name_len = name_len };
    return hashmap_get(sys_func_name_set, &key) != NULL;
}

SysFuncInfo* get_sys_func_info(StrView* name, int arg_count) {
    init_sys_func_maps();

    SysFuncEntry key = {
        .name = name->str,
        .name_len = (int)name->length,
        .arg_count = arg_count,
        .info = NULL
    };
    const SysFuncEntry* found = (const SysFuncEntry*)hashmap_get(sys_func_map, &key);
    if (found) {
        log_debug("is sys func: %.*s, %d", (int)name->length, name->str, arg_count);
        return found->info;
    }

    // fallback: match variadic functions (arg_count == -1)
    key.arg_count = -1;
    found = (const SysFuncEntry*)hashmap_get(sys_func_map, &key);
    if (found) {
        log_debug("is sys func (variadic): %.*s, %d", (int)name->length, name->str, arg_count);
        return found->info;
    }

    log_debug("don't have sys func: %.*s, %d", (int)name->length, name->str, arg_count);
    return NULL;
}

static SysFuncInfo* get_unambiguous_sys_func_value(StrView* name) {
    init_sys_func_maps();

    SysFuncInfo* found = NULL;
    for (int i = 0; i < sys_func_def_count; i++) {
        int name_len = (int)strlen(sys_func_defs[i].name);
        if (name_len != (int)name->length || strncmp(sys_func_defs[i].name, name->str, name->length) != 0) {
            continue;
        }
        if (!sys_func_defs[i].func_ptr) return NULL;
        if (found) return NULL;
        found = &sys_func_defs[i];
    }
#ifndef SIMPLE_SCHEMA_PARSER
    for (int i = 0; i < jube_sys_func_record_count; i++) {
        SysFuncInfo* info = &jube_sys_func_records[i].info;
        int name_len = (int)strlen(info->name);
        if (name_len != (int)name->length || strncmp(info->name, name->str, name->length) != 0) {
            continue;
        }
        if (!info->func_ptr) return NULL;
        if (found) return NULL;
        found = info;
    }
#endif
    return found;
}

// Look up a system function for method-style call: obj.method(args)
// This searches for a sys func where arg_count includes the object (+1)
// and validates that the function is method-eligible and type-compatible
SysFuncInfo* get_sys_func_for_method(StrView* method_name, int method_arg_count, TypeId obj_type_id) {
    init_sys_func_maps();

    // method_arg_count is the count of arguments in parentheses (not including obj)
    // sys func arg_count includes the object, so we add 1
    int total_arg_count = method_arg_count + 1;

    SysFuncEntry key = {
        .name = method_name->str,
        .name_len = (int)method_name->length,
        .arg_count = total_arg_count,
        .info = NULL
    };
    const SysFuncEntry* found = (const SysFuncEntry*)hashmap_get(sys_func_map, &key);
    if (!found) {
        log_debug("method_call no sys func: %.*s, args=%d",
            (int)method_name->length, method_name->str, total_arg_count);
        return NULL;
    }

    SysFuncInfo* info = found->info;

    // Check if this function is method-eligible
    if (!info->is_method_eligible) {
        log_debug("method_call sys func '%.*s' not method-eligible",
            (int)method_name->length, method_name->str);
        return NULL;
    }

    // Check type compatibility with first parameter
    if (info->first_param_type != LMD_TYPE_ANY && obj_type_id != LMD_TYPE_ANY) {
        if (info->first_param_type != obj_type_id) {
            bool type_compatible = false;
            if (!type_compatible) {
                log_debug("method_call type mismatch for '%.*s': expected %d, got %d",
                    (int)method_name->length, method_name->str,
                    info->first_param_type, obj_type_id);
                return NULL;
            }
        }
    }

    log_debug("method_call found sys func: %.*s, args=%d",
        (int)method_name->length, method_name->str, total_arg_count);
    return info;
}


Type* unwrap_simple_type_type(Type* type) {
    while (type && type->type_id == LMD_TYPE_TYPE && !is_global_simple_type(type) &&
            type->kind == TYPE_KIND_SIMPLE) {
        // Global meta-types use the compact Type prefix. Only a compiler-built
        // TypeType owns the extended `kind` and nested-type fields below.
        TypeType* type_type = (TypeType*)type;
        if (!type_type->type) break;
        type = type_type->type;
    }
    return type;
}

static bool type_is_sized_integer(Type* type) {
    type = unwrap_simple_type_type(type);
    if (!type) return false;
    if (type->type_id == LMD_TYPE_UINT64) return true;
    if (type->type_id != LMD_TYPE_NUM_SIZED) return false;
    NumSizedType st = type_num_sized_kind(type);
    return st != NUM_FLOAT16 && st != NUM_FLOAT32;
}

static bool typed_array_element_compatible(Type* arg_elem, Type* expected_elem) {
    arg_elem = unwrap_simple_type_type(arg_elem);
    expected_elem = unwrap_simple_type_type(expected_elem);
    if (!arg_elem || !expected_elem) return true;

    TypeId arg_tid = arg_elem->type_id;
    if (arg_tid == LMD_TYPE_ANY || arg_tid == LMD_TYPE_TYPE) return true;
    LambdaNumericKind expected_numeric = lambda_numeric_kind_from_type(expected_elem);
    if (lambda_numeric_is_sized(expected_numeric)) {
        // A sized array annotation is an explicit destination conversion boundary,
        // just like an annotated scalar slot; its store performs the lane conversion.
        return lambda_numeric_kind_from_type(arg_elem) != LAMBDA_NUM_INVALID ||
               arg_tid == LMD_TYPE_BOOL;
    }
    switch (expected_elem->type_id) {
    case LMD_TYPE_INT:
        return is_integer_type_id(arg_tid) ||
               arg_tid == LMD_TYPE_BOOL || type_is_sized_integer(arg_elem);
    case LMD_TYPE_FLOAT:
        // Complex has no scalar float representation; accepting it here would
        // let a typed float array silently discard the imaginary component.
        return (arg_tid != LMD_TYPE_COMPLEX && is_numeric_type_id(arg_tid)) ||
               arg_tid == LMD_TYPE_BOOL;
    case LMD_TYPE_INT64:
    case LMD_TYPE_UINT64:
        return is_integer_type_id(arg_tid) ||
               arg_tid == LMD_TYPE_UINT64 || arg_tid == LMD_TYPE_BOOL ||
               type_is_sized_integer(arg_elem);
    default:
        return false;
    }
}

static bool ast_is_numeric_literal_syntax(AstNode* node) {
    while (node && node->node_type == AST_NODE_PRIMARY) {
        AstPrimaryNode* primary = (AstPrimaryNode*)node;
        if (!primary->expr) {
            return node->type && node->type->is_literal &&
                lambda_numeric_kind_from_type(node->type) != LAMBDA_NUM_INVALID;
        }
        // Identifier types can carry constant/literal metadata from their binding,
        // but the source expression itself is not a literal-zero diagnostic site.
        if (primary->expr->node_type == AST_NODE_IDENT) return false;
        node = primary->expr;
    }
    if (node && node->node_type == AST_NODE_UNARY) {
        AstUnaryNode* unary = (AstUnaryNode*)node;
        if (unary->op == OPERATOR_NEG || unary->op == OPERATOR_POS) {
            return ast_is_numeric_literal_syntax(unary->operand);
        }
    }
    return false;
}

static bool typed_array_annotation_compatible(Type* arg_type, Type* param_type) {
    arg_type = unwrap_simple_type_type(arg_type);
    param_type = unwrap_simple_type_type(param_type);
    if (!arg_type || !param_type || param_type->kind != TYPE_KIND_UNARY) return false;

    TypeUnary* unary = (TypeUnary*)param_type;
    Type* expected_elem = unwrap_simple_type_type(unary->operand);
    if (!expected_elem) return false;
    if (expected_elem->type_id != LMD_TYPE_INT &&
        expected_elem->type_id != LMD_TYPE_FLOAT &&
        expected_elem->type_id != LMD_TYPE_INT64 &&
        expected_elem->type_id != LMD_TYPE_UINT64) {
        return false;
    }

    if (arg_type->type_id == LMD_TYPE_ARRAY_NUM) return true;
    if (arg_type->type_id != LMD_TYPE_ARRAY) return false;

    TypeArray* arr_type = (TypeArray*)arg_type;
    if (!arr_type->nested) return arr_type->length == 0;
    return typed_array_element_compatible(arr_type->nested, expected_elem);
}

static Type* typed_array_expected_element(Type* param_type) {
    param_type = unwrap_simple_type_type(param_type);
    if (!param_type || param_type->kind != TYPE_KIND_UNARY) return NULL;

    TypeUnary* unary = (TypeUnary*)param_type;
    Type* expected_elem = unwrap_simple_type_type(unary->operand);
    if (!expected_elem) return NULL;
    if (expected_elem->type_id != LMD_TYPE_INT &&
        expected_elem->type_id != LMD_TYPE_FLOAT &&
        expected_elem->type_id != LMD_TYPE_INT64 &&
        expected_elem->type_id != LMD_TYPE_UINT64) {
        return NULL;
    }
    return expected_elem;
}

static bool typed_array_argument_compatible(AstNode* arg, Type* param_type) {
    if (!arg || !arg->type) return true;

    Type* expected_elem = typed_array_expected_element(param_type);
    if (!expected_elem) return false;

    Type* arg_type = unwrap_simple_type_type(arg->type);
    if (!arg_type) return true;
    if (arg_type->type_id == LMD_TYPE_ARRAY_NUM) return true;
    if (arg_type->type_id != LMD_TYPE_ARRAY) return false;

    TypeArray* arr_type = (TypeArray*)arg_type;
    if (arr_type->nested) {
        return typed_array_element_compatible(arr_type->nested, expected_elem);
    }

    if (arg->node_type != AST_NODE_ARRAY) {
        return true;
    }

    AstNode* item = ((AstArrayNode*)arg)->item;
    while (item) {
        if (item->node_type != AST_NODE_ASSIGN &&
            item->type && item->type->type_id != LMD_TYPE_ANY &&
            !typed_array_element_compatible(item->type, expected_elem)) {
            return false;
        }
        item = item->next;
    }
    return true;
}



bool is_global_simple_type(const Type* type) {
    return type_is_global_meta_type(type) || type == &TYPE_NULL || type == &TYPE_BOOL || type == &TYPE_INT ||
        type == &TYPE_INT64 || type == &TYPE_FLOAT || type == &TYPE_COMPLEX || type == &TYPE_DECIMAL ||
           type == &TYPE_INTEGER_VALUE || type == &TYPE_STRING || type == &TYPE_SYMBOL ||
           type == &TYPE_DTIME || type == &TYPE_DATE || type == &TYPE_TIME ||
           type == &TYPE_BINARY || type == &TYPE_RANGE || type == &TYPE_ARRAY ||
           type == &TYPE_MAP || type == &TYPE_ELMT || type == &TYPE_OBJECT ||
           type == &TYPE_FUNC || type == &TYPE_ANY ||
           type == &TYPE_ERROR || type == &TYPE_ANY_NO_ERROR ||
           type == &TYPE_ANY_NO_NULL || type == &TYPE_ANY_NO_ERROR_OR_NULL ||
           type == &TYPE_I8 || type == &TYPE_I16 ||
           type == &TYPE_I32 || type == &TYPE_U8 || type == &TYPE_U16 ||
           type == &TYPE_U32 || type == &TYPE_F16 || type == &TYPE_F32 ||
           type == &TYPE_UINT64;
}

static inline void set_param_contract(TypeParam* parameter, Type* contract,
        bool is_explicit) {
    parameter->contract_type = contract;
    parameter->has_explicit_contract = is_explicit;
}

static Type* parameter_contract_for_declared(Transpiler* tp, Type* declared,
        bool optional, AstNode* default_value) {
    if (optional && !default_value) {
        // An omitted typed optional is the language-level null value. Keep the
        // scalar payload for the native lane, but publish the nullable contract
        // so ABI padding decodes null instead of silently becoming zero (D2.5.1).
        return lambda_type_nullable_normalized(tp->pool, declared);
    }
    return declared;
}

static inline Type* parameter_boundary_type(TypeParam* parameter) {
    if (!parameter) return &TYPE_ANY;
    // TypeParam repurposes `kind` for the ABI, so its compact prefix loses a
    // sized numeric discriminator such as i32. Boundary checks must use the
    // retained source contract rather than treating every sized type alike.
    if (parameter->contract_type) return parameter->contract_type;
    return parameter->full_type ? parameter->full_type : (Type*)parameter;
}

static inline void set_function_return_contract(TypeFunc* function, Type* contract,
        bool is_explicit) {
    function->return_contract = contract;
    function->has_explicit_return_contract = is_explicit;
}

static Type* function_success_result_type(TypeFunc* function) {
    if (!function) return &TYPE_ANY;
    // A written result annotation is the caller-visible success domain. An
    // unannotated function can retain its narrower inferred body result.
    if (function->has_explicit_return_contract && function->returned) {
        return function->returned;
    }
    return function->inferred_return ? function->inferred_return :
        function->returned ? function->returned : &TYPE_ANY;
}

static Type* function_call_result_type(Transpiler* tp, TypeFunc* function) {
    Type* success = function_success_result_type(function);
    if (!function || (!function->can_raise && !function->may_return_error)) {
        return success;
    }
    Type* error = function->error_type ? function->error_type : &TYPE_ERROR;
    return lambda_type_union_normalized(tp->pool, success, error);
}


static Type* known_array_element_type(Type* type);
static bool is_magnitude_numeric_type(TypeId type_id);

static Type* sys_func_success_result_type(Transpiler* tp, SysFuncInfo* info,
        AstNode* first_arg) {
    Type* success = info && info->success_type ? info->success_type :
        info ? info->return_type : NULL;
    if (!info) return success ? success : &TYPE_ANY;

    // Search and ordinal operations have one optional scalar result: a valid
    // non-negative integer or no value. Their former -1 sentinel made the
    // public type look total and prevented Lambda's `value or default` idiom.
    switch (info->fn) {
    case SYSFUNC_INDEX_OF:
    case SYSFUNC_LAST_INDEX_OF:
    case SYSFUNC_ORD:
        return lambda_type_nullable_normalized(tp->pool,
            success ? success : (Type*)&TYPE_INT);
    default:
        break;
    }

    if (!first_arg || !first_arg->type) return success ? success : &TYPE_ANY;

    // Element-preserving and carrier-preserving rows derive their success type
    // from the first argument [TI4]. This replaced a per-function switch: at
    // the third near-identical case the shape belongs in the registry, not in
    // a growing list of SYSFUNC_ labels here.
    Type* arg0 = first_arg->type;
    switch (info->result_kind) {
    case SYS_RESULT_SAME_AS_ARG0:
        // A proven concrete argument keeps its carrier through the operation;
        // an open argument leaves the registry's declared type in force.
        // Literal-ness must NOT ride along: `slice("hello", 2)` is not the
        // literal `"hello"`, and handing back the literal type let consumers
        // treat the runtime result as a compile-time constant (observed as a
        // raw String* printed as a number, and `inf`, in slice_two_arg).
        if (arg0->type_id != LMD_TYPE_ANY) {
            if (arg0->is_literal || arg0->is_const) {
                return alloc_type(tp->pool, arg0->type_id, sizeof(Type));
            }
            return arg0;
        }
        break;
    case SYS_RESULT_ARG0_NUMERIC:
        if (lambda_numeric_kind_from_type(arg0) != LAMBDA_NUM_INVALID) return arg0;
        break;
    case SYS_RESULT_TEXT_SAME_AS_ARG0:
        if (arg0->type_id == LMD_TYPE_STRING || arg0->type_id == LMD_TYPE_SYMBOL) {
            return arg0;
        }
        break;
    case SYS_RESULT_REAL_TO_FLOAT:
        // Complex and vector arguments keep the row's open type: these builtins
        // are polymorphic and return the argument's own shape for them.
        if (arg0->type_id != LMD_TYPE_COMPLEX &&
                is_magnitude_numeric_type(arg0->type_id)) {
            return &TYPE_FLOAT;
        }
        break;
    case SYS_RESULT_ELEM_OF_ARG0: {
        Type* elem = known_array_element_type(arg0);
        if (elem && elem != arg0 && elem->type_id != LMD_TYPE_ANY) return elem;
        break;
    }
    case SYS_RESULT_ARRAY_OF_ARG0_ELEM: {
        Type* elem = known_array_element_type(arg0);
        if (elem && elem->type_id != LMD_TYPE_ANY) {
            TypeArray* out = (TypeArray*)alloc_type(tp->pool, LMD_TYPE_ARRAY,
                sizeof(TypeArray));
            out->nested = elem;
            out->type_index = -1;
            return (Type*)out;
        }
        break;
    }
    case SYS_RESULT_FIXED:
    default:
        break;
    }
    return success ? success : &TYPE_ANY;
}

static Type* sys_func_call_result_type(Transpiler* tp, SysFuncInfo* info,
        bool may_return_error, AstNode* first_arg) {
    if (!info) return set_type_any(tp, ANY_ERROR_RECOVERY);
    Type* success = sys_func_success_result_type(tp, info, first_arg);
    // A row with no precise success type is the TIG4 gap, not a property of
    // the call site — census it here so IP2's row sweep has a metric.
    if (success == &TYPE_ANY) set_type_any(tp, ANY_SYSFUNC_ROW);
    return may_return_error
        ? lambda_type_union_normalized(tp->pool, success, &TYPE_ERROR) : success;
}

bool ast_static_literal_item(Transpiler* tp, AstNode* node, Item* out);
static bool sys_conversion_literal_is_error_free(Transpiler* tp, SysFuncInfo* info,
        AstNode* first_arg);
static bool ast_is_explicit_type_value(AstNode* node);

static bool sys_conversion_has_error_free_numeric_input(Type* type) {
    // Keep this proof deliberately narrower than the abstract `number` type:
    // it mirrors the concrete fn_int/fn_float/fn_decimal switch arms that never
    // reject a value. A union/error or a broad numeric abstraction retains the
    // registry effect and therefore its checked Item result lane.
    if (!type || lambda_type_accepts_error(type)) return false;
    switch (type->type_id) {
    case LMD_TYPE_INT:
    case LMD_TYPE_INT64:
    case LMD_TYPE_UINT64:
    case LMD_TYPE_FLOAT:
    case LMD_TYPE_NUM_SIZED:
        return true;
    default:
        return false;
    }
}

static bool sys_func_call_may_return_error(Transpiler* tp, SysFuncInfo* info,
        AstNode* first_arg) {
    if (!info || !info->may_return_error) return false;
    if (!first_arg || !first_arg->type) return true;
    if (sys_conversion_literal_is_error_free(tp, info, first_arg)) return false;

    switch (info->fn) {
    case SYSFUNC_INT:
    case SYSFUNC_FLOAT:
    case SYSFUNC_DECIMAL:
        // Conversion effects depend on the source domain. For these concrete
        // numeric input lanes the runtime conversion has no rejection branch;
        // string/opaque inputs remain the registry-declared error-producing
        // calls that `or` must contain.
        return !sys_conversion_has_error_free_numeric_input(first_arg->type);
    case SYSFUNC_INT64:
        // int64(float) can signal its sentinel for an out-of-range value, so
        // only integral machine lanes prove this native conversion clean.
        return first_arg->type->type_id != LMD_TYPE_INT &&
            first_arg->type->type_id != LMD_TYPE_INT64 &&
            first_arg->type->type_id != LMD_TYPE_UINT64 &&
            first_arg->type->type_id != LMD_TYPE_NUM_SIZED;
    case SYSFUNC_BINARY:
        return first_arg->type->type_id != LMD_TYPE_BINARY;
    default:
        return true;
    }
}

static bool is_complex_component_type(TypeId type_id) {
    // Complex promotion is intentionally limited to binary64-preserving lanes;
    // exact decimal and full-width integer values require an explicit float().
    return type_id == LMD_TYPE_INT || type_id == LMD_TYPE_FLOAT ||
           type_id == LMD_TYPE_NUM_SIZED;
}

static inline bool is_param_full_type_id(TypeId type_id) {
    return type_id == LMD_TYPE_MAP || type_id == LMD_TYPE_OBJECT ||
           type_id == LMD_TYPE_ELEMENT;
}



static bool types_compatible_with_full(Type* arg_type, Type* param_type, Type* param_full_type) {
    if (!arg_type || !param_type) return true;  // unknown types are compatible
    if (param_type->type_id == LMD_TYPE_ANY) return true;  // any accepts all
    if (arg_type->type_id == LMD_TYPE_ANY) return true;  // any arg can pass to typed param (runtime check)
    LambdaNumericKind arg_numeric = lambda_numeric_kind_from_type(arg_type);
    LambdaNumericKind param_numeric = lambda_numeric_kind_from_type(param_type);
    if (arg_numeric != LAMBDA_NUM_INVALID && param_numeric != LAMBDA_NUM_INVALID) {
        return lambda_numeric_kind_exactly_embeds(arg_numeric, param_numeric);
    }
    if (arg_type->type_id == param_type->type_id) return true;

    // handle union types (e.g., T | error from T^ syntax)
    // if param is a union type, check if arg matches either side
    // for TypeParam with complex types, use full_type if available
    Type* actual_param = param_full_type ? param_full_type : param_type;

    bool can_read_extended_kind = actual_param && !is_global_simple_type(actual_param);

    if (can_read_extended_kind && is_array_family_type_id(actual_param->type_id) &&
        typed_array_annotation_compatible(arg_type, actual_param)) {
        return true;
    }

    if (can_read_extended_kind && actual_param->kind == TYPE_KIND_BINARY) {
        TypeBinary* union_type = (TypeBinary*)actual_param;
        if (union_type->op == OPERATOR_UNION) {
            // unwrap TypeType if present
            Type* left = unwrap_simple_type_type(union_type->left);
            Type* right = unwrap_simple_type_type(union_type->right);
            // arg matches if compatible with either side of the union
            if (types_compatible_with_full(arg_type, left, NULL) ||
                types_compatible_with_full(arg_type, right, NULL)) {
                return true;
            }
        }
    }

    if (param_type == &TYPE_NUMBER) {
        // `number` is an abstract type keyword; no runtime TypeId carries it.
        if (IS_NUMERIC_ID(arg_type->type_id)) return true;
    }
    return false;
}

bool ast_static_literal_item(Transpiler* tp, AstNode* node, Item* out) {
    if (!tp || !node || !out) return false;
    while (node && node->node_type == AST_NODE_PRIMARY) {
        AstPrimaryNode* primary = (AstPrimaryNode*)node;
        if (!primary->expr) break;
        node = primary->expr;
    }
    if (!node || !node->type || !node->type->is_literal) return false;

    if (static_literal_item_from_type(node->type, out)) return true;

    switch (node->type->type_id) {
    case LMD_TYPE_BOOL: {
        StrView text = ast_node_source(tp, node);
        out->item = b2it(strview_equal(&text, "true") ? BOOL_TRUE : BOOL_FALSE);
        return true;
    }
    case LMD_TYPE_INT: {
        // a value-bearing pooled int (hand-parsed patterns) carries its payload;
        // only the shared &LIT_INT type requires re-reading the source span —
        // the same dichotomy the transpiler's literal emitter uses
        if (node->type != (Type*)&LIT_INT) {
            out->item = i2it(((TypeInt64*)node->type)->int64_val);
            return true;
        }
        StrView source = ast_node_source(tp, node);
        char* num_str = ast_copy_source_text(tp, source, node->source_span);
        if (!num_str) return false;
        int64_t value = 0;
        lambda_parse_int_literal(num_str, &value);
        mem_free(num_str);
        out->item = i2it(value);
        return true;
    }
    case LMD_TYPE_DTIME:
        // Static containers cannot own a GC datetime.  Leave this expression
        // dynamic so its runtime path materializes a traced heap object.
        return false;
    default:
        return false;
    }
}

static bool sys_conversion_literal_is_error_free(Transpiler* tp, SysFuncInfo* info,
        AstNode* first_arg) {
    if (!info || !first_arg || !first_arg->type || !first_arg->type->is_literal) {
        return false;
    }

    // AST construction has no active heap context. Do not execute allocation-
    // capable conversion helpers here just to refine an effect; concrete
    // numeric literals already prove their conversion cannot return error.
    if (first_arg->type->type_id == LMD_TYPE_DECIMAL) {
        if (info->fn == SYSFUNC_FLOAT || info->fn == SYSFUNC_DECIMAL) return true;
        if (info->fn == SYSFUNC_INT) {
            Item literal;
            int64_t value = 0;
            // decimal_to_int64_exact is a read-only range proof; unlike
            // fn_int it does not materialize a temporary decimal on the
            // unavailable AST-build heap.
            return ast_static_literal_item(tp, first_arg, &literal) &&
                decimal_to_int64_exact(literal, &value);
        }
        return false;
    }
    if (!sys_conversion_has_error_free_numeric_input(first_arg->type)) {
        return false;
    }
    switch (info->fn) {
    case SYSFUNC_INT:
    case SYSFUNC_FLOAT:
    case SYSFUNC_DECIMAL:
        return true;
    default:
        return false;
    }
}

static bool ast_static_numeric_literal_is_zero(Transpiler* tp, AstNode* node) {
    if (!ast_is_numeric_literal_syntax(node)) return false;
    while (node && node->node_type == AST_NODE_PRIMARY &&
            ((AstPrimaryNode*)node)->expr) {
        node = ((AstPrimaryNode*)node)->expr;
    }
    if (node && node->node_type == AST_NODE_UNARY) {
        node = ((AstUnaryNode*)node)->operand;
    }
    Item item;
    if (!ast_static_literal_item(tp, node, &item)) return false;
    switch (get_type_id(item)) {
    case LMD_TYPE_INT: return lambda_int_item_to_i64(item) == 0;
    case LMD_TYPE_INT64: return item.get_int64() == 0;
    case LMD_TYPE_UINT64: return item.get_uint64() == 0;
    case LMD_TYPE_FLOAT: return item.get_double() == 0.0;
    case LMD_TYPE_NUM_SIZED:
        return item.get_num_type() == NUM_FLOAT16 || item.get_num_type() == NUM_FLOAT32 ?
            item.get_num_sized_as_double() == 0.0 :
            item.get_num_sized_as_int64() == 0;
    case LMD_TYPE_DECIMAL: return decimal_item_is_zero(item);
    default: return false;
    }
}

static Type* ast_called_type_target(AstNode* function) {
    while (function && function->node_type == AST_NODE_PRIMARY) {
        AstPrimaryNode* primary = (AstPrimaryNode*)function;
        if (!primary->expr) break;
        function = primary->expr;
    }
    if (!function || !function->type) return NULL;
    // Built-in annotations (`i32`, `u64`, ...) are represented directly by
    // AST_NODE_TYPE with their target type.  Named type values retain the
    // TypeType wrapper. Accept both forms so direct reductions enter the same
    // conversion lowering lane.
    if (function->type->type_id != LMD_TYPE_TYPE) {
        return function->node_type == AST_NODE_TYPE ? function->type : NULL;
    }
    Type* target = unwrap_simple_type_type(function->type);
    return target != function->type ? target : NULL;
}

static bool ast_called_function_signature_ready(AstNode* function) {
    while (function && function->node_type == AST_NODE_PRIMARY) {
        AstPrimaryNode* primary = (AstPrimaryNode*)function;
        if (!primary->expr) break;
        function = primary->expr;
    }
    if (!function || function->node_type != AST_NODE_IDENT) return true;
    AstIdentNode* ident = (AstIdentNode*)function;
    AstNode* declaration = ident->entry ? ident->entry->node : NULL;
    if (!declaration || (declaration->node_type != AST_NODE_FUNC &&
            declaration->node_type != AST_NODE_PROC)) return true;
    // Top-level pass one publishes a function name before its parameters are
    // built.  That placeholder has a zero count, not a zero-argument contract.
    return ((AstFuncNode*)declaration)->body != NULL;
}

static bool ast_constant_integer_value(Transpiler* tp, AstNode* node, int64_t* out) {
    bool negate = false;
    while (node && node->node_type == AST_NODE_PRIMARY) {
        AstPrimaryNode* primary = (AstPrimaryNode*)node;
        if (!primary->expr) break;
        node = primary->expr;
    }
    if (node && node->node_type == AST_NODE_UNARY) {
        AstUnaryNode* unary = (AstUnaryNode*)node;
        if (unary->op == OPERATOR_NEG || unary->op == OPERATOR_POS) {
            negate = (unary->op == OPERATOR_NEG);
            node = unary->operand;
        }
    }
    Item item;
    if (!ast_static_literal_item(tp, node, &item)) return false;
    TypeId type_id = get_type_id(item);
    int64_t value = 0;
    if (type_id == LMD_TYPE_INT) {
        value = lambda_int_item_to_i64(item);
    } else if (type_id == LMD_TYPE_INT64) {
        value = item.get_int64();
    } else if (type_id == LMD_TYPE_UINT64) {
        uint64_t u = item.get_uint64();
        if (u > (uint64_t)INT64_MAX) return false;
        value = (int64_t)u;
    } else if (type_id == LMD_TYPE_NUM_SIZED) {
        NumSizedType st = item.get_num_type();
        if (st == NUM_FLOAT16 || st == NUM_FLOAT32) return false;
        value = item.get_num_sized_as_int64();
    } else {
        return false;
    }
    *out = negate ? -value : value;
    return true;
}

static bool constant_fits_sized_integer(NumSizedType num_type, int64_t value) {
    switch (num_type) {
    case NUM_INT8:   return value >= INT8_MIN && value <= INT8_MAX;
    case NUM_INT16:  return value >= INT16_MIN && value <= INT16_MAX;
    case NUM_INT32:  return value >= INT32_MIN && value <= INT32_MAX;
    case NUM_UINT8:  return value >= 0 && value <= UINT8_MAX;
    case NUM_UINT16: return value >= 0 && value <= UINT16_MAX;
    case NUM_UINT32: return value >= 0 && (uint64_t)value <= UINT32_MAX;
    default:         return true;
    }
}

// check if arg_type is compatible with param_type for function calls
bool types_compatible(Type* arg_type, Type* param_type) {
    return types_compatible_with_full(arg_type, param_type, NULL);
}

static Type* infer_bitwise_call_type(SysFunc fn, AstNode* first_arg, AstNode* second_arg) {
    Type* left = first_arg ? first_arg->type : NULL;
    Type* right = second_arg ? second_arg->type : NULL;
    switch (fn) {
    case SYSFUNC_BAND:
    case SYSFUNC_BOR:
    case SYSFUNC_BXOR: {
        LambdaNumericDecision decision = lambda_numeric_classify(
            LAMBDA_NUM_OP_BITWISE, lambda_numeric_kind_from_type(left),
            lambda_numeric_kind_from_type(right));
        return decision.valid ? lambda_numeric_type_from_kind(decision.result) : NULL;
    }
    case SYSFUNC_BNOT: {
        LambdaNumericKind kind = lambda_numeric_kind_from_type(left);
        if (lambda_numeric_is_sized_integer(kind)) return left;
        if (kind == LAMBDA_NUM_INT || kind == LAMBDA_NUM_INTEGER) {
            return lambda_numeric_type_from_kind(kind);
        }
        return NULL;
    }
    case SYSFUNC_SHL:
    case SYSFUNC_SHR: {
        LambdaNumericDecision decision = lambda_numeric_classify(
            LAMBDA_NUM_OP_SHIFT, lambda_numeric_kind_from_type(left),
            lambda_numeric_kind_from_type(right));
        return decision.valid ? lambda_numeric_type_from_kind(decision.result) : NULL;
    }
    case SYSFUNC_USHR: {
        LambdaNumericKind kind = lambda_numeric_kind_from_type(left);
        if (lambda_numeric_is_sized_integer(kind)) {
            return lambda_numeric_type_from_kind(lambda_numeric_sized_kind(
                1, lambda_numeric_sized_bits(kind)));
        }
        // Lambda's unsized int follows the documented ToUint32 lane.
        return kind == LAMBDA_NUM_INT ? &TYPE_U32 : NULL;
    }
    default:
        return NULL;
    }
}

// Assign `any` and record WHY [Type_Infer TI3]. Never bare-assign
// `node->type = &TYPE_ANY` in this file: the census is how later inference
// slices prove their effect, and an unclassified site hides a gap.
Type* set_type_any(Transpiler* tp, AnyReason reason) {
    if (tp && reason >= 0 && reason < ANY_REASON_COUNT) tp->any_census[reason]++;
    return &TYPE_ANY;
}

// Same for the literal-typed variant used by type-annotation positions.
Type* set_lit_type_any(Transpiler* tp, AnyReason reason) {
    if (tp && reason >= 0 && reason < ANY_REASON_COUNT) tp->any_census[reason]++;
    return (Type*)&LIT_TYPE_ANY;
}

// Operator paths pick a TypeId first and allocate the Type afterwards, so they
// record through the id rather than through a Type* [Type_Infer TI3].
TypeId census_any_type_id(Transpiler* tp, AnyReason reason) {
    if (tp && reason >= 0 && reason < ANY_REASON_COUNT) tp->any_census[reason]++;
    return LMD_TYPE_ANY;
}

// relaxed mode (--static-warning): a semantic (E2xx) diagnostic is stored as
// a warning and does not fail compilation — the script still runs and may
// produce a result containing error values (SI3v2/TI6 per-surface policy).
// Parse/syntax failures never reach here, so they are never downgraded.
static bool record_as_static_warning(Transpiler* tp, LambdaErrorCode code,
        LambdaError* error) {
    if (!tp->static_warning || !ERR_IS_SEMANTIC(code)) return false;
    tp->warning_count++;
    if (!tp->warnings) tp->warnings = arraylist_new(8);
    arraylist_append(tp->warnings, error);
    return true;
}

// Record a typed semantic error and check if we should continue transpiling.
// Boundary sites select the specific public diagnostic code; E201 remains the
// generic declaration/assignment mismatch.
static void record_type_error_code(Transpiler* tp, int line, LambdaErrorCode code,
        const char* format, ...) {
    // Format error message
    char error_msg[512];
    va_list args;
    va_start(args, format);
    vsnprintf(error_msg, sizeof(error_msg), format, args);
    va_end(args);

    // Create structured error
    SourceLocation loc = src_loc(tp->reference, line, 1);
    loc.source = tp->source;
    LambdaError* error = err_create(code, error_msg, &loc);

    if (record_as_static_warning(tp, code, error)) {
        log_warn("static_warning[E%d] (line %d): %s", code, line, error_msg);
        return;
    }
    tp->error_count++;

    // Store in error list if available
    if (tp->errors) {
        arraylist_append(tp->errors, error);
    }

    // Also log for backward compatibility
    log_error("type_error[E%d] (line %d): %s", code, line, error_msg);

    // Check threshold
    if (tp->error_count >= tp->max_errors) {
        log_error("error_threshold: max errors (%d) reached", tp->max_errors);
    }
}

// Record a generic type mismatch. Keep this entry point for existing callers
// that do not describe a more specific source boundary.
void record_type_error(Transpiler* tp, int line, const char* format, ...) {
    va_list args;
    va_start(args, format);
    char error_msg[512];
    vsnprintf(error_msg, sizeof(error_msg), format, args);
    va_end(args);
    record_type_error_code(tp, line, ERR_TYPE_MISMATCH, "%s", error_msg);
}

static void record_semantic_error_message(Transpiler* tp, SourceSpan span,
        LambdaErrorCode code, const char* error_msg) {
    LambdaSourcePoint start = lambda_source_span_start_point(tp->source, span);
    LambdaSourcePoint end = lambda_source_span_end_point(tp->source, span);

    // Create structured error with span
    SourceLocation loc = src_loc_span(tp->reference,
        start.row + 1, start.column + 1,
        end.row + 1, end.column + 1);
    loc.source = tp->source;
    LambdaError* error = err_create(code, error_msg, &loc);

    if (record_as_static_warning(tp, code, error)) {
        log_warn("static_warning[E%d] at %s:%u:%u: %s", code,
            tp->reference ? tp->reference : "<unknown>",
            start.row + 1, start.column + 1, error_msg);
        return;
    }
    tp->error_count++;

    // Store in error list if available
    if (tp->errors) {
        arraylist_append(tp->errors, error);
    }

    // Also log for backward compatibility
    log_error("error[E%d] at %s:%u:%u: %s", code,
        tp->reference ? tp->reference : "<unknown>",
        start.row + 1, start.column + 1, error_msg);

    // Check threshold
    if (tp->error_count >= tp->max_errors) {
        log_error("error_threshold: max errors (%d) reached", tp->max_errors);
    }
}

// Record a semantic error against a source span.


void record_semantic_error_span(Transpiler* tp, SourceSpan span,
        LambdaErrorCode code, const char* format, ...) {
    char error_msg[512];
    va_list args;
    va_start(args, format);
    vsnprintf(error_msg, sizeof(error_msg), format, args);
    va_end(args);
    record_semantic_error_message(tp, span, code, error_msg);
}

// Check if should continue transpiling based on error count
bool should_continue_transpiling(Transpiler* tp) {
    return tp->error_count < tp->max_errors;
}

static Type* boundary_unwrap_type(Type* type) {
    type = unwrap_simple_type_type(type);
    if (type && !is_global_simple_type(type) && type->kind == TYPE_KIND_CONSTRAINED) {
        TypeConstrained* constrained = (TypeConstrained*)type;
        type = unwrap_simple_type_type(constrained->base);
    }
    return type;
}

static bool boundary_type_is_extended(const Type* type, TypeKind kind) {
    return type && !is_global_simple_type(type) && type->kind == kind;
}

// This is the static half of an annotated boundary.  It intentionally treats
// `any` and an unproven map shape as deferred rather than silently accepting
// them: the MIR/runtime boundary supplies the corresponding dynamic check.
// C16: within the numeric tower admission is decided by MEMBERSHIP, not by the
// static type. `int` is the float64-representable integers -- a subset of
// float, a superset of i32 -- so a value typed float can still be an int
// (`(a + 2) / 3 * 4` is typed float because `/` is, yet every value it produces
// there is one), and a value typed int can still be an i32. Neither direction
// is statically refutable, so the established runtime boundary owns the check
// and its soft out-of-range error. Widenings that ARE provable fall through to
// types_compatible_with_full and stay PROVEN.
bool boundary_numeric_admission_is_dynamic(TypeId source_id, TypeId target_id) {
    if (source_id == target_id) return false;
    switch (target_id) {
    case LMD_TYPE_INT: case LMD_TYPE_INT64:
    case LMD_TYPE_UINT64: case LMD_TYPE_NUM_SIZED:
        break;
    default:
        return false;
    }
    switch (source_id) {
    case LMD_TYPE_INT: case LMD_TYPE_INT64:
    case LMD_TYPE_UINT64: case LMD_TYPE_NUM_SIZED:
    case LMD_TYPE_FLOAT:
        return true;
    default:
        return false;
    }
}

static StaticBoundaryResult static_boundary_relation(Type* source, Type* target) {
    source = boundary_unwrap_type(source);
    target = boundary_unwrap_type(target);
    if (!source || !target || (source->type_id == LMD_TYPE_ANY &&
            !type_is_any_without_error(source) && !type_is_any_without_null(source))) {
        return STATIC_BOUNDARY_DEFERRED;
    }
    if (target->type_id == LMD_TYPE_ANY) {
        if (target == &TYPE_ANY) return STATIC_BOUNDARY_PROVEN;
        if (type_is_any_without_error(target) && lambda_type_accepts_error(source)) {
            return STATIC_BOUNDARY_REJECTED;
        }
        if (type_is_any_without_null(target) && lambda_type_accepts_null(source)) {
            return STATIC_BOUNDARY_REJECTED;
        }
        return STATIC_BOUNDARY_PROVEN;
    }
    // The internal tops establish only their error/null exclusions. They do
    // not prove a concrete carrier or structural contract, so leave those
    // boundaries for the runtime rather than accepting through TypeId ANY.
    if (type_is_any_without_error(source) || type_is_any_without_null(source)) {
        return STATIC_BOUNDARY_DEFERRED;
    }
    if (source == &TYPE_NUMBER || source == &TYPE_INTEGER) {
        // Abstract numeric success sets describe several concrete Item carriers.
        // They cannot reject a narrower destination statically; its established
        // runtime boundary owns the exact conversion/check instead.
        return STATIC_BOUNDARY_DEFERRED;
    }
    // C16: `int` is the float64-representable integers, so it is a SUBSET of
    // float rather than a disjoint carrier, and admission into it is decided by
    // membership -- is this float integral and in band -- not by the static
    // type. `(a + 2) / 3 * 4` is typed float because `/` is, yet every value it
    // can produce here is an int. Only the runtime boundary can tell, so a
    // float source against an int target defers exactly like the abstract
    // numeric sets above. (int -> float stays PROVEN below: every int, poison
    // included, is a float.)
    if (boundary_numeric_admission_is_dynamic(source->type_id, target->type_id)) {
        return STATIC_BOUNDARY_DEFERRED;
    }

    if (boundary_type_is_extended(source, TYPE_KIND_BINARY)) {
        TypeBinary* source_binary = (TypeBinary*)source;
        if (source_binary->op == OPERATOR_UNION) {
            // A source union is contained only when every member fits the
            // whole target. Checking target arms first rejected `int | error`
            // against itself because neither target arm admits both members.
            StaticBoundaryResult left = static_boundary_relation(source_binary->left, target);
            StaticBoundaryResult right = static_boundary_relation(source_binary->right, target);
            if (left == STATIC_BOUNDARY_REJECTED || right == STATIC_BOUNDARY_REJECTED) {
                return STATIC_BOUNDARY_REJECTED;
            }
            return left == STATIC_BOUNDARY_DEFERRED || right == STATIC_BOUNDARY_DEFERRED ?
                STATIC_BOUNDARY_DEFERRED : STATIC_BOUNDARY_PROVEN;
        }
    }
    if (boundary_type_is_extended(target, TYPE_KIND_BINARY)) {
        TypeBinary* target_binary = (TypeBinary*)target;
        if (target_binary->op == OPERATOR_UNION) {
            StaticBoundaryResult left = static_boundary_relation(source, target_binary->left);
            StaticBoundaryResult right = static_boundary_relation(source, target_binary->right);
            if (left == STATIC_BOUNDARY_PROVEN || right == STATIC_BOUNDARY_PROVEN) {
                return STATIC_BOUNDARY_PROVEN;
            }
            return left == STATIC_BOUNDARY_DEFERRED || right == STATIC_BOUNDARY_DEFERRED ?
                STATIC_BOUNDARY_DEFERRED : STATIC_BOUNDARY_REJECTED;
        }
        // `A & B` admits a source only if BOTH arms admit it. Without these two
        // arms an intersection or exclusion target fell through to the generic
        // tail and was rejected outright, so `let a: int & string` failed while
        // `x is (int & string)` worked (LR02-9). `OPERATOR_OR` is accepted as
        // the historical spelling of a type-level `&`.
        if (target_binary->op == OPERATOR_INTERSECT || target_binary->op == OPERATOR_OR) {
            StaticBoundaryResult left = static_boundary_relation(source, target_binary->left);
            StaticBoundaryResult right = static_boundary_relation(source, target_binary->right);
            if (left == STATIC_BOUNDARY_REJECTED || right == STATIC_BOUNDARY_REJECTED) {
                return STATIC_BOUNDARY_REJECTED;
            }
            return left == STATIC_BOUNDARY_PROVEN && right == STATIC_BOUNDARY_PROVEN ?
                STATIC_BOUNDARY_PROVEN : STATIC_BOUNDARY_DEFERRED;
        }
        // `A ! B` admits a source that fits A and is not B. Only the positive
        // arm is a static question; non-membership is decided on the value
        // unless the excluded arm is provably disjoint from the source.
        if (target_binary->op == OPERATOR_EXCLUDE) {
            StaticBoundaryResult keep = static_boundary_relation(source, target_binary->left);
            if (keep == STATIC_BOUNDARY_REJECTED) return STATIC_BOUNDARY_REJECTED;
            StaticBoundaryResult drop = static_boundary_relation(source, target_binary->right);
            if (keep == STATIC_BOUNDARY_PROVEN && drop == STATIC_BOUNDARY_REJECTED) {
                return STATIC_BOUNDARY_PROVEN;
            }
            return STATIC_BOUNDARY_DEFERRED;
        }
    }
    // A parameter/member can carry an occurrence or optional wrapper as its
    // effective AST type. Its structural relation is not represented by the
    // compact Type prefix, so defer to the runtime matcher instead of
    // misdiagnosing a statically well-formed nullable container call.
    if (boundary_type_is_extended(source, TYPE_KIND_UNARY)) {
        return STATIC_BOUNDARY_DEFERRED;
    }
    if (boundary_type_is_extended(target, TYPE_KIND_UNARY)) {
        TypeUnary* target_unary = (TypeUnary*)target;
        if (target_unary->op == OPERATOR_OPTIONAL) {
            if (source->type_id == LMD_TYPE_NULL) return STATIC_BOUNDARY_PROVEN;
            return static_boundary_relation(source, target_unary->operand);
        }
        // Occurrence/array membership requires element information that can be
        // absent from a nonliteral source, so keep the dynamic path available.
        return STATIC_BOUNDARY_DEFERRED;
    }
    if (source->type_id == LMD_TYPE_MAP && target->type_id == LMD_TYPE_MAP &&
            source != target && target != &TYPE_MAP) {
        return STATIC_BOUNDARY_DEFERRED;
    }
    return types_compatible_with_full(source, target, target) ?
        STATIC_BOUNDARY_PROVEN : STATIC_BOUNDARY_REJECTED;
}

// See type_contract.hpp. A proven relation is necessary but not sufficient:
// admission also converts. Requiring both sides to be unadorned global scalar
// carriers of the same TypeId is what makes admission provably the identity —
// constraints, unions, occurrences and named map shapes all reach runtime
// admission and may convert or re-pack. An earlier revision elided on PROVEN
// alone and silently skipped the int->float widening in a `var x: float = <int>`
// declaration, which made mbrot/permute/nqueens compute wrong answers.
bool lambda_boundary_is_redundant(Type* source, Type* target) {
    if (!source || !target) return false;
    if (static_boundary_relation(source, target) != STATIC_BOUNDARY_PROVEN) return false;
    if (!is_global_simple_type(source) || !is_global_simple_type(target)) return false;
    return source->type_id == target->type_id;
}

// Calls with an error-capable argument have one extra control-flow edge: a
// parameter that excludes error returns that exact Item before its body starts.
// Compare only the successful union members here; the MIR caller guard owns
// the skipped error member, so it must not turn a valid `T | error` call into
// a static parameter mismatch.
static StaticBoundaryResult static_parameter_boundary_relation(Type* source, Type* target) {
    source = boundary_unwrap_type(source);
    if (boundary_type_is_extended(source, TYPE_KIND_BINARY)) {
        TypeBinary* binary = (TypeBinary*)source;
        if (binary->op == OPERATOR_UNION) {
            Type* left = boundary_unwrap_type(binary->left);
            Type* right = boundary_unwrap_type(binary->right);
            bool left_is_error = left && left->type_id == LMD_TYPE_ERROR;
            bool right_is_error = right && right->type_id == LMD_TYPE_ERROR;
            if (left_is_error && right_is_error) return STATIC_BOUNDARY_PROVEN;
            if (left_is_error) return static_parameter_boundary_relation(right, target);
            if (right_is_error) return static_parameter_boundary_relation(left, target);
        }
    }
    return static_boundary_relation(source, target);
}

static AstNode* boundary_unwrap_primary(AstNode* node) {
    while (node && node->node_type == AST_NODE_PRIMARY) {
        AstNode* inner = ((AstPrimaryNode*)node)->expr;
        if (!inner) break;
        node = inner;
    }
    return node;
}

static void check_declared_map_literal(Transpiler* tp, AstNamedNode* declaration,
        TypeMap* expected, int line) {
    AstNode* rhs = boundary_unwrap_primary(declaration->as);
    if (!rhs || rhs->node_type != AST_NODE_MAP || !expected ||
            expected == (TypeMap*)&TYPE_MAP) return;
    TypeMap* actual = (TypeMap*)rhs->type;
    if (!actual) return;

    bool has_spread = false;
    for (ShapeEntry* entry = actual->shape; entry; entry = entry->next) {
        if (!entry->name) {
            has_spread = true;
            break;
        }
    }
    for (ShapeEntry* expected_entry = expected->shape; expected_entry;
            expected_entry = expected_entry->next) {
        if (!expected_entry->name || !expected_entry->name->str) continue;
        ShapeEntry* actual_entry = typemap_shape_lookup_last(actual,
            expected_entry->name->str, (int)expected_entry->name->length);
        if (!actual_entry) {
            if (!has_spread) {
                record_semantic_error_span(tp, declaration->source_span, ERR_UNDEFINED_FIELD,
                    "map assigned to '%.*s' is missing required field '%.*s'",
                    (int)declaration->name->len, declaration->name->chars,
                    (int)expected_entry->name->length, expected_entry->name->str);
            }
            continue;
        }
        StaticBoundaryResult field_result = static_boundary_relation(actual_entry->type,
            expected_entry->type);
        if (field_result == STATIC_BOUNDARY_REJECTED) {
            char expected_name[128];
            char actual_name[128];
            lambda_type_format_name(expected_entry->type, expected_name, sizeof(expected_name));
            lambda_type_format_name(actual_entry->type, actual_name, sizeof(actual_name));
            record_type_error(tp, line,
                "field '%.*s' of '%.*s' expects %s, but got %s",
                (int)expected_entry->name->length, expected_entry->name->str,
                (int)declaration->name->len, declaration->name->chars,
                expected_name, actual_name);
        }
    }
}

// returns true when the declaration was statically REJECTED (diagnostic
// recorded) — relaxed mode uses that to drop the annotation contract so the
// binding keeps its inferred type instead of lying about its representation
// (SI14: emitting the declared carrier against a rejected value reinterprets
// bits — the observed symptom was a string pointer printed as an int).
static bool check_declaration_static_boundary(Transpiler* tp, AstNamedNode* declaration,
        Type* expected, int line) {
    if (!declaration || !declaration->as || !expected) return false;
    if (expected->type_id == LMD_TYPE_RANGE && expected->kind == TYPE_KIND_RANGE) {
        // Range annotations are checked by value membership at the MIR boundary, not by TypeId equality.
        return false;
    }
    Type* actual = declaration->as->type;
    StaticBoundaryResult result = static_boundary_relation(actual, expected);
    if (result == STATIC_BOUNDARY_REJECTED) {
        Type* actual_type = boundary_unwrap_type(actual);
        Type* expected_type = boundary_unwrap_type(expected);
        char expected_name[128];
        char actual_name[128];
        lambda_type_format_name(expected_type, expected_name, sizeof(expected_name));
        lambda_type_format_name(actual_type, actual_name, sizeof(actual_name));
        record_type_error(tp, line, "cannot initialize '%.*s' of type %s with %s",
            (int)declaration->name->len, declaration->name->chars,
            expected_name, actual_name);
        return true;
    }
    Type* expected_type = boundary_unwrap_type(expected);
    if (expected_type && expected_type->type_id == LMD_TYPE_MAP) {
        check_declared_map_literal(tp, declaration, (TypeMap*)expected_type, line);
    }
    return false;
}

// Forward declaration for closure capture analysis
void collect_captures_from_node(Transpiler* tp, AstNode* node, NameScope* fn_scope,
                                 NameScope* global_scope, FnCapture** captures);

// Check if a name entry is defined in a scope or any of its descendant scopes (local to function)
// This includes variables declared in while blocks, if blocks, for loops, etc.
bool is_local_to_scope(NameEntry* entry, NameScope* fn_scope) {
    if (!entry) return false;

    // With the entry->scope field, we can now check if the entry's defining scope
    // is fn_scope itself or a descendant of fn_scope (nested block within the function).
    // If yes, the variable is local to this function; if no, it's a capture from outer scope.

    NameScope* entry_scope = entry->scope;
    if (!entry_scope) {
        // Fallback for entries without scope info (shouldn't happen for var/let)
        // Check if entry is directly in fn_scope
        NameEntry* e = fn_scope->first;
        while (e) {
            if (e == entry) return true;
            e = e->next;
        }
        return false;
    }

    // Check if entry_scope is fn_scope or a descendant of fn_scope
    NameScope* scope = entry_scope;
    while (scope) {
        if (scope == fn_scope) return true;
        scope = scope->parent;
    }

    return false;
}

// Check if a name entry is in global scope
bool is_global_entry(NameEntry* entry, NameScope* global_scope) {
    if (!global_scope) return false;
    NameEntry* e = global_scope->first;
    while (e) {
        if (e == entry) return true;
        e = e->next;
    }
    return false;
}

static bool is_object_field_entry(NameEntry* entry) {
    return entry && entry->node && entry->node->node_type == AST_NODE_KEY_EXPR;
}

static AstNode* unwrap_primary_node(AstNode* node) {
    while (node && node->node_type == AST_NODE_PRIMARY) {
        AstPrimaryNode* primary = (AstPrimaryNode*)node;
        node = primary->expr;
    }
    return node;
}

static AstIdentNode* compound_root_ident(AstNode* node) {
    node = unwrap_primary_node(node);
    if (!node) return NULL;
    if (node->node_type == AST_NODE_IDENT) return (AstIdentNode*)node;
    if (node->node_type == AST_NODE_INDEX_EXPR || node->node_type == AST_NODE_MEMBER_EXPR) {
        AstFieldNode* field = (AstFieldNode*)node;
        return compound_root_ident(field->object);
    }
    return NULL;
}

// Resolve a statically named member/index path from an explicitly annotated
// root.  Inferred map shapes intentionally do not participate: an unannotated
// `var` remains free to evolve its value/shape, while `var p: Person` keeps the
// Person contract across every interior write.
static Type* declared_compound_destination_type(Transpiler* tp, AstNode* node,
        const char** destination_label) {
    node = unwrap_primary_node(node);
    if (!node) return NULL;
    if (node->node_type == AST_NODE_IDENT) {
        AstIdentNode* ident = (AstIdentNode*)node;
        return ident->entry ? ident->entry->declared_type : NULL;
    }
    if (node->node_type == AST_NODE_MEMBER_EXPR) {
        AstFieldNode* member = (AstFieldNode*)node;
        Type* owner_type = declared_compound_destination_type(tp, member->object, NULL);
        owner_type = boundary_unwrap_type(owner_type);
        if (!owner_type || owner_type->type_id != LMD_TYPE_MAP ||
                is_global_simple_type(owner_type) ||
                !member->field || member->field->node_type != AST_NODE_IDENT) {
            return NULL;
        }
        AstIdentNode* field_name = (AstIdentNode*)member->field;
        ShapeEntry* field = find_shape_field_by_name((TypeMap*)owner_type,
            field_name->name->chars, field_name->name->len);
        if (field && destination_label) *destination_label = "map member";
        return field ? boundary_unwrap_type(field->type) : NULL;
    }
    if (node->node_type == AST_NODE_INDEX_EXPR) {
        AstFieldNode* index = (AstFieldNode*)node;
        Type* owner_type = boundary_unwrap_type(
            declared_compound_destination_type(tp, index->object, NULL));
        if (owner_type && owner_type->type_id == LMD_TYPE_MAP &&
                !is_global_simple_type(owner_type) && index->field && !index->field->next) {
            Item key = ItemNull;
            if (ast_static_literal_item(tp, index->field, &key) &&
                    get_type_id(key) == LMD_TYPE_STRING) {
                String* field_name = it2s(key);
                ShapeEntry* field = find_shape_field_by_name((TypeMap*)owner_type,
                    field_name->chars, field_name->len);
                // A literal bracket key resolves to the same shaped field as dot syntax.
                if (field && destination_label) *destination_label = "map member";
                return field ? boundary_unwrap_type(field->type) : NULL;
            }
        }
        if (boundary_type_is_extended(owner_type, TYPE_KIND_UNARY)) {
            TypeUnary* occurrence = (TypeUnary*)owner_type;
            if (occurrence->op == OPERATOR_REPEAT) {
                return boundary_unwrap_type(occurrence->operand);
            }
        }
        if (!owner_type || owner_type->type_id != LMD_TYPE_ARRAY ||
                is_global_simple_type(owner_type)) {
            return NULL;
        }
        TypeArray* array_type = (TypeArray*)owner_type;
        return boundary_unwrap_type(array_type->nested);
    }
    return NULL;
}

static void check_compound_assignment_static_boundary(Transpiler* tp,
        SourceSpan span,
        AstNode* destination, AstNode* value, const char* label) {
    const char* destination_label = label;
    Type* expected = declared_compound_destination_type(tp, destination, &destination_label);
    if (!expected || !value || !value->type) return;
    // A value-producing conversion can return ItemError before this store. Its
    // lowering propagates that error and never mutates the destination, so the
    // assignment boundary must validate only the success constituents.
    Type* success_type = lambda_type_remove_error(tp->pool, value->type);
    if (!success_type || static_boundary_relation(success_type, expected) !=
            STATIC_BOUNDARY_REJECTED) return;
    char expected_name[128];
    char value_name[128];
    lambda_type_format_name(expected, expected_name, sizeof(expected_name));
    lambda_type_format_name(success_type, value_name, sizeof(value_name));
    record_semantic_error_span(tp, span, ERR_TYPE_MISMATCH,
        "cannot assign %s to typed %s of type %s",
        value_name, destination_label, expected_name);
}

static bool same_name_string(String* a, String* b) {
    return a == b || (a && b && a->len == b->len && memcmp(a->chars, b->chars, a->len) == 0);
}

static bool type_exact_match(Type* left, TypeParam* right) {
    if (!left || !right) return true;
    Type* right_full = parameter_boundary_type(right);
    if (!right_full) return true;
    if (right_full->type_id == LMD_TYPE_ANY) return true;
    if (left->type_id != right_full->type_id) return false;
    if (left->kind != right_full->kind) return false;
    if (left->kind == TYPE_KIND_UNARY) {
        TypeUnary* lu = (TypeUnary*)left;
        TypeUnary* ru = (TypeUnary*)right_full;
        Type* lo = lu->operand;
        Type* ro = ru->operand;
        if (lo && lo->type_id == LMD_TYPE_TYPE && lo->kind == TYPE_KIND_SIMPLE) lo = ((TypeType*)lo)->type;
        if (ro && ro->type_id == LMD_TYPE_TYPE && ro->kind == TYPE_KIND_SIMPLE) ro = ((TypeType*)ro)->type;
        return lo && ro && lo->type_id == ro->type_id;
    }
    return true;
}



// Add a capture to the list if not already present
void add_capture(Transpiler* tp, FnCapture** captures, String* name, NameEntry* entry) {
    // Check if already captured
    FnCapture* c = *captures;
    while (c) {
        if (c->lambda_name == name || (c->lambda_name && name &&
            c->lambda_name->len == name->len &&
            memcmp(c->lambda_name->chars, name->chars, name->len) == 0)) {
            return; // already captured
        }
        c = c->next;
    }

    // Add new capture
    FnCapture* capture = (FnCapture*)pool_calloc(tp->pool, sizeof(FnCapture));
    capture->lambda_name = name;
    capture->name = name ? name->chars : "";
    capture->scope_env_key = capture->name;
    capture->entry = entry;
    capture->scope_env_slot = -1;
    capture->grandparent_slot = -1;
    capture->parent_env_link_slot_override = -1;
    capture->is_mutable = false;
    capture->next = *captures;
    *captures = capture;
    log_debug("capture added: %.*s", (int)name->len, name->chars);
}

// Mark an existing capture as mutable (called when assignment to captured var is detected)
void mark_capture_mutable(FnCapture** captures, String* name) {
    FnCapture* c = *captures;
    while (c) {
        if (c->lambda_name == name || (c->lambda_name && name &&
            c->lambda_name->len == name->len &&
            memcmp(c->lambda_name->chars, name->chars, name->len) == 0)) {
            c->is_mutable = true;
            log_debug("capture marked mutable: %.*s", (int)name->len, name->chars);
            return;
        }
        c = c->next;
    }
}

// Recursively collect captures from an AST node
void collect_captures_from_node(Transpiler* tp, AstNode* node, NameScope* fn_scope,
                                 NameScope* global_scope, FnCapture** captures) {
    if (!node) return;

    switch (node->node_type) {
    case AST_NODE_IDENT: {
        AstIdentNode* ident = (AstIdentNode*)node;
        if (ident->entry && ident->entry->node) {
            // Object fields in method scope are implicit receiver slots, not
            // closure captures; method write-back owns their mutation rules.
            if (is_object_field_entry(ident->entry)) break;
            // Check if this identifier refers to a variable from an enclosing scope
            // (not local to fn_scope, not global, not an import)
            if (!ident->entry->import &&
                !is_local_to_scope(ident->entry, fn_scope) &&
                !is_global_entry(ident->entry, global_scope)) {
                add_capture(tp, captures, ident->name, ident->entry);
            }
        }
        break;
    }
    case AST_NODE_PRIMARY: {
        AstPrimaryNode* pri = (AstPrimaryNode*)node;
        collect_captures_from_node(tp, pri->expr, fn_scope, global_scope, captures);
        break;
    }
    case AST_NODE_UNARY:
    case AST_NODE_SPREAD: {
        AstUnaryNode* un = (AstUnaryNode*)node;
        collect_captures_from_node(tp, un->operand, fn_scope, global_scope, captures);
        break;
    }
    case AST_NODE_BINARY: {
        AstBinaryNode* bin = (AstBinaryNode*)node;
        collect_captures_from_node(tp, bin->left, fn_scope, global_scope, captures);
        collect_captures_from_node(tp, bin->right, fn_scope, global_scope, captures);
        break;
    }
    case AST_NODE_IF_EXPR: {
        AstIfNode* if_node = (AstIfNode*)node;
        collect_captures_from_node(tp, if_node->cond, fn_scope, global_scope, captures);
        collect_captures_from_node(tp, if_node->then, fn_scope, global_scope, captures);
        collect_captures_from_node(tp, if_node->otherwise, fn_scope, global_scope, captures);
        break;
    }
    case AST_NODE_MATCH_EXPR: {
        AstMatchNode* match_node = (AstMatchNode*)node;
        collect_captures_from_node(tp, match_node->scrutinee, fn_scope, global_scope, captures);
        AstMatchArm* arm = match_node->first_arm;
        while (arm) {
            collect_captures_from_node(tp, arm->body, fn_scope, global_scope, captures);
            arm = (AstMatchArm*)arm->next;
        }
        break;
    }
    case AST_NODE_FOR_EXPR:
    case AST_NODE_FOR_STAM: {
        AstForNode* for_node = (AstForNode*)node;
        // Note: loop variable is local, handled by fn_scope extension
        AstNode* loop = for_node->loop;
        while (loop) {
            if (loop->node_type == AST_NODE_LOOP) {
                // Must cast to AstLoopNode (not AstNamedNode) — AstLoopNode has
                // an extra index_name field before 'as', so the offset differs.
                AstLoopNode* loop_var = (AstLoopNode*)loop;
                collect_captures_from_node(tp, loop_var->as, fn_scope, global_scope, captures);
            }
            loop = loop->next;
        }
        collect_captures_from_node(tp, for_node->then, fn_scope, global_scope, captures);
        break;
    }
    case AST_NODE_WHILE_STAM: {
        AstWhileNode* while_node = (AstWhileNode*)node;
        collect_captures_from_node(tp, while_node->cond, fn_scope, global_scope, captures);
        collect_captures_from_node(tp, while_node->body, fn_scope, global_scope, captures);
        break;
    }
    case AST_NODE_CALL_EXPR: {
        AstCallNode* call = (AstCallNode*)node;
        collect_captures_from_node(tp, call->function, fn_scope, global_scope, captures);
        AstNode* arg = call->argument;
        while (arg) {
            collect_captures_from_node(tp, arg, fn_scope, global_scope, captures);
            arg = arg->next;
        }
        break;
    }
    case AST_NODE_HANDLER_EXPR:
    case AST_NODE_HANDLER_STAM: {
        AstHandlerNode* handler = (AstHandlerNode*)node;
        collect_captures_from_node(tp, handler->operand, fn_scope, global_scope, captures);
        collect_captures_from_node(tp, handler->body, fn_scope, global_scope, captures);
        collect_captures_from_node(tp, handler->value_body, fn_scope, global_scope, captures);
        break;
    }
    case AST_NODE_START: {
        AstStartNode* start = (AstStartNode*)node;
        collect_captures_from_node(tp, (AstNode*)start->call, fn_scope, global_scope, captures);
        break;
    }
    case AST_NODE_INDEX_EXPR:
    case AST_NODE_MEMBER_EXPR: {
        AstFieldNode* field = (AstFieldNode*)node;
        collect_captures_from_node(tp, field->object, fn_scope, global_scope, captures);
        collect_captures_from_node(tp, field->field, fn_scope, global_scope, captures);
        break;
    }
    case AST_NODE_LIST:
    case AST_NODE_CONTENT:
    case AST_NODE_ARRAY: {
        AstListNode* list = (AstListNode*)node;
        AstNode* item = list->item;
        while (item) {
            collect_captures_from_node(tp, item, fn_scope, global_scope, captures);
            item = item->next;
        }
        break;
    }
    case AST_NODE_MAP: {
        AstMapNode* map = (AstMapNode*)node;
        AstNode* item = map->item;
        while (item) {
            collect_captures_from_node(tp, item, fn_scope, global_scope, captures);
            item = item->next;
        }
        break;
    }
    case AST_NODE_ASSIGN:
    case AST_NODE_KEY_EXPR:
    case AST_NODE_LOOP: {
        AstNamedNode* named = (AstNamedNode*)node;
        collect_captures_from_node(tp, named->as, fn_scope, global_scope, captures);
        break;
    }
    case AST_NODE_ASSIGN_STAM: {
        AstAssignStamNode* assign = (AstAssignStamNode*)node;
        // check if the assignment target is a captured variable from an enclosing scope
        if (is_object_field_entry(assign->target_entry)) {
            collect_captures_from_node(tp, assign->value, fn_scope, global_scope, captures);
            break;
        }
        if (assign->target_entry && !assign->target_entry->import &&
            !is_local_to_scope(assign->target_entry, fn_scope) &&
            !is_global_entry(assign->target_entry, global_scope)) {
            add_capture(tp, captures, assign->target, assign->target_entry);
            mark_capture_mutable(captures, assign->target);
        }
        // also collect captures from the value expression
        collect_captures_from_node(tp, assign->value, fn_scope, global_scope, captures);
        break;
    }
    case AST_NODE_INDEX_ASSIGN_STAM:
    case AST_NODE_MEMBER_ASSIGN_STAM: {
        AstCompoundAssignNode* assign = (AstCompoundAssignNode*)node;
        AstIdentNode* root = compound_root_ident(assign->object);
        if (root && is_object_field_entry(root->entry)) {
            collect_captures_from_node(tp, assign->key, fn_scope, global_scope, captures);
            collect_captures_from_node(tp, assign->value, fn_scope, global_scope, captures);
            break;
        }
        if (root && root->entry && !root->entry->import &&
            !is_local_to_scope(root->entry, fn_scope) &&
            !is_global_entry(root->entry, global_scope)) {
            add_capture(tp, captures, root->name, root->entry);
            mark_capture_mutable(captures, root->name);
        }
        collect_captures_from_node(tp, assign->object, fn_scope, global_scope, captures);
        collect_captures_from_node(tp, assign->key, fn_scope, global_scope, captures);
        collect_captures_from_node(tp, assign->value, fn_scope, global_scope, captures);
        break;
    }
    case AST_NODE_RETURN_STAM: {
        AstReturnNode* ret = (AstReturnNode*)node;
        collect_captures_from_node(tp, ret->value, fn_scope, global_scope, captures);
        break;
    }
    case AST_NODE_LET_STAM:
    case AST_NODE_VAR_STAM: {
        AstLetNode* let = (AstLetNode*)node;
        AstNode* decl = let->declare;
        while (decl) {
            collect_captures_from_node(tp, decl, fn_scope, global_scope, captures);
            decl = decl->next;
        }
        break;
    }
    case AST_NODE_FUNC:
    case AST_NODE_FUNC_EXPR:
    case AST_NODE_PROC: {
        // Nested functions: we need to propagate any captures that the nested function
        // has which come from scopes ABOVE the current function's scope.
        // This ensures that intermediate functions capture variables needed by their
        // nested functions for closure environment construction.
        AstFuncNode* nested_fn = (AstFuncNode*)node;
        if (nested_fn->captures) {
            FnCapture* cap = nested_fn->captures;
            while (cap) {
                // Check if this captured variable is NOT local to the current function's scope
                // and NOT global. If so, the current function also needs to capture it.
                if (cap->entry && !cap->entry->import &&
                    !is_local_to_scope(cap->entry, fn_scope) &&
                    !is_global_entry(cap->entry, global_scope)) {
                    add_capture(tp, captures, cap->lambda_name, cap->entry);
                }
                cap = cap->next;
            }
        }
        break;
    }
    default:
        // Other node types don't need capture analysis
        break;
    }
}

// Analyze captures for a function node
void analyze_captures(Transpiler* tp, AstFuncNode* fn_node, NameScope* global_scope) {
    fn_node->captures = nullptr;
    collect_captures_from_node(tp, fn_node->body, fn_node->vars, global_scope, &fn_node->captures);
    if (!fn_node->analysis) {
        fn_node->analysis = (FnAnalysis*)pool_calloc(tp->pool, sizeof(FnAnalysis));
    }
    fn_node->analysis->captures = fn_node->captures;
    fn_node->analysis->capture_count = 0;

    if (fn_node->captures) {
        log_debug("function %.*s has captures:",
            fn_node->name ? (int)fn_node->name->len : 5,
            fn_node->name ? fn_node->name->chars : "anon");
        FnCapture* c = fn_node->captures;
        while (c) {
            fn_node->analysis->capture_count++;
            String* capture_name = c->lambda_name;
            log_debug("  - %.*s", (int)capture_name->len, capture_name->chars);
            // A mutable capture is an explicit cross-frame write only when the
            // outer binding itself is a `var`; immutable captures remain pure.
            if (c->is_mutable && (!c->entry || !c->entry->is_mutable)) {
                record_semantic_error_span(tp, fn_node->source_span, ERR_IMMUTABLE_ASSIGNMENT,
                    "cannot mutate captured binding '%.*s'. pass it as `var` to a pn or return a new value.",
                    (int)capture_name->len, capture_name->chars);
            }
            c = c->next;
        }
    }
}

// Find the global scope by walking up the parent chain
NameScope* find_global_scope(NameScope* scope) {
    while (scope && scope->parent) {
        scope = scope->parent;
    }
    return scope;
}

// str_to_decimal is now in lambda-decimal.cpp as decimal_parse_str

AstNode* alloc_ast_node_from_span(Transpiler* tp, AstNodeType node_type,
        SourceSpan span, size_t size) {
    AstNode* ast_node = (AstNode*)pool_alloc(tp->pool, size);
    memset(ast_node, 0, size);
    ast_node->node_type = node_type;
    ast_node->source_span = span;
    return ast_node;
}



void* alloc_const(Transpiler* tp, size_t size) {
    void* bytes = pool_alloc(tp->pool, size);
    memset(bytes, 0, size);
    return bytes;
}

// extract name text from an identifier or symbol node
// for identifiers, returns the source text as-is
// for symbols, strips the surrounding single quotes




// check if a name is a reserved type keyword
bool is_type_keyword(StrView name) {
    static const char* type_keywords[] = {
        "null", "any", "error", "bool", "int", "int64", "float", "f64", "decimal", "integer", "number",
        "date", "time", "datetime", "symbol", "string", "binary",
        "list", "array", "map", "element", "entity", "object", "type", "function",
        "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "f16", "f32", "f64"
    };
    for (size_t i = 0; i < sizeof(type_keywords) / sizeof(type_keywords[0]); i++) {
        if (strview_equal(&name, type_keywords[i])) {
            return true;
        }
    }
    return false;
}

bool is_reserved_identifier_keyword(StrView name) {
    // S16.10.1v2: only capture-real words are barred — those that can begin a
    // construct, so a binding of that name could not be read back where the
    // construct starts. Before this bar, `let type = 1` silently read the base
    // type and `let if = 1` failed at every use. Clause words and infix word
    // operators stay legal; the lexer owns the classification.
    return lambda_lexer_word_bars_binding(name.str, name.length);
}

// S16.10.2 keeps data names open, so only true binding declarations are
// checked. Object-type field entries reach `push_name` as scope helpers for
// bare-field resolution, not as bindings, and `{type: int}` must stay legal.
static bool ast_node_declares_binding(AstNode* node) {
    return node && node->node_type != AST_NODE_KEY_EXPR;
}

// lookup a name in the current scope only (not in parent scopes)
// returns the existing entry if found, NULL otherwise
NameEntry* lookup_name_in_current_scope(Transpiler* tp, String* name) {
    NameEntry* entry = tp->current_scope->first;
    while (entry) {
        if (entry->name == name ||  // pointer comparison (interned strings)
            (entry->name->len == name->len &&
             memcmp(entry->name->chars, name->chars, name->len) == 0)) {
            return entry;
        }
        entry = entry->next;
    }
    return NULL;
}

void push_name(Transpiler* tp, AstNamedNode* node, AstImportNode* import) {
    log_debug("pushing name %.*s, %p", (int)node->name->len, node->name->chars, node->type);

    StrView name_view = {node->name->chars, node->name->len};
    if (ast_node_declares_binding((AstNode*)node) &&
            is_reserved_identifier_keyword(name_view)) {
        int line = (int)ast_node_start_point(tp, node).row + 1;
        // S16.10.1: keywords never name bindings; no quoted escape exists,
        // since a quoted use site is a symbol and symbols never implicitly
        // read bindings (S2.4.3). `last` was the original single case.
        record_type_error(tp, line, "Error: '%.*s' is a reserved keyword and cannot be used as a name",
            (int)name_view.length, name_view.str);
    }
    // S12.3.7: a user binding shadows a same-named system function for this
    // module only. Legal and forward-compatible (a new sys func must never
    // change an existing program), but always warned so accidents surface.
    else if (ast_node_declares_binding((AstNode*)node) && !import &&
            is_sys_func_name(name_view.str, (int)name_view.length)) {
        log_warn("lambda_shadow_lint: line %d: '%.*s' shadows a system function in this module; "
            "calls here resolve to your definition",
            (int)ast_node_start_point(tp, node).row + 1,
            (int)name_view.length, name_view.str);
    }

    // check for duplicate definition in current scope
    NameEntry* existing = lookup_name_in_current_scope(tp, node->name);
    if (existing) {
        record_semantic_error_span(tp, node->source_span, ERR_DUPLICATE_DEFINITION,
            "duplicate definition of '%.*s' in the same scope",
            (int)node->name->len, node->name->chars);
        // continue anyway to allow further error checking
    }

    NameEntry* entry = (NameEntry*)pool_calloc(tp->pool, sizeof(NameEntry));
    entry->name = node->name;
    entry->node = (AstNode*)node;  entry->import = import;
    entry->scope = tp->current_scope;  // track which scope this variable belongs to
    if (node->node_type == AST_NODE_ASSIGN || node->node_type == AST_NODE_PARAM) {
        entry->declared_type = node->declared_type;
        entry->has_type_annotation = node->declared_type != NULL;
    }
    // `push_name` also registers loop/function/object nodes through historical
    // layout-compatible casts.  Do not write AstNamedNode-only fields here:
    // on a loop that alias is its join pointer and would turn every for into a join.
    if (!tp->current_scope->first) { tp->current_scope->first = entry; }
    if (tp->current_scope->last) { tp->current_scope->last->next = entry; }
    tp->current_scope->last = entry;
}

NameScope* lambda_ast_enter_scope_with_parent(Transpiler* tp,
        NameScope* parent, bool is_proc) {
    NameScope* scope = (NameScope*)pool_calloc(tp->pool, sizeof(NameScope));
    scope->parent = parent;
    scope->is_proc = is_proc;
    tp->current_scope = scope;
    return scope;
}

NameScope* lambda_ast_enter_scope(Transpiler* tp, bool is_proc) {
    return lambda_ast_enter_scope_with_parent(tp, tp->current_scope, is_proc);
}

void lambda_ast_leave_scope(Transpiler* tp, NameScope* scope) {
    if (!scope || tp->current_scope != scope) {
        // A parser lookahead must never mutate a scope. Refusing an unmatched
        // leave protects the parent chain if a future sink abandons a branch.
        log_error("lambda AST scope leave: unmatched scope");
        return;
    }
    tp->current_scope = scope->parent;
}

void lambda_ast_register_name(Transpiler* tp, AstNamedNode* node) {
    push_name(tp, node, NULL);
}

AstFuncNode* build_function_placeholder_from_parts(Transpiler* tp,
        SourceSpan span, StrView name, bool is_proc) {
    // An unnamed function is an arrow/closure: AST_NODE_FUNC_EXPR. Forward
    // declarations always carry a name, so keying on the
    // spelling is safe. Consumers group FUNC_EXPR with FUNC everywhere, so this
    // only restores the distinction the AST already models — it is not a
    // behaviour change.
    bool is_anonymous = name.length == 0;
    AstFuncNode* fn_node = (AstFuncNode*)alloc_ast_node_from_span(tp,
        is_proc ? AST_NODE_PROC : is_anonymous ? AST_NODE_FUNC_EXPR : AST_NODE_FUNC,
        span, sizeof(AstFuncNode));
    fn_node->type = alloc_type(tp->pool, LMD_TYPE_FUNC, sizeof(TypeFunc));
    TypeFunc* fn_type = (TypeFunc*)fn_node->type;
    fn_type->is_anonymous = is_anonymous;
    fn_type->is_proc = is_proc;
    fn_type->param = NULL;
    fn_type->param_count = 0;
    fn_type->required_param_count = 0;
    fn_type->is_variadic = false;
    fn_type->error_type = NULL;
    fn_type->can_raise = false;
    fn_type->returned = &TYPE_ANY;
    set_function_return_contract(fn_type,
        is_proc ? &TYPE_ANY : &TYPE_ANY_NO_ERROR, false);
    // an anonymous function has no name at all — an empty String would print
    // as `(name "")` and read as a named function.
    fn_node->name = is_anonymous ? NULL
        : name_pool_create_strview(tp->name_pool, name);
    // The forward declaration must be safely visible before its body exists.
    // Completion later fills these fields in place so every early reference
    // keeps the binding identity registered in the enclosing scope.
    fn_node->param = NULL;
    fn_node->body = NULL;
    fn_node->vars = NULL;
    fn_node->captures = NULL;
    return fn_node;
}

// Parenthesized lets: (let x = a, let y = b, expr) — sequential bindings returning the last expr.


AstNode* build_array_from_items(Transpiler* tp, SourceSpan span,
        AstNode* items) {
    for (AstNode* item = items; item; item = item->next) {
        reject_procedural_block_operand(tp, item, "an array element");
    }
    AstArrayNode* ast_node = (AstArrayNode*)alloc_ast_node_from_span(tp,
        AST_NODE_ARRAY, span, sizeof(AstArrayNode));
    ast_node->type = alloc_type(tp->pool, LMD_TYPE_ARRAY, sizeof(TypeArray));
    TypeArray* type = (TypeArray*)ast_node->type;

    Type* nested_type = NULL;
    bool has_value_item = false;
    for (AstNode* item = items; item; item = item->next) {
        // Let bindings are transparent: they establish a local name but do
        // not occupy a runtime array slot in either parser front end.
        if (item->node_type == AST_NODE_ASSIGN) continue;
        if (!has_value_item) {
            nested_type = item->type;
            has_value_item = true;
        }
        else if (nested_type && (!item->type ||
                item->type->type_id != nested_type->type_id)) {
            nested_type = NULL;
        }
        else if (nested_type && nested_type->type_id == LMD_TYPE_NUM_SIZED &&
                type_num_sized_kind(nested_type) != type_num_sized_kind(item->type)) {
            nested_type = NULL;
        }
        type->length++;
    }
    ast_node->item = items;
    type->nested = nested_type;
    log_debug("build array from items: nested type %d",
        nested_type ? nested_type->type_id : -1);
    return (AstNode*)ast_node;
}



// check if an identifier is a path scheme keyword
// returns the PathScheme if it is, or -1 if not


// Add a namespace binding to the transpiler context
static void add_namespace(Transpiler* tp, String* prefix, Target* target) {
    NamespaceEntry* entry = (NamespaceEntry*)pool_calloc(tp->pool, sizeof(NamespaceEntry));
    entry->prefix = prefix;
    entry->target = target;
    entry->next = tp->namespaces;
    tp->namespaces = entry;
    log_debug("namespace added: %.*s", (int)prefix->len, prefix->chars);
}

// Lookup a namespace by prefix string
// returns the NamespaceEntry if found, NULL if not
static NamespaceEntry* lookup_namespace(Transpiler* tp, String* prefix) {
    NamespaceEntry* entry = tp->namespaces;
    while (entry) {
        if (entry->prefix->len == prefix->len &&
            memcmp(entry->prefix->chars, prefix->chars, prefix->len) == 0) {
            return entry;
        }
        entry = entry->next;
    }
    return NULL;
}

// Lookup a namespace by prefix StrView


// check if a member_expr chain starts with a path scheme (file, http, https, sys)
// and collect all segment names if so
// returns the PathScheme if it's a path, or -1 if it's a regular member expression




static AstNode* build_namespace_symbol_from_parts(Transpiler* tp,
        SourceSpan span, String* prefix, String* field,
        NamespaceEntry* ns_entry) {
    if (!tp || !prefix || !field || !ns_entry) return NULL;
    size_t total_len = prefix->len + 1 + field->len;
    char* qualified = (char*)pool_alloc(tp->pool, total_len + 1);
    memcpy(qualified, prefix->chars, prefix->len);
    qualified[prefix->len] = '.';
    memcpy(qualified + prefix->len + 1, field->chars, field->len);
    qualified[total_len] = '\0';

    TypeString* sym_type = (TypeString*)alloc_type(tp->pool, LMD_TYPE_SYMBOL,
        sizeof(TypeString));
    sym_type->is_const = 1;
    sym_type->is_literal = 1;
    Symbol* symbol = (Symbol*)pool_alloc(tp->pool, sizeof(Symbol) + total_len + 1);
    symbol->ns = ns_entry->target;
    symbol->len = total_len;
    memcpy(symbol->chars, qualified, total_len);
    symbol->chars[total_len] = '\0';
    sym_type->string = (String*)symbol;
    arraylist_append(tp->const_list, symbol);
    sym_type->const_index = tp->const_list->length - 1;

    AstPrimaryNode* result = (AstPrimaryNode*)alloc_ast_node_from_span(tp,
        AST_NODE_PRIMARY, span, sizeof(AstPrimaryNode));
    result->type = (Type*)sym_type;
    return (AstNode*)result;
}

// Forward declaration: check if AST node contains ~ (current_item) reference
bool has_current_item_ref(AstNode* node);


static bool strview_matches_string(StrView* view, String* str) {
    return view && str && view->length == str->len &&
        strncmp(view->str, str->chars, view->length) == 0;
}

#ifndef SIMPLE_SCHEMA_PARSER
static void add_jube_module_import(Transpiler* tp, String* module, String* alias) {
    JubeModuleImport* entry = (JubeModuleImport*)pool_calloc(tp->pool, sizeof(JubeModuleImport));
    entry->module = module;
    entry->alias = alias;
    entry->next = tp->jube_module_imports;
    tp->jube_module_imports = entry;
}
#endif

static const char* registered_jube_module_name(StrView* name) {
#ifndef SIMPLE_SCHEMA_PARSER
    char module_name[128];
    if (!name || name->length >= sizeof(module_name)) return NULL;
    memcpy(module_name, name->str, name->length);
    module_name[name->length] = '\0';
    jube_register_builtin_modules();
    const JubeModuleDef* module = jube_find_static_module(module_name);
    return module ? module->name : NULL;
#else
    (void)name;
    return NULL;
#endif
}

// Check if an identifier matches a built-in or descriptor-backed module name,
// or a registered alias for one. Returns the real module name if matched.
static const char* resolve_imported_module(Transpiler* tp, StrView* name) {
    if (strview_equal(name, "math")) return "math";
    if (strview_equal(name, "io")) return "io";
    // check aliases
    if (strview_matches_string(name, tp->builtin_alias_math)) return "math";
    if (strview_matches_string(name, tp->builtin_alias_io)) return "io";
    for (JubeModuleImport* import = tp->jube_module_imports; import; import = import->next) {
        if (import->alias && strview_matches_string(name, import->alias)) return import->module->chars;
    }
    return registered_jube_module_name(name);
}

static SysFuncInfo* lookup_module_prefixed_sys_func(const char* module, StrView* func_name, int arg_count) {
    if (!module || !func_name) return NULL;
    char prefixed[128];
    snprintf(prefixed, sizeof(prefixed), "%s_%.*s",
        module, (int)func_name->length, func_name->str);
    StrView prefixed_view = strview_from_cstr(prefixed);
    return get_sys_func_info(&prefixed_view, arg_count);
}

static SysFuncInfo* lookup_global_imported_sys_func(Transpiler* tp, StrView* func_name, int arg_count) {
    const char* modules[] = { NULL, NULL };
    int mod_count = 0;
    if (tp->builtin_import_math) modules[mod_count++] = "math";
    if (tp->builtin_import_io) modules[mod_count++] = "io";
    for (int mi = 0; mi < mod_count; mi++) {
        SysFuncInfo* info = lookup_module_prefixed_sys_func(modules[mi], func_name, arg_count);
        if (info) return info;
    }
    for (JubeModuleImport* import = tp->jube_module_imports; import; import = import->next) {
        if (import->alias) continue;
        SysFuncInfo* info = lookup_module_prefixed_sys_func(import->module->chars, func_name, arg_count);
        if (info) return info;
    }
    return NULL;
}

static SysFuncInfo* lookup_complex_math_builtin(StrView* func_name, int arg_count) {
    static const char* names[] = {"sqrt", "exp", "log", "sin", "cos", "tan"};
    for (int i = 0; i < (int)(sizeof(names) / sizeof(names[0])); i++) {
        if (!strview_equal(func_name, names[i])) continue;
        char qualified_name[16];
        snprintf(qualified_name, sizeof(qualified_name), "math_%s", names[i]);
        StrView qualified_view = strview_from_cstr(qualified_name);
        return get_sys_func_info(&qualified_view, arg_count);
    }
    return NULL;
}

static bool start_option_name_is(AstNamedNode* option, const char* name) {
    size_t length = strlen(name);
    return option && option->name && option->name->len == (int)length &&
        memcmp(option->name->chars, name, length) == 0;
}

static const char* start_literal_symbol(AstNode* node) {
    while (node && node->node_type == AST_NODE_PRIMARY &&
            ((AstPrimaryNode*)node)->expr) {
        node = ((AstPrimaryNode*)node)->expr;
    }
    if (!node || !node->type || node->type->type_id != LMD_TYPE_SYMBOL ||
            !node->type->is_literal) return NULL;
    Symbol* symbol = (Symbol*)((TypeSymbol*)node->type)->string;
    return symbol ? symbol->chars : NULL;
}

static bool parse_start_options_span(Transpiler* tp, AstNode* options,
        SourceSpan span, StartMode* mode) {
    *mode = START_MODE_TASK;
    if (!options) return true;
    AstNode* root = ast_unwrap_primary(options);
    if (!root || root->node_type != AST_NODE_MAP) {
        record_semantic_error_span(tp, span, ERR_INVALID_OPERATION,
            "`start` options must be a map literal");
        return false;
    }

    bool saw_mode = false;
    bool valid = true;
    for (AstNode* item = ((AstMapNode*)root)->item; item; item = item->next) {
        if (item->node_type != AST_NODE_KEY_EXPR) {
            record_semantic_error_span(tp, span, ERR_INVALID_OPERATION,
                "`start` options cannot use spread fields");
            valid = false;
            continue;
        }
        AstNamedNode* option = (AstNamedNode*)item;
        if (!start_option_name_is(option, "mode")) {
            record_semantic_error_span(tp, span, ERR_INVALID_OPERATION,
                "unknown `start` option '%.*s'",
                option->name ? option->name->len : 0,
                option->name ? option->name->chars : "");
            valid = false;
            continue;
        }
        if (saw_mode) {
            record_semantic_error_span(tp, span, ERR_INVALID_OPERATION,
                "duplicate `start` option 'mode'");
            valid = false;
            continue;
        }
        saw_mode = true;
        const char* value = start_literal_symbol(option->as);
        if (!value) {
            record_semantic_error_span(tp, span, ERR_INVALID_OPERATION,
                "`start` option 'mode' must be the literal symbol 'task', 'thread', or 'process'");
            valid = false;
        } else if (strcmp(value, "task") == 0) {
            *mode = START_MODE_TASK;
        } else if (strcmp(value, "thread") == 0) {
            *mode = START_MODE_THREAD;
        } else if (strcmp(value, "process") == 0) {
            *mode = START_MODE_PROCESS;
        } else {
            record_semantic_error_span(tp, span, ERR_INVALID_OPERATION,
                "invalid `start` mode '%s'; expected 'task', 'thread', or 'process'", value);
            valid = false;
        }
    }

    if (valid && *mode != START_MODE_TASK) {
        const char* value = *mode == START_MODE_THREAD ? "thread" : "process";
        record_semantic_error_span(tp, span, ERR_NOT_IMPLEMENTED,
            "`start` mode '%s' is not implemented yet; use 'task'", value);
        valid = false;
    }
    return valid;
}

static bool start_has_named_arguments(AstNode* target) {
    for (AstNode* arg = target; arg; arg = arg->next) {
        if (arg->node_type == AST_NODE_NAMED_ARG) return true;
    }
    return false;
}

static void validate_start_parts(Transpiler* tp, AstStartNode* start,
        SourceSpan span, AstNode* target, AstNode* args, AstNode* options,
        AstCallNode* target_call) {
    TypeFunc* fn_type = target && target->type &&
        target->type->type_id == LMD_TYPE_FUNC ? (TypeFunc*)target->type : NULL;
    if (!fn_type || !fn_type->is_proc) {
        record_semantic_error_span(tp, span, ERR_INVALID_CALL,
            "`start` first argument must resolve to a procedure (pn)");
        start->type = &TYPE_ERROR;
    }
    if (args && (!args->type || args->type->type_id != LMD_TYPE_ARRAY)) {
        record_semantic_error_span(tp, span, ERR_ARGUMENT_TYPE_MISMATCH,
            "`start` second argument must be an argument array");
        start->type = &TYPE_ERROR;
    }
    if (!parse_start_options_span(tp, options, span, &start->mode)) {
        start->type = &TYPE_ERROR;
    }
    AstNode* args_root = args ? ast_unwrap_primary(args) : NULL;
    if (fn_type && args_root && args_root->node_type == AST_NODE_ARRAY) {
        AstArrayNode* literal_args = (AstArrayNode*)args_root;
        for (AstNode* item = literal_args->item; item; item = item->next) {
            if (item->node_type == AST_NODE_ASSIGN) {
                record_semantic_error_span(tp, span, ERR_INVALID_OPERATION,
                    "`start` argument arrays cannot contain declarations");
                start->type = &TYPE_ERROR;
                return;
            }
        }
        AstCallNode validation_call = *target_call;
        validation_call.argument = literal_args->item;
        if (!lambda_ast_validate_call_arguments(tp, &validation_call, span,
                (int)((TypeArray*)literal_args->type)->length)) {
            start->type = &TYPE_ERROR;
        }
    }
}



static bool validate_lambda_argument_limit(Transpiler* tp,
        SourceSpan span,
        int count, const char* subject) {
    if (count <= LAMBDA_MAX_FUNCTION_ARGS) return true;
    record_semantic_error_span(tp, span, ERR_FUNCTION_ARGUMENT_LIMIT,
        "%s count %d exceeds Core Lambda limit %d; use a rest parameter or an array/map",
        subject, count, LAMBDA_MAX_FUNCTION_ARGS);
    return false;
}

bool lambda_ast_validate_call_arguments(Transpiler* tp, AstCallNode* call,
        SourceSpan diagnostic_span, int arg_count) {
    if (!call || !call->function || !call->function->type ||
            call->function->type->type_id != LMD_TYPE_FUNC) return true;

    TypeFunc* func_type = (TypeFunc*)call->function->type;
    TypeParam* expected_param = func_type->param;
    AstNode* arg = call->argument;
    int arg_index = 0;
    int line = (int)lambda_source_span_start_point(tp->source,
        diagnostic_span).row + 1;
    String* var_arg_roots[64];
    int var_arg_root_count = 0;
    bool parameter_short_circuits_error = false;

    // A statically resolved function cannot manufacture missing values. The
    // validator is shared by direct calls and start(pn, literal_args), keeping
    // launch syntax from weakening the ordinary call contract.
    if (ast_called_function_signature_ready(call->function) &&
            (arg_count < func_type->required_param_count ||
             (!func_type->is_variadic && arg_count > func_type->param_count))) {
        // Describe the accepted arity as a RANGE when optional parameters make
        // it one. Reporting only `required_param_count` said "expects 1
        // argument" for `fn g(a, b?)`, which is wrong in both directions — it
        // understates what the function takes and, since S12.3.6 makes optional
        // parameters the sanctioned answer to overloading, it is the shape
        // callers now hit most.
        char expected[64];
        int min_args = func_type->required_param_count;
        int max_args = func_type->param_count;
        if (func_type->is_variadic) {
            snprintf(expected, sizeof(expected), "%d or more arguments", min_args);
        } else if (min_args >= max_args) {
            snprintf(expected, sizeof(expected), "%d argument%s", min_args,
                min_args == 1 ? "" : "s");
        } else {
            snprintf(expected, sizeof(expected), "%d to %d arguments",
                min_args, max_args);
        }
        record_type_error_code(tp, line, ERR_ARGUMENT_COUNT_MISMATCH,
            "function expects %s, got %d", expected, arg_count);
        if (!should_continue_transpiling(tp)) {
            call->type = &TYPE_ERROR;
            return false;
        }
    }

    while (arg && expected_param) {
        if (expected_param->is_var_param) {
            AstIdentNode* root = compound_root_ident(arg);
            if (!root || !root->entry || !root->entry->is_mutable) {
                record_semantic_error_span(tp, diagnostic_span, ERR_IMMUTABLE_ASSIGNMENT,
                    "argument %d for `var` parameter must be a mutable `var` binding",
                    arg_index + 1);
                if (!should_continue_transpiling(tp)) {
                    call->type = &TYPE_ERROR;
                    return false;
                }
            } else {
                for (int i = 0; i < var_arg_root_count; i++) {
                    if (same_name_string(var_arg_roots[i], root->name)) {
                        record_semantic_error_span(tp, diagnostic_span, ERR_IMMUTABLE_ASSIGNMENT,
                            "argument %d overlaps another `var` parameter; pass distinct mutable bindings",
                            arg_index + 1);
                        break;
                    }
                }
                if (var_arg_root_count < 64) {
                    var_arg_roots[var_arg_root_count++] = root->name;
                }
            }
            if (!type_exact_match(arg->type, expected_param)) {
                Type* full_type = parameter_boundary_type(expected_param);
                char expected_name[128];
                char actual_name[128];
                lambda_type_format_name(full_type, expected_name, sizeof(expected_name));
                lambda_type_format_name(arg->type, actual_name, sizeof(actual_name));
                record_type_error_code(tp, line, ERR_ARGUMENT_TYPE_MISMATCH,
                    "argument %d for `var` parameter must match exactly: expected %s, got %s; declare as any[] or use a value parameter",
                    arg_index + 1, expected_name, actual_name);
                if (!should_continue_transpiling(tp)) {
                    call->type = &TYPE_ERROR;
                    return false;
                }
            }
        }
        Type* full_type = parameter_boundary_type(expected_param);
        if (expected_param->contract_type &&
                !lambda_type_accepts_error(expected_param->contract_type) &&
                lambda_type_accepts_error(arg->type)) {
            parameter_short_circuits_error = true;
        }
        StaticBoundaryResult relation = lambda_type_accepts_error(arg->type)
            ? static_parameter_boundary_relation(arg->type, full_type)
            : static_boundary_relation(arg->type, full_type);
        bool compatible = relation != STATIC_BOUNDARY_REJECTED;
        if (!compatible) compatible = typed_array_argument_compatible(arg, full_type);
        if (arg->type && !compatible) {
            char expected_name[128];
            char actual_name[128];
            lambda_type_format_name(full_type, expected_name, sizeof(expected_name));
            lambda_type_format_name(arg->type, actual_name, sizeof(actual_name));
            record_type_error_code(tp, line, ERR_ARGUMENT_TYPE_MISMATCH,
                "argument %d expected %s, got %s",
                arg_index + 1, expected_name, actual_name);
            if (!should_continue_transpiling(tp)) {
                call->type = &TYPE_ERROR;
                return false;
            }
        }
        arg = arg->next;
        expected_param = expected_param->next;
        arg_index++;
    }
    if (parameter_short_circuits_error) {
        call->type = lambda_type_union_normalized(tp->pool, call->type, &TYPE_ERROR);
    }
    return true;
}





NameEntry* lookup_name(Transpiler* tp, StrView var_name) {
    // lookup the name
    NameScope* scope = tp->current_scope;
    FIND_VAR_NAME:
    NameEntry* entry = scope->first;
    // Cycle guard: the old fixed 1000-entry cap fired on legitimately large
    // module scopes (thousands of top-level lets), silently returning NULL and
    // leaving idents entry-less — the JIT recovered via its name-keyed global
    // table, but the T0 interpreter read null (tier mismatch, SI3v2). Use
    // tortoise-hare so only a genuinely circular entry list bails out.
    NameEntry* chase = scope->first;
    while (entry) {
        if (chase) {
            chase = chase->next ? chase->next->next : NULL;
            if (chase && chase == entry) {
                log_error("ERROR: circular entry list detected in scope");
                return NULL;
            }
        }

        StrView entry_name = strview_init(entry->name->chars, entry->name->len);
        // no per-comparison trace here: this loop runs O(scope_size) per lookup
        // and a large module scope (thousands of lets) turns a per-entry
        // log_debug into tens of millions of log writes that dominate build time.
        if (strview_eq(&entry_name, &var_name)) {
            break;
        }
        entry = entry->next;
    }
    if (!entry) {
        if (scope->parent) {
            // Defensive check: prevent infinite loop if parent pointer is circular
            if (scope == scope->parent) {
                log_error("Error: circular parent scope detected - breaking to prevent infinite loop");
                return NULL;
            }
            scope = scope->parent;
            log_debug("checking parent scope: %p", scope);
            goto FIND_VAR_NAME;
        }
        log_debug("missing identifier %.*s", (int)var_name.length, var_name.str);
        return NULL;
    }
    else {
        log_debug("found identifier %.*s", (int)entry->name->len, entry->name->chars);
        return entry;
    }
}

AstNode* build_identifier_from_span(Transpiler* tp, SourceSpan span) {
    log_debug("building identifier");
    AstIdentNode* ast_node = (AstIdentNode*)alloc_ast_node_from_span(tp,
        AST_NODE_IDENT, span, sizeof(AstIdentNode));

    // get the identifier name from source and create pooled string
    StrView var_name = {.str = tp->source + span.start_byte,
        .length = lambda_source_span_length(span)};
    ast_node->name = name_pool_create_strview(tp->name_pool, var_name);

    // lookup the name
    log_debug("looking up name: %.*s", (int)var_name.length, var_name.str);
    NameEntry* entry = lookup_name(tp, var_name);
    if (!entry) {
        // In 'that' clause, rewrite bare identifier to ~.name (member access on current item)
        // Name resolution order: 1) scope names, 2) ~.name fields, 3) system properties
        if (tp->in_that_clause) {
            log_debug("that clause: rewriting bare '%.*s' to ~.%.*s",
                (int)var_name.length, var_name.str, (int)var_name.length, var_name.str);
            AstFieldNode* field_node = (AstFieldNode*)alloc_ast_node_from_span(tp,
                AST_NODE_MEMBER_EXPR, span, sizeof(AstFieldNode));
            // create ~ (current item) as the object
            AstNode* current_item = alloc_ast_node_from_span(tp,
                AST_NODE_CURRENT_ITEM, span, sizeof(AstNode));
            current_item->type = alloc_type(tp->pool, LMD_TYPE_ANY, sizeof(Type));
            field_node->object = current_item;
            // use the identifier as the field name (without scope lookup)
            ast_node->type = set_type_any(tp, ANY_DYNAMIC_NAME);
            field_node->field = (AstNode*)ast_node;
            field_node->type = set_type_any(tp, ANY_DYNAMIC_NAME);
            return (AstNode*)field_node;
        }
        // Global import: resolve math constants (pi, e) when `import math;` is active
        if (tp->builtin_import_math) {
            double const_val = 0.0;
            bool is_math_const = false;
            if (var_name.length == 2 && memcmp(var_name.str, "pi", 2) == 0) {
                const_val = 3.14159265358979323846;
                is_math_const = true;
            } else if (var_name.length == 1 && var_name.str[0] == 'e') {
                const_val = 2.71828182845904523536;
                is_math_const = true;
            }
            if (is_math_const) {
                TypeFloat* ft = (TypeFloat*)alloc_type(tp->pool, LMD_TYPE_FLOAT, sizeof(TypeFloat));
                ft->double_val = const_val;
                arraylist_append(tp->const_list, &ft->double_val);
                ft->const_index = tp->const_list->length - 1;
                ft->is_const = 1;  ft->is_literal = 1;
                AstPrimaryNode* pn = (AstPrimaryNode*)alloc_ast_node_from_span(tp,
                    AST_NODE_PRIMARY, span, sizeof(AstPrimaryNode));
                pn->type = (Type*)ft;
                log_debug("global import math constant resolved: %.*s", (int)var_name.length, var_name.str);
                return (AstNode*)pn;
            }
        }
        SysFuncInfo* sys_value = get_unambiguous_sys_func_value(&var_name);
        if (sys_value) {
            AstSysFuncNode* sys_node = (AstSysFuncNode*)alloc_ast_node_from_span(tp,
                AST_NODE_SYS_FUNC, span, sizeof(AstSysFuncNode));
            TypeFunc* fn_type = (TypeFunc*)alloc_type(tp->pool, LMD_TYPE_FUNC, sizeof(TypeFunc));
            fn_type->is_variadic = sys_value->arg_count < 0;
            fn_type->param_count = fn_type->is_variadic ? 0 : sys_value->arg_count;
            fn_type->required_param_count = fn_type->is_variadic ? 0 : fn_type->param_count;
            fn_type->returned = sys_value->return_type;
            fn_type->inferred_return = sys_value->success_type
                ? sys_value->success_type : sys_value->return_type;
            set_function_return_contract(fn_type, sys_value->return_type, true);
            fn_type->is_proc = sys_value->is_proc;
            fn_type->can_raise = sys_value->can_raise;
            fn_type->may_return_error = sys_value->may_return_error;
            sys_node->fn_info = sys_value;
            sys_node->type = (Type*)fn_type;
            return (AstNode*)sys_node;
        }
        // ident is used for member access, thus we return TYPE_ANY
        ast_node->type = set_type_any(tp, ANY_DYNAMIC_NAME);
    }
    else {
        log_debug("found identifier %.*s", (int)entry->name->len, entry->name->chars);
        ast_node->entry = entry;
        if (entry->import && entry->node->type->type_id != LMD_TYPE_FUNC) {
            // clone and remove is_const flag
            // todo: full type clone
            log_debug("got imported identifier %.*s from module %.*s",
                (int)entry->name->len, entry->name->chars,
                (int)entry->import->module.length, entry->import->module.str);
            if (entry->node->type->type_id == LMD_TYPE_TYPE) {
                // for imported type definitions (pub type T = ...), preserve the full TypeType wrapper
                ast_node->type = entry->node->type;
            } else {
                // For container types (array, list, map, element, etc.), use the
                // original type directly to preserve nested type info (e.g.
                // TypeArray::nested). Allocating a bare Type loses this info,
                // causing wrong accessor functions (e.g. array_get vs array_int_get).
                Type* orig = entry->node->type;
                TypeId tid = orig->type_id;
                if (tid >= LMD_TYPE_CONTAINER) {
                    ast_node->type = orig;
                } else {
                    ast_node->type = alloc_type(tp->pool, tid, sizeof(Type));
                    ast_node->type->is_const = 0;
                }
            }
        }
        else {
            log_debug("Debug: entry->node->type is %p for identifier %.*s",
                entry->node->type, (int)entry->name->len, entry->name->chars);
            ast_node->type = entry->node->type;
            if (entry->node->node_type == AST_NODE_PARAM && entry->node->type &&
                    entry->node->type->kind == TYPE_KIND_PARAM) {
                TypeParam* pt = (TypeParam*)entry->node->type;
                // The compact TypeParam prefix selects the call ABI, but it
                // hid an implicit `any \\ error` in recursive expression typing.
                // Reads must expose the retained source contract so `or` and
                // return inference cannot manufacture a meta-type union.
                ast_node->type = pt->contract_type ? pt->contract_type :
                    (pt->full_type ? pt->full_type : (Type*)pt);
            }
            if (entry->is_mutable && entry->type_widened && !entry->has_type_annotation) {
                // widened vars must read as ANY so later map/element shapes do not
                // retain the null-shaped initializer and discard reassigned values.
                ast_node->type = set_type_any(tp, ANY_WIDENED_VAR);
            }
            if (!ast_node->type) {
                log_warn("Warning: entry->node->type is null for identifier %.*s, using TYPE_ANY",
                    (int)entry->name->len, entry->name->chars);
                ast_node->type = set_type_any(tp, ANY_LEGACY_UNCLASSIFIED);
            }
            // Special handling: if identifier refers to a type definition, wrap type in TypeType
            else if (entry->node->node_type == AST_NODE_TYPE_STAM) {
                TypeType* type_type = (TypeType*)alloc_type(tp->pool, LMD_TYPE_TYPE, sizeof(TypeType));
                type_type->type = entry->node->type;
                ast_node->type = (Type*)type_type;
                log_debug("Wrapped type definition %.*s in TypeType",
                    (int)entry->name->len, entry->name->chars);
            }
            // Handle string/symbol pattern definitions - wrap in TypeType for use in type expressions
            else if (entry->node->node_type == AST_NODE_STRING_PATTERN ||
                     entry->node->node_type == AST_NODE_SYMBOL_PATTERN) {
                TypeType* type_type = (TypeType*)alloc_type(tp->pool, LMD_TYPE_TYPE, sizeof(TypeType));
                type_type->type = entry->node->type;  // TypePattern*
                ast_node->type = (Type*)type_type;
                log_debug("Wrapped pattern definition %.*s in TypeType",
                    (int)entry->name->len, entry->name->chars);
            }
        }
        if (ast_node->type) {
            log_debug("ident %p type: %d", ast_node->type, ast_node->type->type_id);
        }
        else {
            log_debug("ident %p type: null", ast_node);
        }
    }
    return (AstNode*)ast_node;
}



static Type* build_lit_string_from_span(Transpiler* tp, SourceSpan span,
        LambdaAstLiteralKind kind) {
    // Phase 3: empty strings are values; empty symbol/binary values remain absent.
    // With single-token strings/symbols, we parse the raw token text directly
    // Binary is now a single token: b'content'
    String* str;
    bool is_binary = kind == LAMBDA_AST_LITERAL_BINARY;
    bool is_symbol = kind == LAMBDA_AST_LITERAL_SYMBOL;
    log_debug("build lit string with kind: %d", kind);

    // Handle binary separately — extract content between b' and '
    if (is_binary) {
        TypeBinaryConst* str_type = (TypeBinaryConst*)alloc_type(tp->pool, LMD_TYPE_BINARY, sizeof(TypeBinaryConst));
        str_type->is_const = 1;  str_type->is_literal = 1;

        const char* raw = tp->source + span.start_byte;
        int raw_len = (int)lambda_source_span_length(span);

        // Skip b' prefix (2 chars) and trailing ' (1 char)
        const char* content_start = raw + 2;
        int content_len = raw_len - 3;

        StrBuf* decoded = strbuf_new_cap((size_t)content_len + 1);
        int err_off = 0;
        int decoded_len = decoded ?
            str_binary_payload_decode(content_start, content_len, decoded, &err_off) : -1;
        if (decoded_len < 0) {
            // Binary constants must enter the runtime as bytes; retaining malformed
            // source text here makes every later length/print operation ambiguous.
            record_semantic_error_span(tp, span, ERR_SYNTAX_ERROR,
                "invalid binary literal payload at byte %d", err_off);
            if (decoded) strbuf_free(decoded);
            return &TYPE_ERROR;
        }
        if (decoded_len == 0) {
            log_debug("build_lit_string: empty binary literal, returning null type");
            strbuf_free(decoded);
            return &LIT_NULL;
        }

        Binary* binary = pool_binary_from_bytes(tp->pool, decoded->str, (size_t)decoded_len);
        str_type->binary = binary;
        strbuf_free(decoded);

        arraylist_append(tp->const_list, binary);
        str_type->const_index = tp->const_list->length - 1;
        return (Type*)str_type;
    }

    int raw_len = (int)lambda_source_span_length(span);
    const char* raw = tp->source + span.start_byte;

    // Determine quote character and content boundaries
    char quote_char = is_symbol ? '\'' : '"';  // symbols use single quotes

    // Skip opening quote
    if (raw_len < 2 || raw[0] != quote_char || raw[raw_len - 1] != quote_char) {
        log_error("Invalid string literal format: %.*s", raw_len, raw);
        return &LIT_NULL;
    }

    const char* content_start = raw + 1;
    int content_len = raw_len - 2;  // exclude both quotes

    // Empty symbol literals are rejected by grammar; keep a null fallback for generated/stale parsers.
    if (content_len == 0) {
        if (is_symbol) {
            log_debug("build_lit_string: empty symbol literal, returning null type");
            return &LIT_NULL;
        }
    }

    TypeString* str_type = (TypeString*)alloc_type(tp->pool,
        is_symbol ? LMD_TYPE_SYMBOL : LMD_TYPE_STRING, sizeof(TypeString));
    str_type->is_const = 1;  str_type->is_literal = 1;

    // Check if there are any escape sequences in the content
    bool has_escape = false;
    for (int i = 0; i < content_len; i++) {
        if (content_start[i] == '\\') {
            has_escape = true;
            break;
        }
    }

    if (!has_escape) {
        // No escapes - simple copy
        if (is_symbol) {
            // Allocate as Symbol (has ns field before chars)
            Symbol* sym = (Symbol*)pool_alloc(tp->pool, sizeof(Symbol) + content_len + 1);
            sym->ns = NULL;
            memcpy(sym->chars, content_start, content_len);
            sym->chars[content_len] = '\0';
            sym->len = content_len;
            str = (String*)sym;  // store as String* in TypeString (const pool uses raw pointer)
        } else {
            str = (String*)pool_alloc(tp->pool, sizeof(String) + content_len + 1);
            memcpy(str->chars, content_start, content_len);
            str->chars[content_len] = '\0';
            str->len = content_len;
            str->flags = 0;
            str->is_ascii = str_is_ascii(str->chars, content_len) ? 1 : 0;
        }
        str_type->string = str;
    }
    else {
        // Has escape sequences - process them
        StringBuf *str_buf = stringbuf_new(tp->pool);

        for (int i = 0; i < content_len; i++) {
            if (content_start[i] == '\\' && i + 1 < content_len) {
                char escape_char = content_start[i + 1];
                switch (escape_char) {
                case '"':
                    stringbuf_append_char(str_buf, '"');
                    i++;
                    break;
                case '\'':
                    stringbuf_append_char(str_buf, '\'');
                    i++;
                    break;
                case '\\':
                    stringbuf_append_char(str_buf, '\\');
                    i++;
                    break;
                case '/':
                    stringbuf_append_char(str_buf, '/');
                    i++;
                    break;
                case 'b':
                    stringbuf_append_char(str_buf, '\b');
                    i++;
                    break;
                case 'f':
                    stringbuf_append_char(str_buf, '\f');
                    i++;
                    break;
                case 'n':
                    stringbuf_append_char(str_buf, '\n');
                    i++;
                    break;
                case 'r':
                    stringbuf_append_char(str_buf, '\r');
                    i++;
                    break;
                case 't':
                    stringbuf_append_char(str_buf, '\t');
                    i++;
                    break;
                case 'u':
                    // Handle Unicode escape sequences: \uXXXX or \u{...}
                    if (i + 5 < content_len && content_start[i + 2] != '{') {
                        // \uXXXX format (exactly 4 hex digits)
                        char hex_digits[5] = {0};
                        memcpy(hex_digits, content_start + i + 2, 4);
                        char* endptr;
                        uint32_t code_point = strtoul(hex_digits, &endptr, 16);
                        if (endptr == hex_digits + 4) {
                            // Check for surrogate pairs (used for characters > U+FFFF like emojis)
                            // High surrogate: 0xD800-0xDBFF, Low surrogate: 0xDC00-0xDFFF
                            if (code_point >= 0xD800 && code_point <= 0xDBFF) {
                                // This is a high surrogate, look for low surrogate
                                if (i + 11 < content_len &&
                                    content_start[i + 6] == '\\' && content_start[i + 7] == 'u') {
                                    char hex_low[5] = {0};
                                    memcpy(hex_low, content_start + i + 8, 4);
                                    char* endptr_low;
                                    uint32_t low_surrogate = strtoul(hex_low, &endptr_low, 16);
                                    if (endptr_low == hex_low + 4 &&
                                        low_surrogate >= 0xDC00 && low_surrogate <= 0xDFFF) {
                                        // Valid surrogate pair - combine into full codepoint
                                        code_point = 0x10000 + ((code_point - 0xD800) << 10) + (low_surrogate - 0xDC00);
                                        i += 6; // skip extra \uXXXX for low surrogate
                                    } else {
                                        // Not a valid low surrogate, output replacement char
                                        code_point = 0xFFFD;
                                    }
                                } else {
                                    // Lone high surrogate - output replacement character
                                    code_point = 0xFFFD;
                                }
                            } else if (code_point >= 0xDC00 && code_point <= 0xDFFF) {
                                // Lone low surrogate - output replacement character
                                code_point = 0xFFFD;
                            }

                            // Convert Unicode code point to UTF-8
                            if (code_point <= 0x7F) {
                                stringbuf_append_char(str_buf, (char)code_point);
                            } else if (code_point <= 0x7FF) {
                                stringbuf_append_char(str_buf, 0xC0 | (code_point >> 6));
                                stringbuf_append_char(str_buf, 0x80 | (code_point & 0x3F));
                            } else if (code_point <= 0xFFFF) {
                                stringbuf_append_char(str_buf, 0xE0 | (code_point >> 12));
                                stringbuf_append_char(str_buf, 0x80 | ((code_point >> 6) & 0x3F));
                                stringbuf_append_char(str_buf, 0x80 | (code_point & 0x3F));
                            } else {
                                // 4-byte UTF-8 encoding for code points > 0xFFFF (emojis, etc.)
                                stringbuf_append_char(str_buf, 0xF0 | (code_point >> 18));
                                stringbuf_append_char(str_buf, 0x80 | ((code_point >> 12) & 0x3F));
                                stringbuf_append_char(str_buf, 0x80 | ((code_point >> 6) & 0x3F));
                                stringbuf_append_char(str_buf, 0x80 | (code_point & 0x3F));
                            }
                            i += 5;  // skip \uXXXX
                        } else {
                            log_error("Invalid Unicode escape: \\u%s", hex_digits);
                            stringbuf_append_char(str_buf, '\\');
                            stringbuf_append_char(str_buf, 'u');
                            i++;
                        }
                    }
                    else if (i + 3 < content_len && content_start[i + 2] == '{') {
                        // \u{...} format (variable length hex digits)
                        const char* hex_start = content_start + i + 3;
                        const char* hex_end = NULL;
                        for (const char* p = hex_start; p < content_start + content_len; p++) {
                            if (*p == '}') {
                                hex_end = p;
                                break;
                            }
                        }
                        if (hex_end && hex_end > hex_start) {
                            StrView hex_source = {hex_start,
                                (size_t)(hex_end - hex_start)};
                            char* hex_str = ast_copy_source_text(tp, hex_source, span);
                            if (!hex_str) return &TYPE_ERROR;
                            char* endptr;
                            uint32_t code_point = strtoul(hex_str, &endptr, 16);
                            if (endptr == hex_str + hex_source.length && code_point <= 0x10FFFF) {
                                // Convert Unicode code point to UTF-8
                                if (code_point <= 0x7F) {
                                    stringbuf_append_char(str_buf, (char)code_point);
                                } else if (code_point <= 0x7FF) {
                                    stringbuf_append_char(str_buf, 0xC0 | (code_point >> 6));
                                    stringbuf_append_char(str_buf, 0x80 | (code_point & 0x3F));
                                } else if (code_point <= 0xFFFF) {
                                    stringbuf_append_char(str_buf, 0xE0 | (code_point >> 12));
                                    stringbuf_append_char(str_buf, 0x80 | ((code_point >> 6) & 0x3F));
                                    stringbuf_append_char(str_buf, 0x80 | (code_point & 0x3F));
                                } else {
                                    // 4-byte UTF-8 encoding for code points > 0xFFFF
                                    stringbuf_append_char(str_buf, 0xF0 | (code_point >> 18));
                                    stringbuf_append_char(str_buf, 0x80 | ((code_point >> 12) & 0x3F));
                                    stringbuf_append_char(str_buf, 0x80 | ((code_point >> 6) & 0x3F));
                                    stringbuf_append_char(str_buf, 0x80 | (code_point & 0x3F));
                                }
                                i = (hex_end - content_start);  // position at '}'
                            } else {
                                log_error("Invalid Unicode escape: \\u{%s}", hex_str);
                                stringbuf_append_char(str_buf, '\\');
                                stringbuf_append_char(str_buf, 'u');
                                i++;
                            }
                            mem_free(hex_str);
                        } else {
                            log_error("Malformed Unicode escape sequence");
                            stringbuf_append_char(str_buf, '\\');
                            stringbuf_append_char(str_buf, 'u');
                            i++;
                        }
                    } else {
                        log_error("Invalid Unicode escape sequence length");
                        stringbuf_append_char(str_buf, '\\');
                        stringbuf_append_char(str_buf, 'u');
                        i++;
                    }
                    break;
                default:
                    // Unknown escape sequence, keep as-is
                    log_warn("Unknown escape sequence: \\%c", escape_char);
                    stringbuf_append_char(str_buf, '\\');
                    stringbuf_append_char(str_buf, escape_char);
                    i++;
                    break;
                }
            } else {
                stringbuf_append_char(str_buf, content_start[i]);
            }
        }

        // Convert StringBuf to String
        str = stringbuf_to_string(str_buf);
        str->flags = 0;
        str->is_ascii = str_is_ascii(str->chars, str->len) ? 1 : 0;
        log_debug("final string: %.*s", str->len, str->chars);

        // Escapes can only produce empty solid values through generated/stale parsers.
        if (str->len == 0 && is_symbol) {
            log_debug("build_lit_string: empty symbol after escape processing, returning null type");
            return &LIT_NULL;
        }

        // For symbols, re-allocate as Symbol struct (different layout from String)
        if (is_symbol) {
            int slen = str->len;
            Symbol* sym = (Symbol*)pool_alloc(tp->pool, sizeof(Symbol) + slen + 1);
            sym->ns = NULL;
            memcpy(sym->chars, str->chars, slen);
            sym->chars[slen] = '\0';
            sym->len = slen;
            str = (String*)sym;  // store as String* in TypeString
        }
        str_type->string = str;
    }
    // add to const list
    arraylist_append(tp->const_list, str);
    str_type->const_index = tp->const_list->length - 1;
    return (Type*)str_type;
}



static Type* build_lit_datetime_from_span(Transpiler* tp, SourceSpan span) {
    TypeDateTime* dt_type = (TypeDateTime*)alloc_type(tp->pool, LMD_TYPE_DTIME, sizeof(TypeDateTime));
    dt_type->is_const = 1;  dt_type->is_literal = 1;

    // Token is t'...' — skip the t' prefix and trailing '
    const char* raw = tp->source + span.start_byte;
    int raw_len = (int)lambda_source_span_length(span);
    const char* datetime_start = raw + 2;  // skip t'
    int datetime_len = raw_len - 3;        // exclude t' and trailing '

    // Skip leading/trailing whitespace inside the quotes
    while (datetime_len > 0 && *datetime_start == ' ') { datetime_start++; datetime_len--; }
    while (datetime_len > 0 && datetime_start[datetime_len - 1] == ' ') { datetime_len--; }

    // Parse the DateTime string directly using ast_pool
    char* parse_end = NULL;
    DateTime* dt = datetime_parse(tp->pool, datetime_start, DATETIME_PARSE_LAMBDA, &parse_end);

    // Check if parsing was successful
    // On success: dt != NULL and parse_end > datetime_start (parsing progressed)
    // On error: dt == NULL and parse_end == datetime_start (no progress)
    if (dt && parse_end > datetime_start) {
        log_debug("parsed datetime fields: %d, %d, %d, %d, %d",
            dt->year_month, dt->day, dt->hour, dt->minute, dt->second);
    }
    else {
        // Fallback to default if parsing fails
        log_debug("Failed to parse datetime: %.*s, using default", datetime_len, datetime_start);
        return NULL;
    }

    dt_type->datetime = *dt;

    // Add to const list
    arraylist_append(tp->const_list, &dt_type->datetime);
    dt_type->const_index = tp->const_list->length - 1;
    log_debug("build lit datetime: %.*s, type: %d", datetime_len, datetime_start, dt_type->type_id);
    return (Type*)dt_type;
}





static int decimal_literal_significant_digits(const char* str);
static bool n_literal_is_integer(const char* str);

static bool track_decimal_constant(Transpiler* tp, Decimal* decimal) {
    if (!tp || !decimal) return false;
    if (!tp->decimal_constants) {
        tp->decimal_constants = arraylist_new(4);
    }
    if (!tp->decimal_constants ||
            !arraylist_append(tp->decimal_constants, decimal)) {
        decimal_payload_release(decimal);
        return false;
    }
    return true;
}

static Type* build_lit_float_from_span(Transpiler* tp, SourceSpan span) {
    TypeFloat* item_type = (TypeFloat*)alloc_type(tp->pool, LMD_TYPE_FLOAT, sizeof(TypeFloat));
    // C supports inf and nan
    log_debug("build lit float");
    StrView source = source_span_text(tp, span);
    char* number_text = ast_copy_source_text(tp, source, span);
    if (!number_text) return &TYPE_ERROR;
    const char* num_str = number_text;
    // check if there's sign
    bool has_sign = false;
    if (num_str[0] == '-') { has_sign = true;  num_str++; } // skip the sign
    while (*num_str == ' ' || *num_str == '\t' || *num_str == '\n' || *num_str == '\r') { num_str++; } // skip leading spaces
    // str_to_double_default() does not handle 'inf', 'nan' — add special handling
    log_debug("build lit float: %s", num_str);
    // add special handling for "inf", "-inf", "nan"
    if (str_istarts_with_const(num_str, strlen(num_str), "inf")) {
        item_type->double_val = INFINITY;
        log_debug("build lit float: inf");
        if (has_sign) { item_type->double_val = -item_type->double_val; }
    }
    else if (str_istarts_with_const(num_str, strlen(num_str), "nan")) {
        item_type->double_val = NAN;
        log_debug("build lit float: nan");
    }
    else { // normal float parsing
        item_type->double_val = str_to_double_default(num_str, strlen(num_str), 0.0);
        log_debug("build lit float: %s, value: %f", num_str, item_type->double_val);
        if (has_sign) { item_type->double_val = -item_type->double_val; }
    }
    arraylist_append(tp->const_list, &item_type->double_val);
    item_type->const_index = tp->const_list->length - 1;
    item_type->is_const = 1;  item_type->is_literal = 1;
    mem_free(number_text);
    return (Type*)item_type;
}



static Type* build_lit_decimal_poison_from_span(Transpiler* tp,
        SourceSpan span) {
    StrView source = source_span_text(tp, span);
    const char* spelling = strview_equal(&source, "decimal.inf") ? "Infinity" : "NaN";
    TypeDecimal* item_type = (TypeDecimal*)alloc_type(tp->pool, LMD_TYPE_DECIMAL,
        sizeof(TypeDecimal));
    Decimal* decimal = (Decimal*)pool_alloc(tp->pool, sizeof(Decimal));
    if (!decimal) return &TYPE_ERROR;
    decimal->unlimited = 0;
    decimal->dec_val = decimal_parse_str(spelling, decimal_fixed_context());
    if (!decimal->dec_val) return &TYPE_ERROR;
    if (!track_decimal_constant(tp, decimal)) return &TYPE_ERROR;
    item_type->decimal = decimal;
    arraylist_append(tp->const_list, decimal);
    item_type->const_index = tp->const_list->length - 1;
    item_type->is_const = 1;
    item_type->is_literal = 1;
    return (Type*)item_type;
}

static Type* build_lit_named_value_from_span(Transpiler* tp,
        SourceSpan span) {
    StrView text = source_span_text(tp, span);
    if (strview_equal(&text, "true") || strview_equal(&text, "false")) return &LIT_BOOL;
    if (strview_equal(&text, "decimal.inf") || strview_equal(&text, "decimal.nan")) {
        return build_lit_decimal_poison_from_span(tp, span);
    }
    return build_lit_float_from_span(tp, span);
}



static Type* build_lit_imaginary_from_span(Transpiler* tp,
        SourceSpan span) {
    StrView source = source_span_text(tp, span);
    if (source.length < 2 || source.str[source.length - 1] != 'j') return &TYPE_ERROR;
    char* coefficient = (char*)mem_alloc(source.length, MEM_CAT_AST);
    if (!coefficient) return &TYPE_ERROR;
    memcpy(coefficient, source.str, source.length - 1);
    coefficient[source.length - 1] = '\0';

    double imag = 0.0;
    if (strcmp(coefficient, "inf") == 0) imag = INFINITY;
    else if (strcmp(coefficient, "nan") == 0) imag = NAN;
    else imag = str_to_double_default(coefficient, source.length - 1, 0.0);
    mem_free(coefficient);

    TypeComplex* item_type = (TypeComplex*)alloc_type(tp->pool, LMD_TYPE_COMPLEX, sizeof(TypeComplex));
    item_type->real = 0.0;
    item_type->imag = imag;
    item_type->is_const = 1;
    item_type->is_literal = 1;
    return (Type*)item_type;
}



static Type* build_lit_decimal_from_span(Transpiler* tp, SourceSpan span) {
    TypeDecimal* item_type = (TypeDecimal*)alloc_type(tp->pool, LMD_TYPE_DECIMAL, sizeof(TypeDecimal));
    StrView num_sv = source_span_text(tp, span);
    char* num_str = ast_copy_source_text(tp, num_sv, span);
    if (!num_str) return &TYPE_ERROR;
    char suffix_char = num_sv.str[num_sv.length - 1];
    if (suffix_char == 'N') {
        record_semantic_error_span(tp, span, ERR_INVALID_NUMBER,
            "decimal literal suffix 'N' has been retired; use 'm' for decimal or 'n' for integer");
        mem_free(num_str);
        return &TYPE_ERROR;
    }
    if (suffix_char == 'n' || suffix_char == 'm') {
        num_str[num_sv.length - 1] = '\0';  // clear the suffix
    }
    log_debug("build lit decimal: %s", num_str);

    // A.5 suffix split (Lambda_Semantics_Number_Model.md): the suffix alone
    // names the type — 'n' is integer always, 'm' is decimal always. The old
    // lexical test survives only as the validity guard on 'n': a fractional
    // or negative-exponent spelling cannot be an integer.
    bool is_integer_literal = (suffix_char == 'n');
    if (is_integer_literal && !n_literal_is_integer(num_str)) {
        record_semantic_error_span(tp, span, ERR_INVALID_NUMBER,
            "'n' literal must be integer-valued; use the 'm' suffix for decimal (e.g. 1.5m)");
        mem_free(num_str);
        return &TYPE_ERROR;
    }

    // Allocate heap-allocated Decimal structure
    Decimal* decimal;
    decimal = (Decimal*)pool_alloc(tp->pool, sizeof(Decimal));
    item_type->decimal = decimal;

    bool needs_unlimited_decimal =
        decimal_literal_significant_digits(num_str) > DECIMAL_FIXED_PRECISION;
    decimal->unlimited = is_integer_literal ? DECIMAL_BIGINT :
        (needs_unlimited_decimal ? 1 : 0);

    // literal digits are preserved exactly by selecting the necessary tier.
    // parse the literal without a precision context; the selected tier must
    // not round away source digits before runtime evaluation sees them.
    decimal->dec_val = decimal_parse_str_exact(num_str);
    if (!decimal->dec_val) {
        log_error("Error: Failed to parse decimal: %s", num_str);
        mem_free(num_str);
        return &TYPE_ERROR;
    }

    if (!track_decimal_constant(tp, decimal)) {
        mem_free(num_str);
        return &TYPE_ERROR;
    }

    // Add to const list
    arraylist_append(tp->const_list, item_type->decimal);
    item_type->const_index = tp->const_list->length - 1;
    item_type->is_const = 1;  item_type->is_literal = 1;
    mem_free(num_str);
    return (Type*)item_type;
}



// Parse a sized integer suffix and return the NumSizedType and suffix length
// Returns -1 if no valid suffix found
static int parse_sized_int_suffix(const char* str, int len, NumSizedType* out_num_type) {
    // check from end of string for i8/i16/i32/i64/u8/u16/u32/u64
    if (len >= 2) {
        char c1 = str[len - 2], c2 = str[len - 1];
        if (c1 == 'i' && c2 == '8') { *out_num_type = NUM_INT8; return 2; }
        if (c1 == 'u' && c2 == '8') { *out_num_type = NUM_UINT8; return 2; }
    }
    if (len >= 3) {
        char c1 = str[len - 3], c2 = str[len - 2], c3 = str[len - 1];
        if (c1 == 'i' && c2 == '1' && c3 == '6') { *out_num_type = NUM_INT16; return 3; }
        if (c1 == 'i' && c2 == '3' && c3 == '2') { *out_num_type = NUM_INT32; return 3; }
        if (c1 == 'i' && c2 == '6' && c3 == '4') { *out_num_type = (NumSizedType)0xFF; return 3; } // i64 = existing INT64
        if (c1 == 'u' && c2 == '1' && c3 == '6') { *out_num_type = NUM_UINT16; return 3; }
        if (c1 == 'u' && c2 == '3' && c3 == '2') { *out_num_type = NUM_UINT32; return 3; }
        if (c1 == 'u' && c2 == '6' && c3 == '4') { *out_num_type = (NumSizedType)0xFE; return 3; } // u64 = UINT64
    }
    return -1;
}

static int decimal_literal_significant_digits(const char* str) {
    bool seen_nonzero = false;
    bool saw_digit = false;
    int digits = 0;
    for (const char* p = str; *p; p++) {
        char ch = *p;
        if (ch == 'e' || ch == 'E') break;
        if (ch < '0' || ch > '9') continue;
        saw_digit = true;
        if (ch != '0') seen_nonzero = true;
        if (seen_nonzero) digits++;
    }
    return saw_digit ? (digits > 0 ? digits : 1) : 0;
}

static bool n_literal_is_integer(const char* str) {
    bool has_dot = false;
    bool has_negative_exponent = false;
    for (const char* p = str; *p; p++) {
        if (*p == '.') {
            has_dot = true;
        } else if (*p == 'e' || *p == 'E') {
            const char* exp = p + 1;
            if (*exp == '+' || *exp == '-') {
                has_negative_exponent = (*exp == '-');
            }
            break;
        }
    }
    return !has_dot && !has_negative_exponent;
}

static bool sized_literal_is_decimal(const char* str) {
    const char* p = str;
    if (*p == '+' || *p == '-') p++;
    return !(p[0] == '0' && (p[1] == 'x' || p[1] == 'X' ||
                             p[1] == 'o' || p[1] == 'O' ||
                             p[1] == 'b' || p[1] == 'B'));
}

static uint64_t sized_literal_limit(NumSizedType num_type, bool decimal_literal) {
    switch (num_type) {
        case NUM_INT8:   return decimal_literal ? 128ULL : 0xFFULL;
        case NUM_INT16:  return decimal_literal ? 32768ULL : 0xFFFFULL;
        case NUM_INT32:  return decimal_literal ? 2147483648ULL : 0xFFFFFFFFULL;
        case NUM_UINT8:  return 0xFFULL;
        case NUM_UINT16: return 0xFFFFULL;
        case NUM_UINT32: return 0xFFFFFFFFULL;
        default:         return UINT64_MAX;
    }
}

// Build AST type for sized integer literal (e.g., 42i8, 255u16, 100i64)
static Type* build_lit_sized_integer_from_span(Transpiler* tp,
        SourceSpan span) {
    StrView source = source_span_text(tp, span);
    char* num_str = ast_copy_source_text(tp, source, span);
    if (!num_str) return &TYPE_ERROR;

    NumSizedType num_type;
    int suffix_len = parse_sized_int_suffix(num_str, source.length, &num_type);
    if (suffix_len < 0) {
        log_error("Invalid sized integer suffix: %s", num_str);
        mem_free(num_str);
        return &TYPE_ERROR;
    }
    num_str[source.length - suffix_len] = '\0';  // strip suffix

    char* endptr;
    errno = 0;
    uint64_t raw_value = strtoull(num_str, &endptr, 0);
    if (errno == ERANGE || endptr == num_str || *endptr != '\0') {
        record_semantic_error_span(tp, span, ERR_INVALID_NUMBER,
            "invalid sized integer literal '%s'", num_str);
        mem_free(num_str);
        return &TYPE_ERROR;
    }
    bool decimal_literal = sized_literal_is_decimal(num_str);
    uint64_t limit = sized_literal_limit(num_type, decimal_literal);
    if (raw_value > limit) {
        // sized constants are checked before packing so invalid source cannot silently truncate.
        record_semantic_error_span(tp, span, ERR_INVALID_NUMBER,
            "sized integer literal '%s' overflows %s", num_str, get_num_sized_type_name(num_type));
        mem_free(num_str);
        return &TYPE_ERROR;
    }
    // sized integer literals are fixed-width, so non-decimal input may still
    // denote raw bits, but decimal input is range-checked before packing.
    int64_t value;
    __builtin_memcpy(&value, &raw_value, sizeof(value));
    mem_free(num_str);

    // i64 suffix → use existing INT64 type
    if (num_type == 0xFF) {
        TypeInt64* item_type = (TypeInt64*)alloc_type(tp->pool, LMD_TYPE_INT64, sizeof(TypeInt64));
        item_type->int64_val = value;
        int64_t* heap_val = (int64_t*)pool_alloc(tp->pool, sizeof(int64_t));
        *heap_val = value;
        arraylist_append(tp->const_list, heap_val);
        item_type->const_index = tp->const_list->length - 1;
        item_type->is_const = 1;  item_type->is_literal = 1;
        return (Type*)item_type;
    }

    // u64 suffix → use UINT64 type
    if (num_type == 0xFE) {
        TypeUint64* item_type = (TypeUint64*)alloc_type(tp->pool, LMD_TYPE_UINT64, sizeof(TypeUint64));
        item_type->uint64_val = raw_value;
        uint64_t* heap_val = (uint64_t*)pool_alloc(tp->pool, sizeof(uint64_t));
        *heap_val = raw_value;
        arraylist_append(tp->const_list, heap_val);
        item_type->const_index = tp->const_list->length - 1;
        item_type->is_const = 1;  item_type->is_literal = 1;
        return (Type*)item_type;
    }

    // sized numeric: pack inline
    TypeNumSized* item_type = (TypeNumSized*)alloc_type(tp->pool, LMD_TYPE_NUM_SIZED, sizeof(TypeNumSized));
    item_type->num_type = num_type;
    // store raw 32-bit value based on sub-type
    switch (num_type) {
        case NUM_INT8:   item_type->raw_bits = (uint32_t)(uint8_t)raw_value; break;
        case NUM_INT16:  item_type->raw_bits = (uint32_t)(uint16_t)raw_value; break;
        case NUM_INT32:  item_type->raw_bits = (uint32_t)(uint32_t)raw_value; break;
        case NUM_UINT8:  item_type->raw_bits = (uint32_t)(uint8_t)raw_value; break;
        case NUM_UINT16: item_type->raw_bits = (uint32_t)(uint16_t)raw_value; break;
        case NUM_UINT32: item_type->raw_bits = (uint32_t)raw_value; break;
        default: item_type->raw_bits = 0; break;
    }
    item_type->is_const = 1;  item_type->is_literal = 1;
    log_debug("build_lit_sized_integer: type=%s value=%lld raw_bits=%u",
              get_num_sized_type_name(num_type), value, item_type->raw_bits);
    return (Type*)item_type;
}



// Build AST type for sized float literal (e.g., 3.14f32, 0.5f16)
static Type* build_lit_sized_float_from_span(Transpiler* tp,
        SourceSpan span) {
    StrView source = source_span_text(tp, span);
    char* num_str = ast_copy_source_text(tp, source, span);
    if (!num_str) return &TYPE_ERROR;

    // detect suffix: f16, f32, f64
    NumSizedType num_type = NUM_FLOAT32;  // default
    int suffix_len = 3;

    if (source.length >= 3) {
        const char* suffix = num_str + source.length - 3;
        if (suffix[0] == 'f' && suffix[1] == '1' && suffix[2] == '6') {
            num_type = NUM_FLOAT16;
        } else if (suffix[0] == 'f' && suffix[1] == '3' && suffix[2] == '2') {
            num_type = NUM_FLOAT32;
        } else if (suffix[0] == 'f' && suffix[1] == '6' && suffix[2] == '4') {
            num_str[source.length - 3] = '\0';
            double dval = strtod(num_str, NULL);
            mem_free(num_str);
            // f64 is an alias for Lambda float; producing a distinct tag makes type() observable.
            TypeFloat* item_type = (TypeFloat*)alloc_type(tp->pool, LMD_TYPE_FLOAT, sizeof(TypeFloat));
            item_type->double_val = dval;
            double* heap_val = (double*)pool_alloc(tp->pool, sizeof(double));
            *heap_val = dval;
            arraylist_append(tp->const_list, heap_val);
            item_type->const_index = tp->const_list->length - 1;
            item_type->is_const = 1;  item_type->is_literal = 1;
            return (Type*)item_type;
        }
    }

    num_str[source.length - suffix_len] = '\0';
    double dval = strtod(num_str, NULL);
    mem_free(num_str);

    TypeNumSized* item_type = (TypeNumSized*)alloc_type(tp->pool, LMD_TYPE_NUM_SIZED, sizeof(TypeNumSized));
    item_type->num_type = num_type;
    if (num_type == NUM_FLOAT32) {
        item_type->raw_bits = f32_to_bits((float)dval);
    } else {
        item_type->raw_bits = (uint32_t)f32_to_f16_bits((float)dval);
    }
    item_type->is_const = 1;  item_type->is_literal = 1;
    log_debug("build_lit_sized_float: type=%s dval=%g raw_bits=%u",
              get_num_sized_type_name(num_type), dval, item_type->raw_bits);
    return (Type*)item_type;
}



static Type* build_literal_type_from_span(Transpiler* tp,
        SourceSpan span, LambdaAstLiteralKind kind) {
    switch (kind) {
    case LAMBDA_AST_LITERAL_STRING:
    case LAMBDA_AST_LITERAL_SYMBOL:
    case LAMBDA_AST_LITERAL_BINARY:
        return build_lit_string_from_span(tp, span, kind);
    case LAMBDA_AST_LITERAL_DATETIME:
        return build_lit_datetime_from_span(tp, span);
    case LAMBDA_AST_LITERAL_NAMED_VALUE:
        return build_lit_named_value_from_span(tp, span);
    case LAMBDA_AST_LITERAL_INTEGER: {
        StrView source = source_span_text(tp, span);
        char* number = ast_copy_source_text(tp, source, span);
        if (!number) return &TYPE_ERROR;
        int64_t value = 0;
        bool in_band = lambda_parse_int_literal(number, &value);
        mem_free(number);
        if (in_band) return &LIT_INT;
        record_semantic_error_span(tp, span, ERR_INVALID_NUMBER,
            "integer literal is outside compact int range; use an explicit suffix or decimal literal");
        return &TYPE_ERROR;
    }
    case LAMBDA_AST_LITERAL_FLOAT:
        return build_lit_float_from_span(tp, span);
    case LAMBDA_AST_LITERAL_DECIMAL:
        return build_lit_decimal_from_span(tp, span);
    case LAMBDA_AST_LITERAL_SIZED_INTEGER:
        return build_lit_sized_integer_from_span(tp, span);
    case LAMBDA_AST_LITERAL_SIZED_FLOAT:
        return build_lit_sized_float_from_span(tp, span);
    case LAMBDA_AST_LITERAL_IMAGINARY:
        return build_lit_imaginary_from_span(tp, span);
    }
    return &TYPE_ERROR;
}

AstNode* build_literal_from_span(Transpiler* tp, SourceSpan span,
        LambdaAstLiteralKind kind) {
    AstPrimaryNode* ast_node = (AstPrimaryNode*)alloc_ast_node_from_span(tp,
        AST_NODE_PRIMARY, span, sizeof(AstPrimaryNode));
    ast_node->type = build_literal_type_from_span(tp, span, kind);
    return (AstNode*)ast_node;
}

// unknown type-name diagnostic with a conceptual-alias suggestion. Names like
// `int64` appear in docs/prose as concept names but are NOT Lambda annotation
// syntax (the defined names are the sized forms, e.g. `i64`); without this,
// the annotation silently became TYPE_ERROR and surfaced later as a confusing
// E201 "cannot initialize ... of type error".
// returns the defined syntax for a conceptual alias, or NULL when the name is
// not a known concept spelling.
const char* base_type_alias_suggestion(StrView type_name) {
    static const struct { const char* concept_name; const char* syntax; } alias_map[] = {
        {"int64", "i64"}, {"uint64", "u64"},
        {"int8", "i8"}, {"int16", "i16"}, {"int32", "i32"},
        {"uint8", "u8"}, {"uint16", "u16"}, {"uint32", "u32"},
        {"float32", "f32"}, {"float64", "f64"}, {"double", "float"},
    };
    for (size_t i = 0; i < sizeof(alias_map) / sizeof(alias_map[0]); i++) {
        if (strview_equal(&type_name, alias_map[i].concept_name)) {
            return alias_map[i].syntax;
        }
    }
    return NULL;
}

void record_unknown_base_type_span(Transpiler* tp, SourceSpan span,
        StrView type_name) {
    const char* suggestion = base_type_alias_suggestion(type_name);
    if (suggestion) {
        record_semantic_error_span(tp, span, ERR_UNDEFINED_TYPE,
            "unknown type '%.*s'; did you mean '%s'?",
            (int)type_name.length, type_name.str, suggestion);
        return;
    }
    record_semantic_error_span(tp, span, ERR_UNDEFINED_TYPE,
        "unknown type '%.*s'", (int)type_name.length, type_name.str);
}



// helper: returns Type* for base_type node (used in primary_expr context)


AstNode* build_navigation_node_from_parts(Transpiler* tp, SourceSpan span,
        AstNode* object, bool root) {
    AstNavigationNode* nav = (AstNavigationNode*)alloc_ast_node_from_span(tp,
        AST_NODE_NAVIGATION_EXPR, span, sizeof(AstNavigationNode));
    nav->object = object;
    nav->root = root;
    nav->type = nav->object ? nav->object->type : &TYPE_ANY;
    return (AstNode*)nav;
}





// Build type negation expression: !T → any ! T (exclude type)
// Creates a TypeBinary(OPERATOR_EXCLUDE, any, T) so that `x is !string` works


bool lambda_unary_operator_from_spelling(StrView op, Operator* op_out) {
    if (!op_out) return false;
    if (strview_equal(&op, "not")) { *op_out = OPERATOR_NOT; }
    else if (strview_equal(&op, "-")) { *op_out = OPERATOR_NEG; }
    else if (strview_equal(&op, "+")) { *op_out = OPERATOR_POS; }
    else { return false; }
    return true;
}

bool lambda_binary_operator_from_spelling(StrView op, Operator* op_out) {
    if (!op_out) return false;
    if (strview_equal(&op, "and")) { *op_out = OPERATOR_AND; }
    else if (strview_equal(&op, "or")) { *op_out = OPERATOR_OR; }
    else if (strview_equal(&op, "+")) { *op_out = OPERATOR_ADD; }
    else if (strview_equal(&op, "++")) { *op_out = OPERATOR_JOIN; }
    else if (strview_equal(&op, "-")) { *op_out = OPERATOR_SUB; }
    else if (strview_equal(&op, "*")) { *op_out = OPERATOR_MUL; }
    else if (strview_equal(&op, "**")) { *op_out = OPERATOR_POW; }
    else if (strview_equal(&op, "/")) { *op_out = OPERATOR_DIV; }
    else if (strview_equal(&op, "div")) { *op_out = OPERATOR_IDIV; }
    else if (strview_equal(&op, "%")) { *op_out = OPERATOR_MOD; }
    else if (strview_equal(&op, "==")) { *op_out = OPERATOR_EQ; }
    else if (strview_equal(&op, "!=")) { *op_out = OPERATOR_NE; }
    else if (strview_equal(&op, "<")) { *op_out = OPERATOR_LT; }
    else if (strview_equal(&op, "<=")) { *op_out = OPERATOR_LE; }
    else if (strview_equal(&op, ">")) { *op_out = OPERATOR_GT; }
    else if (strview_equal(&op, ">=")) { *op_out = OPERATOR_GE; }
    else if (strview_equal(&op, "eq")) { *op_out = OPERATOR_ELEM_EQ; }
    else if (strview_equal(&op, "ne")) { *op_out = OPERATOR_ELEM_NE; }
    else if (strview_equal(&op, "lt")) { *op_out = OPERATOR_ELEM_LT; }
    else if (strview_equal(&op, "le")) { *op_out = OPERATOR_ELEM_LE; }
    else if (strview_equal(&op, "gt")) { *op_out = OPERATOR_ELEM_GT; }
    else if (strview_equal(&op, "ge")) { *op_out = OPERATOR_ELEM_GE; }
    else if (strview_equal(&op, "to")) { *op_out = OPERATOR_TO; }
    else if (strview_equal(&op, "|")) { *op_out = OPERATOR_UNION; }
    else if (strview_equal(&op, "|>")) { *op_out = OPERATOR_PIPE; }
    else if (strview_equal(&op, "where") || strview_equal(&op, "that")) {
        *op_out = OPERATOR_WHERE;
    }
    else if (strview_equal(&op, "&")) { *op_out = OPERATOR_INTERSECT; }
    else if (strview_equal(&op, "!")) { *op_out = OPERATOR_EXCLUDE; }
    else if (strview_equal(&op, "is")) { *op_out = OPERATOR_IS; }
    else if (strview_equal(&op, "in")) { *op_out = OPERATOR_IN; }
    else if (strview_equal(&op, "at")) { *op_out = OPERATOR_AT; }
    else { return false; }
    return true;
}

AstNode* build_unary_node_from_parts(Transpiler* tp, SourceSpan span,
        StrView op, AstNode* operand) {
    AstUnaryNode* ast_node = (AstUnaryNode*)alloc_ast_node_from_span(tp,
        AST_NODE_UNARY, span, sizeof(AstUnaryNode));
    ast_node->op_str = op;
    ast_node->operand = operand;
    if (!lambda_unary_operator_from_spelling(op, &ast_node->op)) {
        // `*` spread and `!` type negation have distinct retained node shapes;
        // ordinary unary construction must not silently classify either one.
        log_error("build unary from parts: unsupported operator %.*s", (int)op.length,
            op.str);
        ast_node->type = &TYPE_ERROR;
        return (AstNode*)ast_node;
    }
    if (!operand) {
        log_error("build unary from parts: missing operand");
        ast_node->type = &TYPE_ERROR;
        return (AstNode*)ast_node;
    }
    if (!operand->type) {
        log_error("build unary from parts: operand missing type information");
        ast_node->type = &TYPE_ERROR;
        return (AstNode*)ast_node;
    }

    TypeId operand_type = operand->type->type_id;
    TypeId type_id;
    if (ast_node->op == OPERATOR_NOT) {
        type_id = LMD_TYPE_BOOL;
    }
    else if (operand_type == LMD_TYPE_NUM_SIZED || operand_type == LMD_TYPE_UINT64) {
        ast_node->type = operand->type;
        return (AstNode*)ast_node;
    }
    else if (IS_NUMERIC_ID(operand_type)) {
        type_id = operand_type;
    }
    else if (operand_type != LMD_TYPE_ANY) {
        // Keep the shared constructor's result honest: a known non-number
        // cannot become `any` merely because it crosses a parser boundary.
        ast_node->type = lambda_type_union_normalized(tp->pool, &TYPE_NUMBER,
            &TYPE_ERROR);
        return (AstNode*)ast_node;
    }
    else {
        type_id = census_any_type_id(tp, ANY_UNARY);
    }
    ast_node->type = alloc_type(tp->pool, type_id, sizeof(Type));
    return (AstNode*)ast_node;
}



// build spread expression: *expr


// Helper: check if operator is a relational comparison (<, <=, >, >=)
static inline bool is_relational_op(Operator op) {
    return op == OPERATOR_LT || op == OPERATOR_LE || op == OPERATOR_GT || op == OPERATOR_GE;
}

static inline bool is_elementwise_comparison_op(Operator op) {
    return op >= OPERATOR_ELEM_EQ && op <= OPERATOR_ELEM_GE;
}

static bool is_direct_elementwise_comparison(AstNode* node) {
    node = unwrap_primary_node(node);
    return node && node->node_type == AST_NODE_BINARY &&
        is_elementwise_comparison_op(((AstBinaryNode*)node)->op);
}

static void lint_condition_at_line(Transpiler* tp, int line, AstNode* cond,
        const char* context) {
    if (!cond) return;

    if (is_direct_elementwise_comparison(cond)) {
        // masks are containers and therefore truthy; condition sites need an explicit scalar reduction.
        log_warn("lambda_condition_lint: line %d: elementwise comparison used as %s condition; use any(...), all(...), sum(mask), or a[mask] explicitly",
            line, context);
        return;
    }

    TypeId cond_type = cond->type ? cond->type->type_id : LMD_TYPE_ANY;
    if (is_container_type_id(cond_type)) {
        // containers are truthy by design, so a container condition is almost always a missing scalar predicate.
        log_warn("lambda_condition_lint: line %d: %s condition has container type %s, which is always truthy; use len(...), any(...), all(...), or an explicit comparison",
            line, context, get_type_name(cond_type));
    }
}



static void lint_condition_span(Transpiler* tp, SourceSpan span,
        AstNode* cond, const char* context) {
    lint_condition_at_line(tp,
        (int)lambda_source_span_start_point(tp->source, span).row + 1,
        cond, context);
}

static bool is_magnitude_numeric_type(TypeId type_id) {
    return type_id != LMD_TYPE_COMPLEX && is_numeric_type_id(type_id);
}

static bool known_magnitude_comparable(TypeId left_type, TypeId right_type) {
    if (left_type == LMD_TYPE_ANY || right_type == LMD_TYPE_ANY) return true;
    if (left_type == LMD_TYPE_NULL || right_type == LMD_TYPE_NULL) return true;
    if (is_magnitude_numeric_type(left_type) && is_magnitude_numeric_type(right_type)) return true;
    if (left_type == LMD_TYPE_STRING && right_type == LMD_TYPE_STRING) return true;
    if (left_type == LMD_TYPE_DTIME && right_type == LMD_TYPE_DTIME) return true;
    return false;
}

static bool known_magnitude_comparable_type_set(Type* left, Type* right) {
    if (!left || !right) return false;
    if (left == &TYPE_NUMBER || left == &TYPE_INTEGER) {
        return right == &TYPE_NUMBER || right == &TYPE_INTEGER ||
            right->type_id == LMD_TYPE_ANY || right->type_id == LMD_TYPE_NULL ||
            is_magnitude_numeric_type(right->type_id);
    }
    if (right == &TYPE_NUMBER || right == &TYPE_INTEGER) {
        return left->type_id == LMD_TYPE_ANY || left->type_id == LMD_TYPE_NULL ||
            is_magnitude_numeric_type(left->type_id);
    }
    return known_magnitude_comparable(left->type_id, right->type_id);
}



static Type* known_array_element_type(Type* type) {
    if (!type) return NULL;
    if (type->type_id == LMD_TYPE_ARRAY || type->type_id == LMD_TYPE_ARRAY_NUM) {
        return ((TypeArray*)type)->nested;
    }
    return type;
}

static Type* binary_array_result_element_type(AstBinaryNode* ast_node) {
    if (!ast_node) return &TYPE_ANY;
    if (is_elementwise_comparison_op(ast_node->op)) return &TYPE_BOOL;

    LambdaNumericOpFamily family;
    switch (ast_node->op) {
    case OPERATOR_ADD: family = LAMBDA_NUM_OP_ADD; break;
    case OPERATOR_SUB: family = LAMBDA_NUM_OP_SUB; break;
    case OPERATOR_MUL: family = LAMBDA_NUM_OP_MUL; break;
    case OPERATOR_DIV: family = LAMBDA_NUM_OP_TRUE_DIV; break;
    case OPERATOR_IDIV: family = LAMBDA_NUM_OP_IDIV; break;
    case OPERATOR_MOD: family = LAMBDA_NUM_OP_MOD; break;
    default: return &TYPE_ANY;
    }

    Type* left = known_array_element_type(ast_node->left ? ast_node->left->type : NULL);
    Type* right = known_array_element_type(ast_node->right ? ast_node->right->type : NULL);
    LambdaNumericDecision decision = lambda_numeric_classify(family,
        lambda_numeric_kind_from_type(left), lambda_numeric_kind_from_type(right));
    return decision.valid ? lambda_numeric_type_from_kind(decision.result) : &TYPE_ANY;
}

static Type* alloc_array_num_result_type(Transpiler* tp, AstBinaryNode* ast_node) {
    TypeArray* type = (TypeArray*)alloc_type(tp->pool, LMD_TYPE_ARRAY_NUM,
        sizeof(TypeArray));
    // ArrayNum carries its runtime element lane; preserving that witness keeps
    // later indexing native and prevents a typed result from being treated as
    // a bare Type allocation (the exact allocator exposed the stale layout).
    type->nested = binary_array_result_element_type(ast_node);
    type->type_index = -1;
    return (Type*)type;
}





static bool ast_is_explicit_type_value(AstNode* node) {
    node = boundary_unwrap_primary(node);
    if (!node) return false;
    switch (node->node_type) {
    case AST_NODE_TYPE:
    case AST_NODE_CONTENT_TYPE:
    case AST_NODE_LIST_TYPE:
    case AST_NODE_ARRAY_TYPE:
    case AST_NODE_MAP_TYPE:
    case AST_NODE_ELMT_TYPE:
    case AST_NODE_FUNC_TYPE:
    case AST_NODE_BINARY_TYPE:
    case AST_NODE_UNARY_TYPE:
    case AST_NODE_CONSTRAINED_TYPE:
    case AST_NODE_OBJECT_TYPE:
    case AST_NODE_STRING_PATTERN:
    case AST_NODE_SYMBOL_PATTERN:
        return true;
    case AST_NODE_IDENT: {
        AstIdentNode* ident = (AstIdentNode*)node;
        AstNode* declaration = ident->entry ? ident->entry->node : NULL;
        return declaration && (declaration->node_type == AST_NODE_TYPE_STAM ||
            (declaration->node_type == AST_NODE_ASSIGN &&
                ((AstNamedNode*)declaration)->is_type_definition) ||
            declaration->node_type == AST_NODE_STRING_PATTERN ||
            declaration->node_type == AST_NODE_SYMBOL_PATTERN);
    }
    default:
        return false;
    }
}

// `|`, `&` and `!` are all type-set operators when both operands are types.
static bool ast_binary_is_type_set_op(Operator op) {
    return op == OPERATOR_UNION || op == OPERATOR_INTERSECT ||
        op == OPERATOR_EXCLUDE;
}

static bool promote_type_union_expr(Transpiler* tp, AstBinaryNode* ast_node) {
    if (!ast_node || !ast_binary_is_type_set_op(ast_node->op) ||
        !ast_is_explicit_type_value(ast_node->left) ||
        !ast_is_explicit_type_value(ast_node->right)) {
        return false;
    }

    // Phase 6 makes `|` union in expression position too; type-valued operands
    // must build a first-class binary type instead of falling into runtime ops.
    // `&`/`!` reach here on the same footing: without the promotion an
    // annotation received a LMD_TYPE_TYPE-tagged *value* rather than a type,
    // which surfaced as "cannot initialize 'a' of type type with int" (LR02-9).
    ast_node->node_type = AST_NODE_BINARY_TYPE;
    TypeType* node_type = (TypeType*)alloc_type(tp->pool, LMD_TYPE_TYPE, sizeof(TypeType));
    TypeBinary* type = (TypeBinary*)alloc_type_kind(tp->pool, TYPE_KIND_BINARY, sizeof(TypeBinary));
    node_type->type = (Type*)type;
    type->left = ((TypeType*)ast_node->left->type)->type;
    type->right = ((TypeType*)ast_node->right->type)->type;
    type->op = ast_node->op;
    ast_node->type = (Type*)node_type;
    arraylist_append(tp->type_list, ast_node->type);
    type->type_index = tp->type_list->length - 1;
    return true;
}





// check if expression contains ~ or ~# references (pipe context references)
bool has_current_item_ref(AstNode* node) {
    if (!node) return false;

    switch (node->node_type) {
    case AST_NODE_CURRENT_ITEM:
    case AST_NODE_CURRENT_INDEX:
        return true;
    case AST_NODE_HANDLER_EXPR:
    case AST_NODE_HANDLER_STAM: {
        AstHandlerNode* handler = (AstHandlerNode*)node;
        // The value arm binds its own `~`; only the operand and error arm can
        // consume a current-item context supplied by an enclosing pipe.
        return has_current_item_ref(handler->operand) ||
            has_current_item_ref(handler->body);
    }
    case AST_NODE_PRIMARY:
        return has_current_item_ref(((AstPrimaryNode*)node)->expr);
    case AST_NODE_UNARY:
    case AST_NODE_SPREAD:
        return has_current_item_ref(((AstUnaryNode*)node)->operand);
    case AST_NODE_BINARY:
    case AST_NODE_PIPE:
        return has_current_item_ref(((AstBinaryNode*)node)->left) ||
               has_current_item_ref(((AstBinaryNode*)node)->right);
    case AST_NODE_IF_EXPR: {
        AstIfNode* if_node = (AstIfNode*)node;
        return has_current_item_ref(if_node->cond) ||
               has_current_item_ref(if_node->then) ||
               has_current_item_ref(if_node->otherwise);
    }
    case AST_NODE_MATCH_EXPR: {
        AstMatchNode* match_node = (AstMatchNode*)node;
        if (has_current_item_ref(match_node->scrutinee)) return true;
        // The arm BODY can consume a current item supplied by an enclosing
        // pipe; the pattern cannot — `case int that (~ > 0)` rebinds `~` to the
        // match subject, the same shadowing the handler case above models. This
        // loop used to inspect nothing and fall through to `false`, so
        // `xs |> match (1) { case int: (~) * 10 }` evaluated to `error`: the
        // pipe never bound `~` because nothing reported the arm needed it.
        AstMatchArm* arm = match_node->first_arm;
        while (arm) {
            if (has_current_item_ref(arm->body)) return true;
            arm = (AstMatchArm*)arm->next;
        }
        return false;
    }
    case AST_NODE_CALL_EXPR: {
        AstCallNode* call = (AstCallNode*)node;
        if (has_current_item_ref(call->function)) return true;
        AstNode* arg = call->argument;
        while (arg) {
            if (has_current_item_ref(arg)) return true;
            arg = arg->next;
        }
        return false;
    }
    case AST_NODE_MEMBER_EXPR:
    case AST_NODE_INDEX_EXPR:
        return has_current_item_ref(((AstFieldNode*)node)->object) ||
               has_current_item_ref(((AstFieldNode*)node)->field);
    case AST_NODE_NAVIGATION_EXPR:
        return has_current_item_ref(((AstNavigationNode*)node)->object);
    case AST_NODE_ARRAY: {
        AstNode* item = ((AstArrayNode*)node)->item;
        while (item) {
            if (has_current_item_ref(item)) return true;
            item = item->next;
        }
        return false;
    }
    case AST_NODE_MAP: {
        AstNode* item = ((AstMapNode*)node)->item;
        while (item) {
            if (has_current_item_ref(item)) return true;
            item = item->next;
        }
        return false;
    }
    case AST_NODE_KEY_EXPR:
    case AST_NODE_ASSIGN:
        return has_current_item_ref(((AstNamedNode*)node)->as);
    default:
        return false;
    }
}

AstNode* build_current_item_from_span(Transpiler* tp, SourceSpan span,
        bool is_index) {
    if (is_index) {
        log_debug("build current index (~#)");
        AstNode* ast_node = alloc_ast_node_from_span(tp, AST_NODE_CURRENT_INDEX,
            span, sizeof(AstNode));
        ast_node->type = alloc_type(tp->pool, LMD_TYPE_ANY, sizeof(Type));
        return ast_node;
    } else {
        log_debug("build current item (~)");
        AstNode* ast_node = alloc_ast_node_from_span(tp, AST_NODE_CURRENT_ITEM,
            span, sizeof(AstNode));
        ast_node->type = alloc_type(tp->pool, LMD_TYPE_ANY, sizeof(Type));
        return ast_node;
    }
}

AstNode* build_current_parent_navigation_from_span(Transpiler* tp,
        SourceSpan span) {
    AstNavigationNode* nav = (AstNavigationNode*)alloc_ast_node_from_span(tp,
        AST_NODE_NAVIGATION_EXPR, span, sizeof(AstNavigationNode));
    nav->object = build_current_item_from_span(tp, span, false);
    nav->root = false;
    nav->type = nav->object->type;
    return (AstNode*)nav;
}

AstNode* build_primary_wrapper_from_parts(Transpiler* tp, SourceSpan span,
        AstNode* expr) {
    AstPrimaryNode* primary = (AstPrimaryNode*)alloc_ast_node_from_span(tp,
        AST_NODE_PRIMARY, span, sizeof(AstPrimaryNode));
    primary->expr = expr;
    primary->type = expr && expr->type ? expr->type : &TYPE_ERROR;
    return (AstNode*)primary;
}

// build current item reference (~)


// Build the handler-local current error reference (`^`).  The grammar admits
// the token as a primary so ordinary member/index builders can compose with it;
// semantic scope is enforced here rather than letting `^` become a global value.
AstNode* build_current_error_from_span(Transpiler* tp, SourceSpan span) {
    AstNode* ast_node = alloc_ast_node_from_span(tp, AST_NODE_CURRENT_ERROR,
        span, sizeof(AstNode));
    ast_node->type = &TYPE_ERROR;
    if (!tp->building_handler_body) {
        // A direct parser cannot rely on a parser ancestor to enforce this scope;
        // the committed constructor owns the handler-body invariant for both paths.
        record_semantic_error_span(tp, span, ERR_INVALID_EXPR_CONTEXT,
            "current error `^` is only valid inside an error-handler body");
    }
    log_debug("build current handler error (^)");
    return ast_node;
}





// Does this branch leave the expression rather than produce a value? Only
// `raise` qualifies today. A block diverges when its LAST item does, which is
// the only position whose value the block would yield.
static bool ast_branch_diverges(AstNode* node) {
    while (node) {
        switch (node->node_type) {
        case AST_NODE_RAISE_STAM:
        case AST_NODE_RAISE_EXPR:
            return true;
        case AST_NODE_PRIMARY: {
            AstNode* inner = ((AstPrimaryNode*)node)->expr;
            if (!inner) return false;
            node = inner;
            continue;
        }
        case AST_NODE_LIST:
        case AST_NODE_CONTENT: {
            AstNode* last = NULL;
            for (AstNode* it = ((AstListNode*)node)->item; it; it = it->next) last = it;
            if (!last) return false;
            node = last;
            continue;
        }
        default:
            return false;
        }
    }
    return false;
}

static Type* infer_if_result_type(Transpiler* tp, AstNode* then_branch,
        AstNode* else_branch) {
    // Each arm contributes a type to the join. A `raise` never yields a
    // value, so it contributes error rather than its raised payload.
    Type* then_contrib = ast_branch_diverges(then_branch)
        ? &TYPE_ERROR : then_branch->type;
    Type* else_contrib = !else_branch ? &TYPE_NULL
        : ast_branch_diverges(else_branch)
            ? &TYPE_ERROR : else_branch->type;
    TypeId then_type_id = then_contrib->type_id;
    TypeId else_type_id = else_contrib->type_id;

    LambdaNumericDecision numeric_join = lambda_numeric_classify(LAMBDA_NUM_OP_ADD,
        lambda_numeric_kind_from_type(then_contrib),
        lambda_numeric_kind_from_type(else_branch ? else_contrib : NULL));
    if (numeric_join.valid) {
        return lambda_numeric_type_from_kind(numeric_join.result);
    }
    if (then_type_id != else_type_id) {
        // Plain mixed joins remain open until recursive return inference and
        // boxed-carrier handling are resolved together. A written `raise` is
        // the exception: preserve its real error constituent.
        bool has_divergence = then_contrib == &TYPE_ERROR ||
            else_contrib == &TYPE_ERROR;
        if (!has_divergence || then_type_id == LMD_TYPE_ANY ||
                else_type_id == LMD_TYPE_ANY) {
            return set_type_any(tp, ANY_JOIN);
        }
        TypeBinary* join = (TypeBinary*)alloc_type_kind(tp->pool,
            TYPE_KIND_BINARY, sizeof(TypeBinary));
        join->left = then_contrib;
        join->right = else_contrib;
        join->op = OPERATOR_UNION;
        return (Type*)join;
    }
    // Reuse container types to retain their complete shape metadata.
    return then_contrib;
}

// Unified build_if_expr: handles both expression and block forms
// When a branch is a content block, creates a new scope for variable shadowing


// build a match expression from committed reduction parts


// S16.6.8/S16.6.9 for `match`. A `:` arm is a value arm and may not hold a
// procedural block; a braced arm is a control arm when its interior is
// procedural. The form must then be all-value or all-control — a mixture would
// make the match's value depend on which arm ran, which is exactly what
// `case 'click' { ... } default: null` silently did before.
static void validate_match_branch_homogeneity(Transpiler* tp, AstMatchNode* node,
        SourceSpan span) {
    bool saw_value = false, saw_control = false;
    SourceSpan control_span = span;
    for (AstMatchArm* arm = node->first_arm; arm; arm = (AstMatchArm*)arm->next) {
        AstBranchKind kind = ast_branch_kind(arm->body);
        bool procedural = arm->body && arm->body->node_type == AST_NODE_CONTENT &&
            kind == AST_BRANCH_CONTROL;
        if (procedural && !arm->body_braced) {
            record_semantic_error_span(tp, arm->source_span, ERR_INVALID_OPERATION,
                "a `case T:` arm is a value arm and takes an expression; this "
                "block contains statements - write `case T { ... }`");
            return;
        }
        if (procedural) { saw_control = true; control_span = arm->source_span; }
        else if (kind != AST_BRANCH_NEUTRAL) saw_value = true;
    }
    if (saw_value && saw_control) {
        record_semantic_error_span(tp, control_span, ERR_INVALID_OPERATION,
            "match arms must be all value arms or all control arms; this arm is "
            "a statement block while another arm yields a value");
    }
}

AstNode* build_match_from_parts(Transpiler* tp, SourceSpan span,
        AstNode* scrutinee, AstNode* arms) {
    AstMatchNode* node = (AstMatchNode*)alloc_ast_node_from_span(tp,
        AST_NODE_MATCH_EXPR, span, sizeof(AstMatchNode));
    node->scrutinee = scrutinee;
    node->first_arm = (AstMatchArm*)arms;
    node->arm_count = 0;
    Type* result = NULL;
    bool mixed = false;
    for (AstMatchArm* arm = node->first_arm; arm;
            arm = (AstMatchArm*)arm->next) {
        node->arm_count++;
        Type* arm_type = arm->body && arm->body->type ? arm->body->type : &TYPE_ANY;
        if (!result) result = arm_type;
        else if (result->type_id != arm_type->type_id) mixed = true;
    }
    node->type = mixed ? set_type_any(tp, ANY_JOIN) :
        (result ? result : &TYPE_NULL);
    validate_match_branch_homogeneity(tp, node, span);
    AstNode* value = boundary_unwrap_primary(scrutinee);
    if (value && value->node_type == AST_NODE_IDENT) {
        AstIdentNode* ident = (AstIdentNode*)value;
        AstNode* binding = ident->entry ? ident->entry->node : NULL;
        TypeParam* parameter = binding && binding->node_type == AST_NODE_PARAM &&
                binding->type && binding->type->kind == TYPE_KIND_PARAM
            ? (TypeParam*)binding->type : NULL;
        if (parameter && !parameter->has_explicit_contract && parameter->contract_type &&
                !lambda_type_accepts_error(parameter->contract_type) &&
                match_has_error_handler(node)) {
            LambdaSourcePoint point = lambda_source_span_start_point(tp->source, span);
            // Implicit parameters short-circuit errors before a match body,
            // so a `case error` arm is unreachable without an explicit any.
            log_warn("lambda_match_lint: line %u: `case error:` is unreachable for implicit parameter '%.*s'; declare '%.*s: any' to accept error values",
                point.row + 1, ident->name ? (int)ident->name->len : 9,
                ident->name ? ident->name->chars : "parameter",
                ident->name ? (int)ident->name->len : 9,
                ident->name ? ident->name->chars : "parameter");
        }
    }
    return (AstNode*)node;
}



AstNode* build_decompose_from_parts(Transpiler* tp, SourceSpan span,
        String** names, int name_count, AstNode* value, bool is_named) {
    if (!names || name_count <= 0) return NULL;
    AstDecomposeNode* ast_node = (AstDecomposeNode*)alloc_ast_node_from_span(
        tp, AST_NODE_DECOMPOSE, span, sizeof(AstDecomposeNode));
    ast_node->name_count = name_count;
    ast_node->is_named = is_named;
    ast_node->names = (String**)pool_calloc(tp->pool, sizeof(String*) * name_count);
    ast_node->entries = (NameEntry**)pool_calloc(tp->pool,
        sizeof(NameEntry*) * name_count);
    for (int i = 0; i < name_count; i++) ast_node->names[i] = names[i];
    ast_node->as = value;
    ast_node->type = set_type_any(tp, ANY_DECOMPOSE);

    // Project the source's shape onto each target where it is knowable [TIG15].
    // Positional decomposition of a homogeneous array binds the element type;
    // a named decomposition (`let a, b at m`) reads the map's shape by name.
    // Everything else stays open — a wrong projection here would be a binding
    // whose declared type is a lie (SI14), so only proven shapes are used.
    Type* source_type = ast_node->as ? ast_node->as->type : NULL;
    TypeMap* source_map = source_type && !is_global_simple_type(source_type) &&
        (source_type->type_id == LMD_TYPE_MAP || source_type->type_id == LMD_TYPE_OBJECT)
        ? (TypeMap*)source_type : NULL;
    Type* source_elem = NULL;
    if (source_type && !is_global_simple_type(source_type) &&
            (source_type->type_id == LMD_TYPE_ARRAY ||
             source_type->type_id == LMD_TYPE_ARRAY_NUM)) {
        Type* nested = ((TypeArray*)source_type)->nested;
        // Container elements are boxed pointers on every path, so publishing
        // them cannot outrun the emitter the way a numeric lane would (TIG1).
        if (nested && (nested->type_id == LMD_TYPE_MAP ||
                nested->type_id == LMD_TYPE_ELEMENT ||
                nested->type_id == LMD_TYPE_OBJECT)) {
            source_elem = nested;
        }
    }

    // Push all names to the name stack
    for (int i = 0; i < name_count; i++) {
        // Create a temporary named node for each variable
        AstNamedNode* var_node = (AstNamedNode*)alloc_ast_node_from_span(tp,
            AST_NODE_ASSIGN, span, sizeof(AstNamedNode));
        var_node->name = ast_node->names[i];
        Type* projected = NULL;
        if (is_named && source_map && source_map->shape) {
            FOR_EACH_MAP_FIELD(source_map, se) {
                if (se->name && (int)se->name->length == (int)var_node->name->len &&
                        strncmp(se->name->str, var_node->name->chars,
                            se->name->length) == 0) {
                    Type* ft = unwrap_simple_type_type(se->type);
                    if (ft && (ft->type_id == LMD_TYPE_MAP ||
                            ft->type_id == LMD_TYPE_ELEMENT ||
                            ft->type_id == LMD_TYPE_OBJECT)) {
                        projected = ft;
                    }
                    break;
                }
            }
        } else if (!is_named && source_elem) {
            projected = source_elem;
        }
        var_node->type = projected ? projected : set_type_any(tp, ANY_DECOMPOSE);
        var_node->as = nullptr;
        push_name(tp, var_node, NULL);
        // `AstDecomposeNode` has no AstNamedNode-compatible payload for a
        // target entry. Retain the entry created by the canonical name pass so
        // every consumer uses its planned storage rather than re-looking up a
        // shadowable source spelling at evaluation time.
        ast_node->entries[i] = tp->current_scope->last;
    }

    return (AstNode*)ast_node;
}

// Build decomposition expression: let a, b = expr OR let a, b at expr.




// With the trimmed grammar a type annotation is ONE scanner token, so these
// questions are answered from the token's text.



// `type X = \(...)` / `type X = \symbol(...)` declares a pattern.




bool pattern_ast_literal_set(AstNode* node) {
    if (!node) return false;
    if (node->node_type == AST_NODE_PRIMARY) {
        Type* type = node->type;
        return type && type->type_id == LMD_TYPE_STRING && type->is_literal;
    }
    if (node->node_type == AST_NODE_BINARY_TYPE || node->node_type == AST_NODE_BINARY) {
        AstBinaryNode* binary = (AstBinaryNode*)node;
        return binary->op == OPERATOR_UNION &&
            pattern_ast_literal_set(binary->left) && pattern_ast_literal_set(binary->right);
    }
    return false;
}

bool pattern_ast_has_symbol_literal(AstNode* node) {
    if (!node) return false;
    switch (node->node_type) {
    case AST_NODE_PRIMARY:
        return node->type && node->type->type_id == LMD_TYPE_SYMBOL;
    case AST_NODE_BINARY:
    case AST_NODE_BINARY_TYPE: {
        AstBinaryNode* binary = (AstBinaryNode*)node;
        return pattern_ast_has_symbol_literal(binary->left) ||
            pattern_ast_has_symbol_literal(binary->right);
    }
    case AST_NODE_UNARY:
    case AST_NODE_UNARY_TYPE:
        return pattern_ast_has_symbol_literal(((AstUnaryNode*)node)->operand);
    case AST_NODE_PATTERN_SEQ: {
        AstNode* child = ((AstPatternSeqNode*)node)->first;
        while (child) {
            if (pattern_ast_has_symbol_literal(child)) return true;
            child = child->next;
        }
        return false;
    }
    case AST_NODE_LIST_TYPE:
    case AST_NODE_ARRAY_TYPE: {
        AstNode* child = ((AstListNode*)node)->item;
        while (child) {
            if (pattern_ast_has_symbol_literal(child)) return true;
            child = child->next;
        }
        return false;
    }
    case AST_NODE_PATTERN_ISLAND:
        return pattern_ast_has_symbol_literal(((AstPatternIslandNode*)node)->pattern);
    default:
        return false;
    }
}



// ==================== Namespace Attribute Desugaring ====================
// Desugar ns.attr: val → ns: {attr: val} at AST build time (v2 namespace design)

// Build a synthetic map node wrapping a single key-value pair: {attr_name: val_expr}
// Used for desugaring ns.attr: val → ns: {attr: val}
static AstNode* build_ns_attr_map_from_parts(Transpiler* tp, StrView attr_name,
        AstNode* val_expr, SourceSpan span) {
    AstMapNode* map_node = (AstMapNode*)alloc_ast_node_from_span(tp,
        AST_NODE_MAP, span, sizeof(AstMapNode));
    TypeMap* map_type = (TypeMap*)alloc_type(tp->pool, LMD_TYPE_MAP, sizeof(TypeMap));
    map_node->type = (Type*)map_type;

    // create key_expr: attr_name: val_expr
    AstNamedNode* key_node = (AstNamedNode*)alloc_ast_node_from_span(tp,
        AST_NODE_KEY_EXPR, span, sizeof(AstNamedNode));
    key_node->name = name_pool_create_strview(tp->name_pool, attr_name);
    key_node->as = val_expr;
    key_node->type = val_expr->type;
    map_node->item = (AstNode*)key_node;

    // create shape entry
    ShapeEntry* entry = (ShapeEntry*)pool_calloc(tp->pool, sizeof(ShapeEntry));
    StrView* name_view = (StrView*)pool_calloc(tp->pool, sizeof(StrView));
    name_view->str = key_node->name->chars;
    name_view->length = key_node->name->len;
    entry->name = name_view;
    entry->type = val_expr->type;
    entry->byte_offset = 0;

    map_type->shape = entry;
    map_type->length = 1;
    map_type->byte_size = type_info[type_field_storage_type_id(val_expr->type)].byte_size;

    arraylist_append(tp->type_list, map_type);
    map_type->type_index = tp->type_list->length - 1;

    return (AstNode*)map_node;
}



// Merge two map AST nodes: append src map's items and shape entries to dst map
// Used when multiple ns.attr attrs share the same ns prefix
static void merge_ns_attr_maps(Transpiler* tp, AstNode* dst_item, AstNode* src_item) {
    if (!dst_item || !src_item) return;
    if (dst_item->type->type_id != LMD_TYPE_MAP || src_item->type->type_id != LMD_TYPE_MAP) return;

    AstMapNode* dst = (AstMapNode*)dst_item;
    AstMapNode* src = (AstMapNode*)src_item;
    TypeMap* dst_type = (TypeMap*)dst->type;
    TypeMap* src_type = (TypeMap*)src->type;

    // append src items to dst item linked list
    AstNode* last_item = dst->item;
    while (last_item && last_item->next) last_item = last_item->next;
    if (last_item) last_item->next = src->item;
    else dst->item = src->item;

    // append src shape entries to dst shape linked list
    ShapeEntry* last_entry = dst_type->shape;
    while (last_entry && last_entry->next) last_entry = last_entry->next;
    if (last_entry) {
        // update byte_offset for merged entries
        int byte_offset = last_entry->byte_offset +
            type_info[shape_entry_storage_type_id(last_entry)].byte_size;
        ShapeEntry* src_entry = src_type->shape;
        while (src_entry) {
            src_entry->byte_offset = byte_offset;
            byte_offset += type_info[shape_entry_storage_type_id(src_entry)].byte_size;
            src_entry = src_entry->next;
        }
        last_entry->next = src_type->shape;
    } else {
        dst_type->shape = src_type->shape;
    }

    dst_type->length += src_type->length;
    dst_type->byte_size += src_type->byte_size;
}

// Find an existing named item (key_expr) in a linked list by key name
// Returns the AstNamedNode if found, NULL otherwise
static AstNamedNode* find_existing_named_item(AstNode* first_item, String* name) {
    AstNode* item = first_item;
    while (item) {
        if (item->node_type == AST_NODE_KEY_EXPR) {
            AstNamedNode* named = (AstNamedNode*)item;
            if (named->name && named->name->len == name->len &&
                memcmp(named->name->chars, name->chars, name->len) == 0) {
                return named;
            }
        }
        item = item->next;
    }
    return NULL;
}







// One source of truth for the base-type keywords. A table beats the former
// 30-branch if/else chain, and the hand parser uses the same mapping.
typedef struct { const char* name; Type* type; } BaseTypeName;
static const BaseTypeName BASE_TYPE_NAMES[] = {
    {"null", (Type*)&LIT_TYPE_NULL},      {"bool", (Type*)&LIT_TYPE_BOOL},
    {"int", (Type*)&LIT_TYPE_INT},        {"float", (Type*)&LIT_TYPE_FLOAT},
    {"complex", (Type*)&LIT_TYPE_COMPLEX},
    // f64 is accepted on input but canonicalizes to float.
    {"f64", (Type*)&LIT_TYPE_FLOAT},      {"decimal", (Type*)&LIT_TYPE_DECIMAL},
    {"integer", (Type*)&LIT_TYPE_INTEGER},{"number", (Type*)&LIT_TYPE_NUMBER},
    {"string", (Type*)&LIT_TYPE_STRING},  {"symbol", (Type*)&LIT_TYPE_SYMBOL},
    {"datetime", (Type*)&LIT_TYPE_DTIME}, {"time", (Type*)&LIT_TYPE_TIME},
    {"date", (Type*)&LIT_TYPE_DATE},      {"binary", (Type*)&LIT_TYPE_BINARY},
    {"list", (Type*)&LIT_TYPE_LIST},      {"range", (Type*)&LIT_TYPE_RANGE},
    {"array", (Type*)&LIT_TYPE_ARRAY},    {"map", (Type*)&LIT_TYPE_MAP},
    {"element", (Type*)&LIT_TYPE_ELMT},   {"object", (Type*)&LIT_TYPE_OBJECT},
    {"function", (Type*)&LIT_TYPE_FUNC},  {"type", (Type*)&LIT_TYPE_TYPE},
    {"error", (Type*)&LIT_TYPE_ERROR},
    {"i8", (Type*)&LIT_TYPE_I8},          {"i16", (Type*)&LIT_TYPE_I16},
    {"i32", (Type*)&LIT_TYPE_I32},        {"i64", (Type*)&LIT_TYPE_INT64},
    {"u8", (Type*)&LIT_TYPE_U8},          {"u16", (Type*)&LIT_TYPE_U16},
    {"u32", (Type*)&LIT_TYPE_U32},        {"u64", (Type*)&LIT_TYPE_U64},
    {"f16", (Type*)&LIT_TYPE_F16},        {"f32", (Type*)&LIT_TYPE_F32},
};

Type* lookup_base_type_name(Transpiler* tp, StrView name) {
    // `any` is the one entry that is not a constant: it records whether the
    // annotation spelled it explicitly.
    if (strview_equal(&name, "any")) { return set_lit_type_any(tp, ANY_EXPLICIT); }
    for (size_t i = 0; i < sizeof(BASE_TYPE_NAMES)/sizeof(BASE_TYPE_NAMES[0]); i++) {
        if (strview_equal(&name, BASE_TYPE_NAMES[i].name)) { return BASE_TYPE_NAMES[i].type; }
    }
    return NULL;
}



// ============================================================================
// Resolve base type for inheritance: returns the parent TypeObject*, or NULL
// Also copies parent fields into child shape entries (parent fields first).
// ============================================================================


// ============================================================================
// Push parent fields into scope so child methods can reference them (implicit this)
// ============================================================================


// Build object methods while the object's fields are visible as implicit names.


// ============================================================================
// Object type definition: type Point { x: float, y: float; fn magnitude() => ... }
// ============================================================================
ShapeEntry* append_shape_entry_typed(Transpiler* tp, String* pooled_name, Type* field_type,
        ShapeEntry** shape, ShapeEntry** prev_entry, int byte_offset) {
    StrView* name_view = (StrView*)pool_calloc(tp->pool, sizeof(StrView));
    name_view->str = pooled_name->chars;
    name_view->length = pooled_name->len;
    ShapeEntry* shape_entry = (ShapeEntry*)pool_calloc(tp->pool, sizeof(ShapeEntry));
    shape_entry->name = name_view;
    shape_entry->type = field_type;
    shape_entry->byte_offset = byte_offset;
    if (!*shape) *shape = shape_entry;
    else (*prev_entry)->next = shape_entry;
    *prev_entry = shape_entry;
    return shape_entry;
}



// build range type: start to end (e.g. 1 to 10, 'a' to 'z')
// constructs as a binary node with OPERATOR_TO and type LMD_TYPE_RANGE
// build constrained type: base_type where (constraint)
// e.g. int where (5 < ~ < 10), string where (len(~) > 0)


AstBinaryNode* build_registered_binary_type_from_span(Transpiler* tp,
        SourceSpan span,
        AstNode* left, AstNode* right, Type* left_type, Type* right_type,
        Operator op, StrView op_str) {
    AstBinaryNode* binary = (AstBinaryNode*)alloc_ast_node_from_span(tp,
        AST_NODE_BINARY_TYPE, span, sizeof(AstBinaryNode));
    binary->type = alloc_type(tp->pool, LMD_TYPE_TYPE, sizeof(TypeType));
    TypeBinary* type = (TypeBinary*)alloc_type_kind(tp->pool, TYPE_KIND_BINARY,
        sizeof(TypeBinary));
    ((TypeType*)binary->type)->type = (Type*)type;
    binary->left = left;
    binary->right = right;
    binary->op = op;
    binary->op_str = op_str;
    type->left = unwrap_simple_type_type(left_type);
    type->right = unwrap_simple_type_type(right_type);
    type->op = op;
    arraylist_append(tp->type_list, binary->type);
    type->type_index = tp->type_list->length - 1;
    return binary;
}



// Helper function to parse occurrence count from string like "[]", "[2]", "[2, 5]", "[2+]"
// Sets min_count and max_count; max_count=-1 means unbounded
void parse_occurrence_count(StrView op_str, int* min_count, int* max_count) {
    *min_count = 0;
    *max_count = -1;  // unbounded by default

    if (op_str.length < 2 || op_str.str[0] != '[') {
        return;
    }

    // [] - any count (equivalent to *)
    if (op_str.length == 2 && op_str.str[1] == ']') {
        *min_count = 0;
        *max_count = -1;
        return;
    }

    if (op_str.length < 3) {
        return;
    }

    // skip opening bracket
    const char* p = op_str.str + 1;
    const char* end = op_str.str + op_str.length;

    // parse first number
    int n1 = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        n1 = n1 * 10 + (*p - '0');
        p++;
    }
    *min_count = n1;

    // skip whitespace
    while (p < end && (*p == ' ' || *p == '\t')) p++;

    if (p < end) {
        if (*p == ']') {
            // [n] - exact count
            *max_count = n1;
        } else if (*p == '+') {
            // [n+] - unbounded minimum
            *max_count = -1;
        } else if (*p == ',') {
            // [n, m] - range
            p++;  // skip comma
            while (p < end && (*p == ' ' || *p == '\t')) p++;  // skip whitespace
            int n2 = 0;
            while (p < end && *p >= '0' && *p <= '9') {
                n2 = n2 * 10 + (*p - '0');
                p++;
            }
            *max_count = n2;
        }
    }

    log_debug("parsed occurrence: min=%d, max=%d from '%.*s'", *min_count, *max_count, (int)op_str.length, op_str.str);
}

AstNode* build_function_return_contract_node_from_span(Transpiler* tp,
        SourceSpan span,
        Type* returned, Type* error_type, bool can_raise) {
    // Both grammar paths must carry exactly this compact TypeFunc contract;
    // build_func reads it before replacing the wrapper with the declared fn.
    AstFuncNode* wrapper_node = (AstFuncNode*)alloc_ast_node_from_span(tp,
        AST_NODE_FUNC_TYPE, span, sizeof(AstFuncNode));
    wrapper_node->type = alloc_type(tp->pool, LMD_TYPE_TYPE, sizeof(TypeType));
    TypeFunc* fn_type_info = (TypeFunc*)alloc_type(tp->pool, LMD_TYPE_FUNC, sizeof(TypeFunc));
    ((TypeType*)wrapper_node->type)->type = (Type*)fn_type_info;

    fn_type_info->returned = returned;
    fn_type_info->inferred_return = returned;
    set_function_return_contract(fn_type_info, returned, true);
    fn_type_info->error_type = error_type;
    fn_type_info->can_raise = can_raise;

    log_debug("return type: ok=%d, error=%d, can_raise=%d",
        returned ? returned->type_id : -1,
        error_type ? error_type->type_id : -1,
        can_raise);

    return (AstNode*)wrapper_node;
}



// todo: build reference type

static ShapeEntry* build_map_shape_entry(Transpiler* tp, TypeMap* owner, AstNode* item,
                                         bool is_spread, bool normalize_type) {
    ShapeEntry* shape_entry = (ShapeEntry*)pool_calloc(tp->pool, sizeof(ShapeEntry));
    // a nameless slot makes owner->length stop counting fields; see TypeMap::has_spread
    if (is_spread && owner) owner->has_spread = true;
    if (!is_spread) {
        String* pooled_name = ((AstNamedNode*)item)->name;
        StrView* name_view = (StrView*)pool_calloc(tp->pool, sizeof(StrView));
        name_view->str = pooled_name->chars;
        name_view->length = pooled_name->len;
        shape_entry->name = name_view;
    } else {
        shape_entry->name = NULL;
    }
    Type* field_type = item->type;
    if (normalize_type && field_type && field_type->type_id == LMD_TYPE_TYPE) {
        bool is_type_value = !is_spread &&
            ast_is_explicit_type_value(((AstNamedNode*)item)->as);
        if (is_type_value) {
            field_type = &TYPE_TYPE;
        } else if (!type_is_global_meta_type(field_type) &&
                   field_type->kind == TYPE_KIND_SIMPLE) {
            field_type = ((TypeType*)field_type)->type;
        }
    }
    shape_entry->type = field_type;
    if (!shape_entry->name && !(field_type->type_id == LMD_TYPE_MAP ||
                                field_type->type_id == LMD_TYPE_ANY)) {
        if (normalize_type) {
            log_error("invalid map item type %s, should be map or any",
                get_type_name(field_type->type_id));
        } else {
            log_debug("invalid map item type %d, should be map or any",
                field_type->type_id);
        }
    }
    return shape_entry;
}

static bool ast_node_is_syntactic_spread_key(Transpiler* tp, AstNode* item) {
    if (!item || item->node_type != AST_NODE_KEY_EXPR) return false;

    // A quoted `'*'` remains an ordinary key. The source spelling is retained
    // until the direct sink has committed the map item, so inspect it here
    // instead of collapsing name text into spread semantics.
    StrView source = ast_node_source(tp, item);
    return source.length > 0 && source.str[0] == '*';
}

AstNode* build_map_from_items(Transpiler* tp, SourceSpan span,
        AstNode* items) {
    AstMapNode* ast_node = (AstMapNode*)alloc_ast_node_from_span(tp,
        AST_NODE_MAP, span, sizeof(AstMapNode));
    ast_node->type = alloc_type(tp->pool, LMD_TYPE_MAP, sizeof(TypeMap));
    TypeMap* type = (TypeMap*)ast_node->type;

    AstNode* prev_item = NULL;  ShapeEntry* prev_entry = NULL;  int byte_offset = 0;
    for (AstNode* raw_item = items; raw_item;) {
        AstNode* next_item = raw_item->next;
        bool is_spread = ast_node_is_syntactic_spread_key(tp, raw_item);
        AstNode* item = is_spread ? ((AstNamedNode*)raw_item)->as : raw_item;
        if (!item) { log_error("build_map: null expr item");  break; }

        // The input chain represents syntactic map items, but the retained
        // AST represents a spread by its value. Detach before relinking so a
        // direct parser's raw reduction list cannot leak stale sibling links.
        item->next = NULL;

        if (!prev_item) { ast_node->item = item; }
        else { prev_item->next = item; }
        prev_item = item;

        ShapeEntry* shape_entry = build_map_shape_entry(tp, type, item, is_spread, true);
        // Only syntactic type expressions produce a first-class Type* field.
        // Ordinary calls can carry a TypeType-shaped abstract contract while
        // still returning an Item; treating that carrier as `type` makes a
        // numeric result read through the Type* lane and dereference its bits.
        Type* field_type = shape_entry->type;
        shape_entry->byte_offset = byte_offset;
        if (!prev_entry) { type->shape = shape_entry; }
        else { prev_entry->next = shape_entry; }
        prev_entry = shape_entry;

        type->length++;
        byte_offset += (!is_spread) ?
            type_info[type_field_storage_type_id(field_type)].byte_size : sizeof(void*);
        raw_item = next_item;
    }
    type->byte_size = byte_offset;

    arraylist_append(tp->type_list, type);
    type->type_index = tp->type_list->length - 1;
    return (AstNode*)ast_node;
}



static TypeObject* lookup_object_type_for_tag(Transpiler* tp, StrView tag_name) {
    NameEntry* entry = lookup_name(tp, tag_name);
    if (!entry || !entry->node || !entry->node->type) return NULL;
    Type* resolved = entry->node->type;
    if (resolved->type_id != LMD_TYPE_TYPE) return NULL;
    Type* inner = ((TypeType*)resolved)->type;
    return inner && inner->type_id == LMD_TYPE_OBJECT
        ? (TypeObject*)inner : NULL;
}

static AstNode* build_object_literal_from_items(Transpiler* tp,
        SourceSpan span, StrView tag_name, TypeObject* object_type,
        AstNode* children) {
    AstObjectLiteralNode* object = (AstObjectLiteralNode*)alloc_ast_node_from_span(
        tp, AST_NODE_OBJECT_LITERAL, span, sizeof(AstObjectLiteralNode));
    object->type_name = name_pool_create_strview(tp->name_pool, tag_name);
    object->type = (Type*)object_type;
    object->item = NULL;

    AstNode* prev = NULL;
    for (AstNode* raw = children; raw;) {
        AstNode* next = raw->next;
        raw->next = NULL;
        if (raw->node_type != AST_NODE_CONTENT) {
            bool spread = raw->node_type == AST_NODE_KEY_EXPR &&
                ast_node_is_syntactic_spread_key(tp, raw);
            AstNode* item = spread ? ((AstNamedNode*)raw)->as : raw;
            if (item) {
                item->next = NULL;
                if (!object->item) object->item = item;
                else prev->next = item;
                prev = item;
            }
        }
        raw = next;
    }
    return (AstNode*)object;
}



static bool join_expr_mentions_name(AstNode* node, String* name) {
    if (!node || !name) return false;
    while (node && node->node_type == AST_NODE_PRIMARY) {
        node = ((AstPrimaryNode*)node)->expr;
    }
    if (!node) return false;

    switch (node->node_type) {
    case AST_NODE_IDENT: {
        AstIdentNode* ident = (AstIdentNode*)node;
        return ident->name == name || (ident->name && ident->name->len == name->len &&
            memcmp(ident->name->chars, name->chars, name->len) == 0);
    }
    case AST_NODE_MEMBER_EXPR: {
        AstFieldNode* field = (AstFieldNode*)node;
        // A field-name identifier is not a value binding; only the receiver can mention the loop var.
        return join_expr_mentions_name(field->object, name);
    }
    case AST_NODE_INDEX_EXPR:
    {
        AstFieldNode* field = (AstFieldNode*)node;
        return join_expr_mentions_name(field->object, name) ||
               join_expr_mentions_name(field->field, name);
    }
    case AST_NODE_PATH_INDEX_EXPR: {
        AstPathIndexNode* path_idx = (AstPathIndexNode*)node;
        return join_expr_mentions_name(path_idx->base_path, name) ||
               join_expr_mentions_name(path_idx->segment_expr, name);
    }
    case AST_NODE_BINARY: {
        AstBinaryNode* bin = (AstBinaryNode*)node;
        return join_expr_mentions_name(bin->left, name) ||
               join_expr_mentions_name(bin->right, name);
    }
    case AST_NODE_UNARY:
    case AST_NODE_SPREAD: {
        AstUnaryNode* un = (AstUnaryNode*)node;
        return join_expr_mentions_name(un->operand, name);
    }
    case AST_NODE_CALL_EXPR: {
        AstCallNode* call = (AstCallNode*)node;
        if (join_expr_mentions_name(call->function, name)) return true;
        for (AstNode* arg = call->argument; arg; arg = arg->next) {
            if (join_expr_mentions_name(arg, name)) return true;
        }
        return false;
    }
    case AST_NODE_HANDLER_EXPR:
    case AST_NODE_HANDLER_STAM: {
        AstHandlerNode* handler = (AstHandlerNode*)node;
        return join_expr_mentions_name(handler->operand, name) ||
            join_expr_mentions_name(handler->body, name) ||
            join_expr_mentions_name(handler->value_body, name);
    }
    default:
        return false;
    }
}

static void append_join_key_spec(Transpiler* tp, AstLoopNode* loop, AstNode* prior_expr, AstNode* new_expr) {
    // Direct reductions have no parser node; the loop span is the shared
    // source anchor for both builders.
    AstJoinKey* spec = (AstJoinKey*)alloc_ast_node_from_span(tp,
        AST_NODE_JOIN_KEY, loop->source_span, sizeof(AstJoinKey));
    spec->prior_expr = prior_expr;
    spec->new_expr = new_expr;
    if (!loop->join_keys) {
        loop->join_keys = spec;
    } else {
        AstJoinKey* tail = loop->join_keys;
        while (tail->next) tail = (AstJoinKey*)tail->next;
        tail->next = (AstNode*)spec;
    }
    loop->join_key_count++;
}

static void build_join_key_specs(Transpiler* tp, AstLoopNode* loop, AstNode* on_expr) {
    AstNode* expr = on_expr;
    while (expr && expr->node_type == AST_NODE_PRIMARY) {
        expr = ((AstPrimaryNode*)expr)->expr;
    }
    if (!expr || expr->node_type != AST_NODE_BINARY) {
        log_error("Error: join 'on' must be an equality or 'and' of equalities");
        return;
    }

    AstBinaryNode* bin = (AstBinaryNode*)expr;
    if (bin->op == OPERATOR_AND) {
        build_join_key_specs(tp, loop, bin->left);
        build_join_key_specs(tp, loop, bin->right);
        return;
    }
    if (bin->op != OPERATOR_EQ) {
        log_error("Error: join 'on' only supports equality conditions; put non-equi filters in 'where'");
        return;
    }

    bool left_new = join_expr_mentions_name(bin->left, loop->name);
    bool right_new = join_expr_mentions_name(bin->right, loop->name);
    if (left_new == right_new) {
        log_error("Error: each join equality must reference the new source '%.*s' on exactly one side",
            (int)loop->name->len, loop->name->chars);
        return;
    }
    if (left_new) append_join_key_spec(tp, loop, bin->right, bin->left);
    else append_join_key_spec(tp, loop, bin->left, bin->right);
}



// Helper: build order_spec node


// Helper: build for_let_clause node (reuses AstNamedNode)


static String* infer_group_key_alias(Transpiler* tp, AstNode* key_expr) {
    AstNode* scan = key_expr;
    while (scan && scan->node_type == AST_NODE_PRIMARY) {
        scan = ((AstPrimaryNode*)scan)->expr;
    }
    if (!scan || scan->node_type != AST_NODE_MEMBER_EXPR) {
        return NULL;
    }
    AstNode* field = ((AstFieldNode*)scan)->field;
    while (field && field->node_type == AST_NODE_PRIMARY) {
        field = ((AstPrimaryNode*)field)->expr;
    }
    if (!field || field->node_type != AST_NODE_IDENT) {
        return NULL;
    }
    return ((AstIdentNode*)field)->name;
}



static void enter_for_group_scope(Transpiler* tp, AstForNode* for_node) {
    NameScope* row_scope = for_node->vars;
    NameScope* parent = row_scope ? row_scope->parent : tp->current_scope;
    bool is_proc = tp->current_scope && tp->current_scope->is_proc;
    // Grouping commits the row-scope boundary before aggregate clauses. The
    // post-group name can see the enclosing scope but never loop/let bindings.
    if (row_scope && tp->current_scope == row_scope) {
        lambda_ast_leave_scope(tp, row_scope);
    }
    lambda_ast_enter_scope_with_parent(tp, parent, is_proc);
}

// Helper: build group by clause


// Helper function to build all for clauses (shared between for_expr and for_stam)
// Three-pass approach:
// Pass 1: Process loop declarations to register loop vars in scope
// Pass 2: Process let clauses (can reference loop vars)
// Pass 3: Process where, group, order, limit, offset (can reference both)






// `apply;` (splat) statement: re-dispatch each child of the matched item (~)
// through the template registry. Equivalent to `for (c in ~) apply(c)`.
// Synthesizes the for-expr AST so existing MIR codegen handles it.


// shared guard for the procedural-only statements (var/assign/while/break/continue/return).
// Recording a semantic error rather than only logging it is load-bearing: each guard returns
// NULL, leaving a hole in the AST (a rejected `var` never enters its name into the scope), and
// runner.cpp:730 only returns before MIR when error_count > 0. With a bare log_error the build
// looked clean, MIR ran against the holey AST, and the real diagnostic was buried under invented
// follow-on errors such as "mir: undefined variable 'x'".


// while statement (procedural only)


// break statement (procedural only)


// continue statement (procedural only)


// return statement (procedural only)


// raise statement - raises an error to the caller
// Allowed in:
// 1. Procedural functions (pn) - can always raise
// 2. Pure functions (fn) with error return type (T^E or T@)




// raise expression (functional) - raises an error in expression context


// var statement for mutable variables (procedural only)


// assignment statement for mutable variables (procedural only)
// supports: x = val, arr[i] = val, obj.field = val


// returns NULL for variadic marker (...)
// Fold a declared type into a TypeParam: copy the compact Type prefix, restore
// the param-only flags, then choose the retained contract and full_type. Shared
// with the type-pattern hand parser, which builds `fn(a: T)` params from spans.
void apply_declared_param_type(Transpiler* tp, TypeParam* param_type, Type* declared) {
    bool was_optional = param_type->is_optional;
    bool was_var_param = param_type->is_var_param;
    AstNode* default_value = param_type->default_value;
    // Copy base Type fields
    *(Type*)param_type = *declared;
    param_type->kind = TYPE_KIND_PARAM;
    param_type->is_optional = was_optional;
    param_type->is_var_param = was_var_param;
    param_type->default_value = default_value;

    Type* parameter_contract = parameter_contract_for_declared(tp, declared,
        was_optional, default_value);
    set_param_contract(param_type, parameter_contract, true);

    // For complex types (TypeBinary, TypeUnary) and named map/object types,
    // store pointer to full type so downstream code can reach the extended
    // fields (shape, struct_name, methods, ...).
    if (!is_global_simple_type(parameter_contract) &&
            (parameter_contract->kind == TYPE_KIND_BINARY ||
             parameter_contract->kind == TYPE_KIND_UNARY)) {
        param_type->full_type = parameter_contract;
    } else if (is_param_full_type_id(parameter_contract->type_id)) {
        param_type->full_type = parameter_contract;
    } else {
        param_type->full_type = NULL;
    }
}

// Wrapper over the static inline setter so the hand parser can declare a fn
// type's return contract without duplicating the field assignments.
void set_fn_return_contract(TypeFunc* fn_type, Type* contract, bool is_explicit) {
    set_function_return_contract(fn_type, contract, is_explicit);
}



AstNamedNode* build_named_argument_from_parts(Transpiler* tp,
        SourceSpan span, StrView name, AstNode* value) {
    AstNamedNode* ast_node = (AstNamedNode*)alloc_ast_node_from_span(tp,
        AST_NODE_NAMED_ARG, span, sizeof(AstNamedNode));
    ast_node->name = name_pool_create_strview(tp->name_pool, name);
    ast_node->as = value;
    ast_node->type = value ? value->type : &TYPE_ANY;

    log_debug("named argument: %s", ast_node->name);
    return ast_node;
}

// build named argument in function call: name: value


typedef struct ReturnBoundaryScan {
    Transpiler* tp;
    Type* expected;
    String* function_name;
    bool accepts_error;
} ReturnBoundaryScan;

typedef struct ReturnErrorOriginScan {
    AstCallNode* call;
} ReturnErrorOriginScan;

static bool find_first_return_error_call(AstNode* node, void* data) {
    ReturnErrorOriginScan* scan = (ReturnErrorOriginScan*)data;
    if (node && node->node_type == AST_NODE_CALL_EXPR &&
            lambda_type_has_proven_error(node->type)) {
        scan->call = (AstCallNode*)node;
        return false;
    }
    return true;
}

static bool return_error_call_name(AstCallNode* call, const char** name, int* length) {
    if (!call || !name || !length) return false;
    AstNode* callee = call->function;
    while (callee && callee->node_type == AST_NODE_PRIMARY) {
        callee = ((AstPrimaryNode*)callee)->expr;
    }
    if (callee && callee->node_type == AST_NODE_SYS_FUNC) {
        SysFuncInfo* info = ((AstSysFuncNode*)callee)->fn_info;
        if (info && info->name) {
            *name = info->name;
            *length = (int)strlen(info->name);
            return true;
        }
    }
    if (callee && callee->node_type == AST_NODE_IDENT) {
        String* ident_name = ((AstIdentNode*)callee)->name;
        if (ident_name) {
            *name = ident_name->chars;
            *length = (int)ident_name->len;
            return true;
        }
    }
    return false;
}

static void record_return_contract_error(Transpiler* tp, AstFuncNode* fn,
        AstNode* site, Type* expected, Type* actual, bool has_explicit_contract) {
    LambdaSourcePoint point = ast_node_start_point(tp, site ? site : (AstNode*)fn);
    ReturnErrorOriginScan origin = {0};
    if (site) walk_lambda_ast(site, find_first_return_error_call, &origin, false);
    const char* call_name = NULL;
    int call_name_length = 0;
    if (lambda_type_has_proven_error(actual) &&
            return_error_call_name(origin.call, &call_name, &call_name_length)) {
        record_type_error_code(tp, (int)point.row + 1, ERR_RETURN_TYPE_MISMATCH,
            "function '%.*s' may return error from call to '%.*s'; contain it with `or`, declare `| error`, or propagate with `^`",
            fn->name ? (int)fn->name->len : 10,
            fn->name ? fn->name->chars : "<function>", call_name_length, call_name);
        return;
    }
    char expected_name[128];
    char actual_name[128];
    lambda_type_format_name(expected, expected_name, sizeof(expected_name));
    lambda_type_format_name(actual, actual_name, sizeof(actual_name));
    if (has_explicit_contract) {
        // Keep the established declared-return wording for source-compatible
        // diagnostics; implicit fn firewalls identify their synthesized rule.
        record_type_error_code(tp, (int)point.row + 1, ERR_RETURN_TYPE_MISMATCH,
            "function '%.*s' body returns type %s, declared return type %s",
            fn->name ? (int)fn->name->len : 10,
            fn->name ? fn->name->chars : "<function>",
            actual_name, expected_name);
        return;
    }
    record_type_error_code(tp, (int)point.row + 1, ERR_RETURN_TYPE_MISMATCH,
        "function '%.*s' body returns type %s, implicit return contract is %s",
        fn->name ? (int)fn->name->len : 10,
        fn->name ? fn->name->chars : "<function>",
        actual_name, expected_name);
}

static bool validate_declared_return_boundary(AstNode* node, void* data) {
    ReturnBoundaryScan* scan = (ReturnBoundaryScan*)data;
    if (!node || node->node_type != AST_NODE_RETURN_STAM) return true;
    AstReturnNode* ret = (AstReturnNode*)node;
    Type* actual = ret->value && ret->value->type ? ret->value->type : &TYPE_NULL;
    // `T^` carries errors on its declared channel.  A `raise` expression has
    // type error in the body, but it is a valid early return for that channel,
    // not a value attempting to inhabit the successful T result.
    if (scan->accepts_error && boundary_unwrap_type(actual)->type_id == LMD_TYPE_ERROR) {
        return true;
    }
    // A declared raised channel owns the error member at this return boundary.
    // Compare only the successful members so `return f()` in `T^` does not
    // reject the same error channel that its signature explicitly publishes.
    StaticBoundaryResult relation = scan->accepts_error
        ? static_parameter_boundary_relation(actual, scan->expected)
        : static_boundary_relation(actual, scan->expected);
    if (relation == STATIC_BOUNDARY_REJECTED) {
        LambdaSourcePoint point = ast_node_start_point(scan->tp, node);
        char expected_name[128];
        char actual_name[128];
        lambda_type_format_name(scan->expected, expected_name, sizeof(expected_name));
        lambda_type_format_name(actual, actual_name, sizeof(actual_name));
        record_type_error_code(scan->tp, (int)point.row + 1, ERR_RETURN_TYPE_MISMATCH,
            "return from '%.*s' expected %s, got %s",
            scan->function_name ? (int)scan->function_name->len : 10,
            scan->function_name ? scan->function_name->chars : "<function>",
            expected_name, actual_name);
    }
    return should_continue_transpiling(scan->tp);
}

static void validate_explicit_return_boundaries(Transpiler* tp, AstFuncNode* fn,
        Type* expected, bool accepts_error) {
    if (!fn || !fn->body || !expected || expected == &TYPE_ANY) return;
    ReturnBoundaryScan scan = {tp, expected, fn->name, accepts_error};
    // Nested functions own their return contracts.
    walk_lambda_ast(fn->body, validate_declared_return_boundary, &scan, false);
}

static void validate_function_return_contract(Transpiler* tp, AstFuncNode* fn,
        TypeFunc* signature) {
    if (!fn || !signature || signature->is_proc) return;
    Type* expected = signature->return_contract ? signature->return_contract :
        signature->returned;
    if (!expected || expected == &TYPE_ANY) return;
    bool accepts_error = signature->can_raise || lambda_type_accepts_error(expected);
    bool body_is_direct_error = fn->body && fn->body->type && accepts_error &&
        boundary_unwrap_type(fn->body->type)->type_id == LMD_TYPE_ERROR;
    StaticBoundaryResult body_relation = !fn->body || !fn->body->type || body_is_direct_error
        ? STATIC_BOUNDARY_PROVEN
        : (accepts_error
            ? static_parameter_boundary_relation(fn->body->type, expected)
            : static_boundary_relation(fn->body->type, expected));
    if (fn->body && fn->body->type && body_relation == STATIC_BOUNDARY_REJECTED) {
        record_return_contract_error(tp, fn, fn->body, expected, fn->body->type,
            signature->has_explicit_return_contract);
    }
    validate_explicit_return_boundaries(tp, fn, expected, accepts_error);
}

// E228 is a property of the enclosing expression, not of an AST build order.
// Validate after the body exists so only an immediate documented handler can
// consume an enforcing call; arbitrary wrappers must not hide the obligation.
static void record_unhandled_error_call(Transpiler* tp, AstCallNode* call) {
    if (!tp || !call) return;
    AstNode* function = boundary_unwrap_primary(call->function);
    const char* name = "function";
    int name_length = 8;
    if (function && function->node_type == AST_NODE_SYS_FUNC) {
        SysFuncInfo* info = ((AstSysFuncNode*)function)->fn_info;
        if (info && info->name) {
            name = info->name;
            name_length = (int)strlen(name);
        }
    } else if (function && function->node_type == AST_NODE_IDENT) {
        String* ident_name = ((AstIdentNode*)function)->name;
        if (ident_name) {
            name = ident_name->chars;
            name_length = (int)ident_name->len;
        }
    }
    record_semantic_error_span(tp, call->source_span, ERR_UNHANDLED_ERROR,
        "error from '%.*s' must be handled: use '%.*s(...)^' to propagate, "
        "handle with '%.*s(...) ^ { ... }', or recover with '%.*s(...) or default'",
        name_length, name, name_length, name, name_length, name, name_length, name);
}

static bool match_arm_is_error_handler(AstMatchArm* arm) {
    AstNode* pattern = arm ? boundary_unwrap_primary(arm->pattern) : NULL;
    if (!pattern || !pattern->type || pattern->type->type_id != LMD_TYPE_TYPE ||
            is_global_simple_type(pattern->type) ||
            pattern->type->kind != TYPE_KIND_SIMPLE) return false;
    // A named pattern has a TypePattern payload under the TYPE_TYPE meta-ID.
    // Only a simple TypeType owns the nested target below; treating a pattern
    // as that wrapper makes ordinary `case alias` dereference address 0x1.
    Type* type = ((TypeType*)pattern->type)->type;
    return type && boundary_unwrap_type(type)->type_id == LMD_TYPE_ERROR;
}

static bool match_has_error_handler(AstMatchNode* match) {
    if (!match) return false;
    for (AstMatchArm* arm = match->first_arm; arm; arm = (AstMatchArm*)arm->next) {
        if (match_arm_is_error_handler(arm)) return true;
    }
    return false;
}

static TypeFunc* call_function_signature(AstCallNode* call) {
    AstNode* function = call ? boundary_unwrap_primary(call->function) : NULL;
    return function && function->type && function->type->type_id == LMD_TYPE_FUNC
        ? (TypeFunc*)function->type : NULL;
}

static bool parameter_is_error_acknowledgment(TypeParam* parameter) {
    return parameter && parameter->has_explicit_contract && parameter->contract_type &&
        lambda_type_has_proven_error(parameter->contract_type);
}

static void validate_enforcing_calls_in_expression(Transpiler* tp, AstNode* node,
        bool immediate_acknowledgment, bool return_acknowledgment) {
    node = boundary_unwrap_primary(node);
    if (!node) return;

    switch (node->node_type) {
    case AST_NODE_CALL_EXPR: {
        AstCallNode* call = (AstCallNode*)node;
        if (call->can_raise && !call->propagate && !immediate_acknowledgment) {
            record_unhandled_error_call(tp, call);
        }
        TypeFunc* signature = call_function_signature(call);
        TypeParam* parameter = signature ? signature->param : NULL;
        for (AstNode* argument = call->argument; argument; argument = argument->next) {
            bool parameter_acknowledgment = parameter_is_error_acknowledgment(parameter);
            bool short_circuit_acknowledgment = immediate_acknowledgment && parameter &&
                parameter->contract_type &&
                !lambda_type_accepts_error(parameter->contract_type);
            // The direct caller returns an incoming error before entering an
            // error-excluding parameter. Its immediate handler therefore owns
            // that exact argument error, even through an implicit contract.
            parameter_acknowledgment = parameter_acknowledgment || short_circuit_acknowledgment;
            validate_enforcing_calls_in_expression(tp, argument, parameter_acknowledgment,
                return_acknowledgment);
            if (parameter) parameter = parameter->next;
        }
        return;
    }
    case AST_NODE_HANDLER_EXPR:
    case AST_NODE_HANDLER_STAM: {
        AstHandlerNode* handler = (AstHandlerNode*)node;
        // The operand is the acknowledged error-producing boundary; errors
        // introduced by the recovery body still need their own acknowledgment.
        validate_enforcing_calls_in_expression(tp, handler->operand, true,
            return_acknowledgment);
        validate_enforcing_calls_in_expression(tp, handler->body, false,
            return_acknowledgment);
        validate_enforcing_calls_in_expression(tp, handler->value_body, false,
            return_acknowledgment);
        return;
    }
    case AST_NODE_BINARY:
    case AST_NODE_PIPE: {
        AstBinaryNode* binary = (AstBinaryNode*)node;
        if (binary->op == OPERATOR_OR) {
            // `or` consumes an error only from its left operand. An enforcing
            // call in the fallback remains unhandled by this expression.
            validate_enforcing_calls_in_expression(tp, binary->left, true,
                return_acknowledgment);
            validate_enforcing_calls_in_expression(tp, binary->right, false,
                return_acknowledgment);
        } else {
            validate_enforcing_calls_in_expression(tp, binary->left, false,
                return_acknowledgment);
            validate_enforcing_calls_in_expression(tp, binary->right, false,
                return_acknowledgment);
        }
        return;
    }
    case AST_NODE_MATCH_EXPR: {
        AstMatchNode* match = (AstMatchNode*)node;
        validate_enforcing_calls_in_expression(tp, match->scrutinee,
            match_has_error_handler(match), return_acknowledgment);
        for (AstMatchArm* arm = match->first_arm; arm; arm = (AstMatchArm*)arm->next) {
            validate_enforcing_calls_in_expression(tp, arm->body, false,
                return_acknowledgment);
        }
        return;
    }
    case AST_NODE_IF_EXPR: {
        AstIfNode* branch = (AstIfNode*)node;
        validate_enforcing_calls_in_expression(tp, branch->cond, false, return_acknowledgment);
        validate_enforcing_calls_in_expression(tp, branch->then, false, return_acknowledgment);
        validate_enforcing_calls_in_expression(tp, branch->otherwise, false, return_acknowledgment);
        return;
    }
    case AST_NODE_ASSIGN:
    case AST_NODE_KEY_EXPR:
    case AST_NODE_NAMED_ARG: {
        AstNamedNode* named = (AstNamedNode*)node;
        bool binding_acknowledgment = named->declared_type &&
            lambda_type_has_proven_error(named->declared_type);
        validate_enforcing_calls_in_expression(tp, named->as, binding_acknowledgment,
            return_acknowledgment);
        return;
    }
    case AST_NODE_RETURN_STAM:
        validate_enforcing_calls_in_expression(tp, ((AstReturnNode*)node)->value,
            return_acknowledgment, return_acknowledgment);
        return;
    case AST_NODE_UNARY:
    case AST_NODE_SPREAD: {
        AstUnaryNode* unary = (AstUnaryNode*)node;
        validate_enforcing_calls_in_expression(tp, unary->operand,
            unary->op == OPERATOR_PROPAGATE, return_acknowledgment);
        return;
    }
    case AST_NODE_MEMBER_EXPR:
    case AST_NODE_INDEX_EXPR: {
        AstFieldNode* field = (AstFieldNode*)node;
        validate_enforcing_calls_in_expression(tp, field->object, false, return_acknowledgment);
        validate_enforcing_calls_in_expression(tp, field->field, false, return_acknowledgment);
        return;
    }
    case AST_NODE_CONTENT:
    case AST_NODE_LIST:
    case AST_NODE_ARRAY:
    case AST_NODE_MAP:
    case AST_NODE_ELEMENT: {
        for (AstNode* item = ((AstArrayNode*)node)->item; item; item = item->next) {
            validate_enforcing_calls_in_expression(tp, item, false, return_acknowledgment);
        }
        return;
    }
    case AST_NODE_ASSIGN_STAM:
    case AST_NODE_INDEX_ASSIGN_STAM:
    case AST_NODE_MEMBER_ASSIGN_STAM: {
        AstCompoundAssignNode* assign = (AstCompoundAssignNode*)node;
        validate_enforcing_calls_in_expression(tp, assign->object, false, return_acknowledgment);
        validate_enforcing_calls_in_expression(tp, assign->key, false, return_acknowledgment);
        validate_enforcing_calls_in_expression(tp, assign->value, false, return_acknowledgment);
        return;
    }
    case AST_NODE_FUNC:
    case AST_NODE_PROC:
    case AST_NODE_FUNC_EXPR:
        // Nested functions are validated against their own return contract.
        return;
    default:
        return;
    }
}



static void validate_top_level_enforcing_calls(Transpiler* tp, AstNode* node) {
    validate_enforcing_calls_in_expression(tp, node, false, false);
}

// Cross-frame writes are deliberately a source-level invalidation, not a
// runtime aliasing rule: a caller may not read an outer `var` after a closure
// that can write it unless an explicit assignment re-establishes the binding.
typedef struct InvalidatedBindingState {
    Transpiler* tp;
    String* names[64];
    int count;
} InvalidatedBindingState;

static bool invalidated_binding_contains(InvalidatedBindingState* state,
        String* name) {
    if (!state || !name) return false;
    for (int i = 0; i < state->count; i++) {
        if (same_name_string(state->names[i], name)) return true;
    }
    return false;
}

static void invalidated_binding_add(InvalidatedBindingState* state, String* name) {
    if (!state || !name || invalidated_binding_contains(state, name)) return;
    if (state->count < 64) state->names[state->count++] = name;
}

static void invalidated_binding_remove(InvalidatedBindingState* state, String* name) {
    if (!state || !name) return;
    for (int i = 0; i < state->count; i++) {
        if (!same_name_string(state->names[i], name)) continue;
        state->names[i] = state->names[--state->count];
        return;
    }
}

static void invalidated_binding_union(InvalidatedBindingState* dst,
        const InvalidatedBindingState* left,
        const InvalidatedBindingState* right) {
    if (!dst) return;
    dst->count = 0;
    if (left) {
        for (int i = 0; i < left->count; i++) {
            invalidated_binding_add(dst, left->names[i]);
        }
    }
    if (right) {
        for (int i = 0; i < right->count; i++) {
            invalidated_binding_add(dst, right->names[i]);
        }
    }
}

static bool ast_reads_binding(AstNode* node, String* name);

static bool ast_reads_any_invalidated_binding(AstNode* node,
        InvalidatedBindingState* state, String** offending) {
    if (offending) *offending = NULL;
    if (!state) return false;
    for (int i = 0; i < state->count; i++) {
        if (ast_reads_binding(node, state->names[i])) {
            if (offending) *offending = state->names[i];
            return true;
        }
    }
    return false;
}

static bool ast_reads_binding(AstNode* node, String* name) {
    if (!node || !name) return false;
    if (node->node_type == AST_NODE_PRIMARY) {
        return ast_reads_binding(((AstPrimaryNode*)node)->expr, name);
    }
    switch (node->node_type) {
    case AST_NODE_IDENT:
        return same_name_string(((AstIdentNode*)node)->name, name);
    case AST_NODE_UNARY:
    case AST_NODE_SPREAD:
        return ast_reads_binding(((AstUnaryNode*)node)->operand, name);
    case AST_NODE_BINARY:
    case AST_NODE_PIPE: {
        AstBinaryNode* binary = (AstBinaryNode*)node;
        return ast_reads_binding(binary->left, name) ||
            ast_reads_binding(binary->right, name);
    }
    case AST_NODE_CALL_EXPR: {
        AstCallNode* call = (AstCallNode*)node;
        // A direct callee name is a declaration reference, not a value read.
        AstNode* callee = boundary_unwrap_primary(call->function);
        if (!callee || callee->node_type != AST_NODE_IDENT) {
            if (ast_reads_binding(call->function, name)) return true;
        }
        for (AstNode* arg = call->argument; arg; arg = arg->next) {
            if (ast_reads_binding(arg, name)) return true;
        }
        return false;
    }
    case AST_NODE_HANDLER_EXPR:
    case AST_NODE_HANDLER_STAM: {
        AstHandlerNode* handler = (AstHandlerNode*)node;
        return ast_reads_binding(handler->operand, name) ||
            ast_reads_binding(handler->body, name) ||
            ast_reads_binding(handler->value_body, name);
    }
    case AST_NODE_MEMBER_EXPR: {
        AstFieldNode* field = (AstFieldNode*)node;
        return ast_reads_binding(field->object, name);
    }
    case AST_NODE_INDEX_EXPR: {
        AstFieldNode* field = (AstFieldNode*)node;
        return ast_reads_binding(field->object, name) ||
            ast_reads_binding(field->field, name);
    }
    case AST_NODE_IF_EXPR: {
        AstIfNode* branch = (AstIfNode*)node;
        return ast_reads_binding(branch->cond, name) ||
            ast_reads_binding(branch->then, name) ||
            ast_reads_binding(branch->otherwise, name);
    }
    case AST_NODE_MATCH_EXPR: {
        AstMatchNode* match = (AstMatchNode*)node;
        if (ast_reads_binding(match->scrutinee, name)) return true;
        for (AstMatchArm* arm = match->first_arm; arm; arm = (AstMatchArm*)arm->next) {
            if (ast_reads_binding(arm->body, name)) return true;
        }
        return false;
    }
    case AST_NODE_CONTENT:
    case AST_NODE_LIST:
    case AST_NODE_ARRAY:
    case AST_NODE_MAP:
    case AST_NODE_ELEMENT: {
        for (AstNode* item = ((AstArrayNode*)node)->item; item; item = item->next) {
            if (ast_reads_binding(item, name)) return true;
        }
        return false;
    }
    case AST_NODE_ASSIGN:
    case AST_NODE_KEY_EXPR:
    case AST_NODE_NAMED_ARG:
        return ast_reads_binding(((AstNamedNode*)node)->as, name);
    case AST_NODE_ASSIGN_STAM:
        return ast_reads_binding(((AstAssignStamNode*)node)->value, name);
    case AST_NODE_INDEX_ASSIGN_STAM:
    case AST_NODE_MEMBER_ASSIGN_STAM: {
        AstCompoundAssignNode* assign = (AstCompoundAssignNode*)node;
        return ast_reads_binding(assign->object, name) ||
            ast_reads_binding(assign->key, name) ||
            ast_reads_binding(assign->value, name);
    }
    case AST_NODE_RETURN_STAM:
        return ast_reads_binding(((AstReturnNode*)node)->value, name);
    case AST_NODE_FUNC:
    case AST_NODE_FUNC_EXPR:
    case AST_NODE_PROC: {
        AstFuncNode* fn = (AstFuncNode*)node;
        for (FnCapture* capture = fn->captures; capture; capture = capture->next) {
            if (same_name_string(capture->lambda_name, name)) return true;
        }
        return false;
    }
    default:
        return false;
    }
}

static AstFuncNode* direct_user_callable(AstCallNode* call) {
    AstNode* callee = call ? boundary_unwrap_primary(call->function) : NULL;
    if (!callee || callee->node_type != AST_NODE_IDENT) return NULL;
    AstIdentNode* ident = (AstIdentNode*)callee;
    AstNode* target = ident->entry ? ident->entry->node : NULL;
    if (target && target->node_type == AST_NODE_ASSIGN) {
        target = boundary_unwrap_primary(((AstNamedNode*)target)->as);
    }
    return target && (target->node_type == AST_NODE_FUNC ||
        target->node_type == AST_NODE_FUNC_EXPR || target->node_type == AST_NODE_PROC)
        ? (AstFuncNode*)target : NULL;
}

static void report_invalidated_read(InvalidatedBindingState* state,
        AstNode* node, String* name) {
    if (!state || !state->tp || !node || !name) return;
    record_semantic_error_span(state->tp, node->source_span, ERR_INVALIDATED_BINDING,
        "binding '%.*s' may have been changed invisibly by a previous call; "
        "assign the returned value back to '%.*s' before reading it",
        (int)name->len, name->chars, (int)name->len, name->chars);
}

static void scan_invalidated_bindings(InvalidatedBindingState* state, AstNode* node);

static void scan_invalidated_call(InvalidatedBindingState* state,
        AstCallNode* call) {
    if (!state || !call) return;
    AstFuncNode* callee = direct_user_callable(call);
    // Evaluate arguments before applying the callee's captured writes.
    for (AstNode* arg = call->argument; arg; arg = arg->next) {
        scan_invalidated_bindings(state, arg);
    }
    if (!callee) return;
    for (FnCapture* capture = callee->captures; capture; capture = capture->next) {
        if (!capture->lambda_name || !capture->entry) continue;
        if (invalidated_binding_contains(state, capture->lambda_name)) {
            report_invalidated_read(state, (AstNode*)call, capture->lambda_name);
        }
        if (capture->is_mutable && capture->entry->is_mutable) {
            invalidated_binding_add(state, capture->lambda_name);
        }
    }
}

static void scan_invalidated_bindings(InvalidatedBindingState* state, AstNode* node) {
    if (!state) return;
    while (node) {
        switch (node->node_type) {
        case AST_NODE_CONTENT:
        case AST_NODE_LIST:
        case AST_NODE_ARRAY:
        case AST_NODE_MAP:
        case AST_NODE_ELEMENT:
            scan_invalidated_bindings(state, ((AstArrayNode*)node)->item);
            break;
        case AST_NODE_PRIMARY:
            scan_invalidated_bindings(state, ((AstPrimaryNode*)node)->expr);
            break;
        case AST_NODE_CALL_EXPR:
            scan_invalidated_call(state, (AstCallNode*)node);
            break;
        case AST_NODE_ASSIGN_STAM: {
            AstAssignStamNode* assign = (AstAssignStamNode*)node;
            scan_invalidated_bindings(state, assign->value);
            if (assign->target && invalidated_binding_contains(state, assign->target)) {
                if (ast_reads_binding(assign->value, assign->target)) {
                    report_invalidated_read(state, (AstNode*)assign, assign->target);
                } else {
                    // An explicit assignment is the only re-establishment edge.
                    invalidated_binding_remove(state, assign->target);
                }
            }
            break;
        }
        case AST_NODE_HANDLER_EXPR:
        case AST_NODE_HANDLER_STAM: {
            AstHandlerNode* handler = (AstHandlerNode*)node;
            scan_invalidated_bindings(state, handler->operand);
            InvalidatedBindingState operand_state = *state;
            InvalidatedBindingState body_state = *state;
            scan_invalidated_bindings(&body_state, handler->body);
            if (handler->value_body) {
                InvalidatedBindingState value_state = operand_state;
                scan_invalidated_bindings(&value_state, handler->value_body);
                invalidated_binding_union(state, &body_state, &value_state);
            } else {
                invalidated_binding_union(state, &operand_state, &body_state);
            }
            break;
        }
        case AST_NODE_BINARY:
        case AST_NODE_PIPE: {
            AstBinaryNode* binary = (AstBinaryNode*)node;
            scan_invalidated_bindings(state, binary->left);
            scan_invalidated_bindings(state, binary->right);
            break;
        }
        case AST_NODE_UNARY:
        case AST_NODE_SPREAD:
            scan_invalidated_bindings(state, ((AstUnaryNode*)node)->operand);
            break;
        case AST_NODE_IF_EXPR: {
            AstIfNode* branch = (AstIfNode*)node;
            scan_invalidated_bindings(state, branch->cond);
            InvalidatedBindingState then_state = *state;
            InvalidatedBindingState else_state = *state;
            scan_invalidated_bindings(&then_state, branch->then);
            scan_invalidated_bindings(&else_state, branch->otherwise);
            invalidated_binding_union(state, &then_state, &else_state);
            break;
        }
        case AST_NODE_MATCH_EXPR: {
            AstMatchNode* match = (AstMatchNode*)node;
            scan_invalidated_bindings(state, match->scrutinee);
            InvalidatedBindingState before_arms = *state;
            InvalidatedBindingState merged = before_arms;
            for (AstMatchArm* arm = match->first_arm; arm; arm = (AstMatchArm*)arm->next) {
                InvalidatedBindingState arm_state = before_arms;
                scan_invalidated_bindings(&arm_state, arm->pattern);
                scan_invalidated_bindings(&arm_state, arm->body);
                InvalidatedBindingState merged_before = merged;
                invalidated_binding_union(&merged, &merged_before, &arm_state);
            }
            *state = merged;
            break;
        }
        case AST_NODE_WHILE_STAM: {
            AstWhileNode* loop = (AstWhileNode*)node;
            scan_invalidated_bindings(state, loop->cond);
            InvalidatedBindingState before_body = *state;
            InvalidatedBindingState body_state = before_body;
            scan_invalidated_bindings(&body_state, loop->body);
            // A while body may execute zero times, so retain both the
            // pre-loop state and every invalidation reachable from one pass.
            invalidated_binding_union(state, &before_body, &body_state);
            break;
        }
        case AST_NODE_DO_WHILE_STAM: {
            AstWhileNode* loop = (AstWhileNode*)node;
            InvalidatedBindingState before_body = *state;
            scan_invalidated_bindings(state, loop->body);
            scan_invalidated_bindings(state, loop->cond);
            InvalidatedBindingState after_body = *state;
            invalidated_binding_union(state, &before_body, &after_body);
            break;
        }
        case AST_NODE_FOR_EXPR:
        case AST_NODE_FOR_STAM: {
            AstForNode* loop = (AstForNode*)node;
            for (AstNode* binding = loop->loop; binding; binding = binding->next) {
                AstLoopNode* loop_binding = (AstLoopNode*)binding;
                scan_invalidated_bindings(state, loop_binding->as);
                scan_invalidated_bindings(state, loop_binding->on);
            }
            scan_invalidated_bindings(state, loop->let_clause);
            scan_invalidated_bindings(state, loop->where);
            if (loop->group) {
                for (AstGroupKey* key = loop->group->keys; key; key = (AstGroupKey*)key->next) {
                    scan_invalidated_bindings(state, key->expr);
                }
            }
            scan_invalidated_bindings(state, loop->order);
            scan_invalidated_bindings(state, loop->limit);
            scan_invalidated_bindings(state, loop->offset);
            InvalidatedBindingState before_body = *state;
            InvalidatedBindingState body_state = before_body;
            scan_invalidated_bindings(&body_state, loop->then);
            invalidated_binding_union(state, &before_body, &body_state);
            break;
        }
        case AST_NODE_MEMBER_EXPR:
        case AST_NODE_INDEX_EXPR: {
            AstFieldNode* field = (AstFieldNode*)node;
            scan_invalidated_bindings(state, field->object);
            scan_invalidated_bindings(state, field->field);
            break;
        }
        case AST_NODE_ASSIGN:
        case AST_NODE_KEY_EXPR:
        case AST_NODE_NAMED_ARG:
            scan_invalidated_bindings(state, ((AstNamedNode*)node)->as);
            break;
        case AST_NODE_INDEX_ASSIGN_STAM:
        case AST_NODE_MEMBER_ASSIGN_STAM: {
            AstCompoundAssignNode* assign = (AstCompoundAssignNode*)node;
            scan_invalidated_bindings(state, assign->object);
            scan_invalidated_bindings(state, assign->key);
            scan_invalidated_bindings(state, assign->value);
            break;
        }
        case AST_NODE_RETURN_STAM:
            scan_invalidated_bindings(state, ((AstReturnNode*)node)->value);
            break;
        case AST_NODE_FUNC:
        case AST_NODE_FUNC_EXPR:
        case AST_NODE_PROC: {
            AstFuncNode* fn = (AstFuncNode*)node;
            for (FnCapture* capture = fn->captures; capture; capture = capture->next) {
                if (capture->lambda_name && invalidated_binding_contains(state,
                        capture->lambda_name)) {
                    report_invalidated_read(state, node, capture->lambda_name);
                }
            }
            break;
        }
        default: {
            String* offending = NULL;
            if (ast_reads_any_invalidated_binding(node, state, &offending)) {
                report_invalidated_read(state, node, offending);
            }
            break;
        }
        }
        node = node->next;
    }
}

static void validate_cross_frame_binding_reads(Transpiler* tp, AstFuncNode* fn) {
    if (!tp || !fn || !fn->body) return;
    InvalidatedBindingState state = {tp, {0}, 0};
    scan_invalidated_bindings(&state, fn->body);
}

static void validate_top_level_cross_frame_binding_reads(Transpiler* tp,
        AstNode* node) {
    if (!tp || !node) return;
    InvalidatedBindingState state = {tp, {0}, 0};
    scan_invalidated_bindings(&state, node);
}

typedef struct ProcReturnTypeScan {
    Transpiler* tp;
    Type* result;
} ProcReturnTypeScan;

static bool collect_procedural_return_type(AstNode* node, void* data) {
    ProcReturnTypeScan* scan = (ProcReturnTypeScan*)data;
    if (!node || node->node_type != AST_NODE_RETURN_STAM) return true;
    AstReturnNode* ret = (AstReturnNode*)node;
    Type* returned = ret->value && ret->value->type ? ret->value->type : &TYPE_NULL;
    scan->result = scan->result ? lambda_type_union_normalized(scan->tp->pool,
        scan->result, returned) : returned;
    return true;
}

static AstReturnNode* procedural_terminal_return(AstFuncNode* fn) {
    AstNode* tail = fn ? boundary_unwrap_primary(fn->body) : NULL;
    if (tail && tail->node_type == AST_NODE_CONTENT) {
        tail = ((AstListNode*)tail)->item;
        while (tail && tail->next) tail = tail->next;
        tail = boundary_unwrap_primary(tail);
    }
    return tail && tail->node_type == AST_NODE_RETURN_STAM
        ? (AstReturnNode*)tail : NULL;
}

static Type* infer_procedural_return_type(Transpiler* tp, AstFuncNode* fn) {
    if (!procedural_terminal_return(fn)) {
        // Only an explicit terminal return proves no fallthrough. Keep the
        // established body effect for expression-bodied procedures.
        return fn && fn->body && fn->body->type ? fn->body->type : &TYPE_ANY;
    }
    ProcReturnTypeScan scan = {tp, NULL};
    // A pn keeps an Item ABI, but a terminal return proves its caller-visible
    // value set. Keep that inference separate from its ABI return carrier.
    walk_lambda_ast(fn->body, collect_procedural_return_type, &scan, false);
    return scan.result ? scan.result : &TYPE_NULL;
}



// for both func expr and stam


// Build a view/edit template declaration






static bool handler_operand_is_proc(AstNode* operand) {
    // the postfix handler tier may wrap a procedure call in primary_expr;
    // classify the effective call so statement handlers keep their context.
    AstNode* effective_operand = boundary_unwrap_primary(operand);
    if (!effective_operand || effective_operand->node_type != AST_NODE_CALL_EXPR) return false;
    AstCallNode* call = (AstCallNode*)effective_operand;
    AstNode* callee = boundary_unwrap_primary(call->function);
    if (!callee) return false;
    if (callee->node_type == AST_NODE_SYS_FUNC) {
        SysFuncInfo* info = ((AstSysFuncNode*)callee)->fn_info;
        return info && info->is_proc;
    }
    return callee->type && callee->type->type_id == LMD_TYPE_FUNC &&
        ((TypeFunc*)callee->type)->is_proc;
}







AstNode* build_handler_from_parts(Transpiler* tp, SourceSpan span,
        AstNode* operand, AstNode* body, AstNode* value_body) {
    bool is_statement = handler_operand_is_proc(operand);
    AstHandlerNode* node = (AstHandlerNode*)alloc_ast_node_from_span(tp,
        is_statement ? AST_NODE_HANDLER_STAM : AST_NODE_HANDLER_EXPR, span,
        sizeof(AstHandlerNode));
    node->operand = operand;
    node->body = body;
    node->value_body = value_body;
    node->is_statement = is_statement;
    if (is_statement) {
        node->type = set_type_any(tp, ANY_STATEMENT);
    } else {
        Type* success = operand && operand->type
            ? lambda_type_remove_error(tp->pool, operand->type) : &TYPE_ANY;
        if (!success) success = set_type_any(tp, ANY_ERROR_RECOVERY);
        Type* body_type = body && body->type ? body->type : &TYPE_ANY;
        node->type = value_body && value_body->type
            ? lambda_type_union_normalized(tp->pool, body_type, value_body->type)
            : lambda_type_union_normalized(tp->pool, success, body_type);
    }
    return (AstNode*)node;
}



// --- external type-pattern tokens -------------------------------------------
// The scanner hands the whole type sub-language over as one token; the hand
// parser (parse_type_pattern.cpp) turns the token's source text into the
// retained AST-node/Type shapes.









// push a name with a qualified alias prefix (alias.name) for aliased imports
static void push_qualified_name(Transpiler* tp, AstNamedNode* node, AstImportNode* import, String* alias) {
    // create qualified name: alias.original_name
    size_t alias_len = alias->len;
    size_t name_len = node->name->len;
    size_t total_len = alias_len + 1 + name_len;  // alias.name
    char* buf = (char*)pool_alloc(tp->pool, total_len + 1);
    memcpy(buf, alias->chars, alias_len);
    buf[alias_len] = '.';
    memcpy(buf + alias_len + 1, node->name->chars, name_len);
    buf[total_len] = '\0';
    StrView qualified = {buf, total_len};
    String* qualified_name = name_pool_create_strview(tp->name_pool, qualified);

    log_debug("pushing qualified name %.*s", (int)qualified_name->len, qualified_name->chars);

    NameEntry* entry = (NameEntry*)pool_calloc(tp->pool, sizeof(NameEntry));
    entry->name = qualified_name;
    entry->node = (AstNode*)node;  entry->import = import;
    entry->scope = tp->current_scope;
    if (!tp->current_scope->first) { tp->current_scope->first = entry; }
    if (tp->current_scope->last) { tp->current_scope->last->next = entry; }
    tp->current_scope->last = entry;
}

static void register_imported_object_type(Transpiler* tp, AstObjectTypeNode* obj_node) {
    Type* node_type = obj_node->type;
    if (!node_type || node_type->type_id != LMD_TYPE_TYPE) return;
    TypeType* tt = (TypeType*)node_type;
    TypeObject* obj_type = (TypeObject*)tt->type;
    arraylist_append(tp->type_list, (void*)tt);
    obj_type->type_index = tp->type_list->length - 1;
    log_debug("registered imported object type '%.*s' at local index %d",
        (int)obj_node->name->len, obj_node->name->chars, obj_type->type_index);
}

void declare_module_import(Transpiler* tp, AstImportNode* import_node) {
    log_debug("declare_module_import");
    // import module
    if (!import_node->script) { log_error("Missing script");  return; }
    log_debug("script reference: %s", import_node->script->reference);
    // loop through the public functions in the module
    if (!import_node->script->ast_root) { log_error("Missing AST root");  return; }
    AstNode* node = import_node->script->ast_root;
    // Defensive check: validate node type instead of using assert
    if (node->node_type != AST_SCRIPT) {
        log_error("Error: declare_module_import expected AST_SCRIPT but got node_type %d", node->node_type);
        return;  // Defensive recovery - exit gracefully
    }
    bool has_alias = (import_node->alias != nullptr);
    node = ((AstScript*)node)->child;
    while (node) {
        if (node->node_type == AST_NODE_CONTENT) {
            node = ((AstListNode*)node)->item;  // drill down
            continue;
        }
        else if (node->node_type == AST_NODE_FUNC || node->node_type == AST_NODE_FUNC_EXPR || node->node_type == AST_NODE_PROC) {
            AstFuncNode* func_node = (AstFuncNode*)node;
            log_debug("got imported fn/pn: %.*s, is_public: %d", (int)func_node->name->len, func_node->name->chars,
                ((TypeFunc*)func_node->type)->is_public);
            if (((TypeFunc*)func_node->type)->is_public) {
                if (has_alias) {
                    push_qualified_name(tp, (AstNamedNode*)func_node, import_node, import_node->alias);
                } else {
                    push_name(tp, (AstNamedNode*)func_node, import_node);
                }
            }
        }
        else if (node->node_type == AST_NODE_PUB_STAM) {
            AstLetNode* pub_node = (AstLetNode*)node;
            AstNode* declare = pub_node->declare;
            while (declare) {
                if (declare->node_type == AST_NODE_OBJECT_TYPE) {
                    AstObjectTypeNode* obj_node = (AstObjectTypeNode*)declare;
                    // exported object type — register in importing script's type_list
                    register_imported_object_type(tp, obj_node);
                    if (has_alias) {
                        push_qualified_name(tp, (AstNamedNode*)obj_node, import_node, import_node->alias);
                    } else {
                        push_name(tp, (AstNamedNode*)obj_node, import_node);
                    }
                    log_debug("got pub type: %.*s", (int)obj_node->name->len, obj_node->name->chars);
                } else {
                    AstNamedNode* dec_node = (AstNamedNode*)declare;
                    if (has_alias) {
                        push_qualified_name(tp, (AstNamedNode*)dec_node, import_node, import_node->alias);
                    } else {
                        push_name(tp, (AstNamedNode*)dec_node, import_node);
                    }
                    log_debug("got pub var: %.*s", (int)dec_node->name->len, dec_node->name->chars);
                    // re-register type aliases in importing script's type_list
                    if (dec_node->type && dec_node->type->type_id == LMD_TYPE_TYPE) {
                        TypeType* tt = (TypeType*)dec_node->type;
                        Type* inner = tt->type;
                        if (inner && (inner->type_id == LMD_TYPE_MAP || inner->type_id == LMD_TYPE_OBJECT
                            || inner->type_id == LMD_TYPE_ARRAY)) {
                            arraylist_append(tp->type_list, (void*)tt);
                            ((TypeMap*)inner)->type_index = tp->type_list->length - 1;
                            log_debug("registered imported type alias '%.*s' at local index %d",
                                (int)dec_node->name->len, dec_node->name->chars, ((TypeMap*)inner)->type_index);
                        }
                    }
                }
                declare = declare->next;
            }
        }
        else if (node->node_type == AST_NODE_OBJECT_TYPE) {
            // standalone pub object type (e.g. pub type Counter { ... })
            AstObjectTypeNode* obj_node = (AstObjectTypeNode*)node;
            if (obj_node->is_public) {
                register_imported_object_type(tp, obj_node);
                if (has_alias) {
                    push_qualified_name(tp, (AstNamedNode*)obj_node, import_node, import_node->alias);
                } else {
                    push_name(tp, (AstNamedNode*)obj_node, import_node);
                }
                log_debug("got pub type: %.*s", (int)obj_node->name->len, obj_node->name->chars);
            }
        }
        node = node->next;
    }
}

#ifndef SIMPLE_SCHEMA_PARSER



#endif





// ==================== String/Symbol Pattern Building ====================

// Build pattern character class (d, w, s, a, ., ...)
// Build concat_type node — concatenation of type terms (for string/symbol patterns)
// e.g. \d[3] "-" \d[3] "-" \d[4]
// With recursive grammar: concat_type -> type_term type_term | concat_type type_term
// Build grouped_type node — parenthesized string type expr with optional ! prefix and occurrence
// e.g. ("a" \d[4])?, !("x" | "y"), ("a" to "z")+
// Build negation_type node — prefix ! operator (for string/symbol patterns)
// e.g. !\d
// Build string/symbol pattern definition
// Pattern bodies use the unified _type_expr reduction path.


static void walk_lambda_ast(AstNode* node, LambdaAstVisitor visitor, void* data,
                            bool descend_functions) {
    if (!node || !visitor(node, data)) return;

    switch (node->node_type) {
    case AST_SCRIPT: {
        for (AstNode* child = ((AstScript*)node)->child; child; child = child->next) {
            walk_lambda_ast(child, visitor, data, descend_functions);
        }
        break;
    }
    case AST_NODE_PRIMARY:
        walk_lambda_ast(((AstPrimaryNode*)node)->expr, visitor, data, descend_functions);
        break;
    case AST_NODE_UNARY:
    case AST_NODE_SPREAD:
        walk_lambda_ast(((AstUnaryNode*)node)->operand, visitor, data, descend_functions);
        break;
    case AST_NODE_BINARY:
    case AST_NODE_PIPE: {
        AstBinaryNode* binary = (AstBinaryNode*)node;
        walk_lambda_ast(binary->left, visitor, data, descend_functions);
        walk_lambda_ast(binary->right, visitor, data, descend_functions);
        break;
    }
    case AST_NODE_CALL_EXPR: {
        AstCallNode* call = (AstCallNode*)node;
        walk_lambda_ast(call->function, visitor, data, descend_functions);
        for (AstNode* arg = call->argument; arg; arg = arg->next) {
            walk_lambda_ast(arg, visitor, data, descend_functions);
        }
        break;
    }
    case AST_NODE_HANDLER_EXPR:
    case AST_NODE_HANDLER_STAM: {
        AstHandlerNode* handler = (AstHandlerNode*)node;
        walk_lambda_ast(handler->operand, visitor, data, descend_functions);
        walk_lambda_ast(handler->body, visitor, data, descend_functions);
        walk_lambda_ast(handler->value_body, visitor, data, descend_functions);
        break;
    }
    case AST_NODE_START:
        walk_lambda_ast((AstNode*)((AstStartNode*)node)->call, visitor, data, descend_functions);
        break;
    case AST_NODE_IF_EXPR: {
        AstIfNode* branch = (AstIfNode*)node;
        walk_lambda_ast(branch->cond, visitor, data, descend_functions);
        walk_lambda_ast(branch->then, visitor, data, descend_functions);
        walk_lambda_ast(branch->otherwise, visitor, data, descend_functions);
        break;
    }
    case AST_NODE_MATCH_EXPR: {
        AstMatchNode* match = (AstMatchNode*)node;
        walk_lambda_ast(match->scrutinee, visitor, data, descend_functions);
        for (AstMatchArm* arm = match->first_arm; arm; arm = (AstMatchArm*)arm->next) {
            walk_lambda_ast(arm->pattern, visitor, data, descend_functions);
            walk_lambda_ast(arm->body, visitor, data, descend_functions);
        }
        break;
    }
    case AST_NODE_CONTENT:
    case AST_NODE_LIST:
    case AST_NODE_ARRAY:
    case AST_NODE_MAP:
    case AST_NODE_ELEMENT: {
        AstNode* item = ((AstArrayNode*)node)->item;
        for (; item; item = item->next) {
            walk_lambda_ast(item, visitor, data, descend_functions);
        }
        break;
    }
    case AST_NODE_ASSIGN:
    case AST_NODE_KEY_EXPR:
    case AST_NODE_NAMED_ARG:
    case AST_NODE_PARAM:
        walk_lambda_ast(((AstNamedNode*)node)->as, visitor, data, descend_functions);
        break;
    case AST_NODE_ASSIGN_STAM:
        walk_lambda_ast(((AstAssignStamNode*)node)->value, visitor, data, descend_functions);
        break;
    case AST_NODE_INDEX_ASSIGN_STAM:
    case AST_NODE_MEMBER_ASSIGN_STAM: {
        AstCompoundAssignNode* assign = (AstCompoundAssignNode*)node;
        walk_lambda_ast(assign->object, visitor, data, descend_functions);
        walk_lambda_ast(assign->key, visitor, data, descend_functions);
        walk_lambda_ast(assign->value, visitor, data, descend_functions);
        break;
    }
    case AST_NODE_MEMBER_EXPR:
    case AST_NODE_INDEX_EXPR: {
        AstFieldNode* field = (AstFieldNode*)node;
        walk_lambda_ast(field->object, visitor, data, descend_functions);
        walk_lambda_ast(field->field, visitor, data, descend_functions);
        break;
    }
    case AST_NODE_WHILE_STAM: {
        AstWhileNode* loop = (AstWhileNode*)node;
        walk_lambda_ast(loop->cond, visitor, data, descend_functions);
        walk_lambda_ast(loop->body, visitor, data, descend_functions);
        break;
    }
    case AST_NODE_FOR_EXPR:
    case AST_NODE_FOR_STAM: {
        AstForNode* loop = (AstForNode*)node;
        for (AstNode* binding = loop->loop; binding; binding = binding->next) {
            AstLoopNode* loop_binding = (AstLoopNode*)binding;
            walk_lambda_ast(loop_binding->as, visitor, data, descend_functions);
            walk_lambda_ast(loop_binding->on, visitor, data, descend_functions);
        }
        for (AstNode* binding = loop->let_clause; binding; binding = binding->next) {
            walk_lambda_ast(binding, visitor, data, descend_functions);
        }
        walk_lambda_ast(loop->where, visitor, data, descend_functions);
        if (loop->group) {
            for (AstGroupKey* key = loop->group->keys; key; key = (AstGroupKey*)key->next) {
                walk_lambda_ast(key->expr, visitor, data, descend_functions);
            }
        }
        for (AstNode* order = loop->order; order; order = order->next) {
            walk_lambda_ast(((AstOrderSpec*)order)->expr, visitor, data, descend_functions);
        }
        walk_lambda_ast(loop->limit, visitor, data, descend_functions);
        walk_lambda_ast(loop->offset, visitor, data, descend_functions);
        walk_lambda_ast(loop->then, visitor, data, descend_functions);
        break;
    }
    case AST_NODE_RETURN_STAM:
        walk_lambda_ast(((AstReturnNode*)node)->value, visitor, data, descend_functions);
        break;
    case AST_NODE_RAISE_STAM:
    case AST_NODE_RAISE_EXPR:
        walk_lambda_ast(((AstRaiseNode*)node)->value, visitor, data, descend_functions);
        break;
    case AST_NODE_LET_STAM:
    case AST_NODE_VAR_STAM:
    case AST_NODE_PUB_STAM: {
        for (AstNode* decl = ((AstLetNode*)node)->declare; decl; decl = decl->next) {
            walk_lambda_ast(decl, visitor, data, descend_functions);
        }
        break;
    }
    case AST_NODE_FUNC:
    case AST_NODE_FUNC_EXPR:
    case AST_NODE_PROC:
        if (descend_functions) {
            walk_lambda_ast(((AstFuncNode*)node)->body, visitor, data, descend_functions);
        }
        break;
    case AST_NODE_OBJECT_TYPE: {
        AstObjectTypeNode* object = (AstObjectTypeNode*)node;
        for (AstNode* method = object->methods; method; method = method->next) {
            walk_lambda_ast(method, visitor, data, descend_functions);
        }
        break;
    }
    default:
        break;
    }
}

static bool shift_source_span(AstNode* node, void* data) {
    uint32_t offset = *(uint32_t*)data;
    node->source_span.start_byte += offset;
    node->source_span.end_byte += offset;
    return true;
}

void lambda_ast_shift_source_spans(AstNode* root, uint32_t byte_offset) {
    if (!root || byte_offset == 0) return;
    walk_lambda_ast(root, shift_source_span, &byte_offset, true);
}

static AstNode* unwrap_primary_ast(AstNode* node) {
    while (node && node->node_type == AST_NODE_PRIMARY) {
        node = ((AstPrimaryNode*)node)->expr;
    }
    return node;
}

static AstFuncNode* direct_pn_callee(AstCallNode* call) {
    if (!call) return NULL;
    AstNode* function = unwrap_primary_ast(call->function);
    if (!function || function->node_type != AST_NODE_IDENT) return NULL;
    NameEntry* entry = ((AstIdentNode*)function)->entry;
    if (!entry || !entry->node || entry->node->node_type != AST_NODE_PROC) return NULL;
    return (AstFuncNode*)entry->node;
}

static bool collect_concurrency_function(AstNode* node, void* data) {
    if (node->node_type == AST_NODE_FUNC || node->node_type == AST_NODE_FUNC_EXPR ||
            node->node_type == AST_NODE_PROC) {
        arraylist_append((ArrayList*)data, node);
    }
    return true;
}

typedef struct MayAwaitScan {
    bool found;
    bool indirect;
    const char* cause;
} MayAwaitScan;

static bool call_may_await(AstCallNode* call, bool* indirect, const char** cause) {
    AstNode* function = call ? unwrap_primary_ast(call->function) : NULL;
    if (function && function->node_type == AST_NODE_SYS_FUNC) {
        SysFuncInfo* info = ((AstSysFuncNode*)function)->fn_info;
        if (info && info->is_async) {
            if (cause) *cause = info->name;
            return true;
        }
        return false;
    }
    if (!function || !function->type || function->type->type_id != LMD_TYPE_FUNC ||
            !((TypeFunc*)function->type)->is_proc) return false;
    AstFuncNode* callee = direct_pn_callee(call);
    if (!callee) {
        if (indirect) *indirect = true;
        if (cause) *cause = "indirect pn call";
        return true;
    }
    if (callee->analysis && callee->analysis->may_await) {
        if (cause) *cause = callee->name ? callee->name->chars : "anonymous pn";
        return true;
    }
    return false;
}

static bool scan_may_await_node(AstNode* node, void* data) {
    MayAwaitScan* scan = (MayAwaitScan*)data;
    if (scan->found) return false;
    if (node->node_type == AST_NODE_START) {
        AstStartNode* start = (AstStartNode*)node;
        // Creating a child is synchronous, but the owning block's implicit
        // structured join can suspend unless ownership escapes by return.
        if (!start->escapes) {
            scan->found = true;
            scan->cause = "implicit scoped-task join";
        }
        return false;
    }
    if (node->node_type == AST_NODE_FUNC || node->node_type == AST_NODE_FUNC_EXPR ||
            node->node_type == AST_NODE_PROC) return false;
    if (node->node_type != AST_NODE_CALL_EXPR) return true;

    bool indirect = false;
    const char* cause = NULL;
    if (call_may_await((AstCallNode*)node, &indirect, &cause)) {
        scan->found = true;
        scan->indirect = indirect;
        scan->cause = cause;
    }
    return !scan->found;
}

typedef struct TaskContextScan {
    bool found;
} TaskContextScan;

static bool scan_task_context_node(AstNode* node, void* data) {
    TaskContextScan* scan = (TaskContextScan*)data;
    if (scan->found) return false;
    if (node->node_type == AST_NODE_START) {
        scan->found = true;
        return false;
    }
    if (node->node_type == AST_NODE_FUNC || node->node_type == AST_NODE_FUNC_EXPR ||
            node->node_type == AST_NODE_PROC) return false;
    if (node->node_type != AST_NODE_CALL_EXPR) return true;
    AstCallNode* call = (AstCallNode*)node;
    if (call_may_await(call, NULL, NULL)) {
        scan->found = true;
        return false;
    }
    AstFuncNode* callee = direct_pn_callee(call);
    if (callee && callee->analysis && callee->analysis->needs_task_context) {
        scan->found = true;
        return false;
    }
    return true;
}

typedef struct AwaitPointScan {
    int count;
} AwaitPointScan;

typedef struct HandlerStateScan {
    int base_state;
    int count;
} HandlerStateScan;

static bool assign_async_handler_state(AstNode* node, void* data) {
    if (node->node_type != AST_NODE_HANDLER_STAM) return true;
    AstHandlerNode* handler = (AstHandlerNode*)node;
    HandlerStateScan* scan = (HandlerStateScan*)data;
    if (handler->is_statement && handler_operand_is_proc(handler->operand)) {
        handler->async_fault_state = scan->base_state + ++scan->count;
    }
    return true;
}

static bool count_await_point_node(AstNode* node, void* data) {
    if (node->node_type == AST_NODE_START) return false;
    if (node->node_type == AST_NODE_FUNC || node->node_type == AST_NODE_FUNC_EXPR ||
            node->node_type == AST_NODE_PROC) return false;
    AwaitPointScan* scan = (AwaitPointScan*)data;
    if (node->node_type == AST_NODE_CALL_EXPR) {
        AstCallNode* call = (AstCallNode*)node;
        // Lowering can insert an error-unwind continuation for a dynamic call
        // result and for each dynamic argument checked against a parameter
        // contract. Those continuations are not represented as AST exit nodes,
        // but must have dispatcher labels before MIR lowering begins.
        scan->count++;
        for (AstNode* arg = call->argument; arg; arg = arg->next) {
            scan->count++;
        }
        if (call_may_await(call, NULL, NULL)) {
            scan->count++;
        }
        if (call->propagate) {
            scan->count++;
        }
    }
    if (node->node_type == AST_NODE_LET_STAM ||
            node->node_type == AST_NODE_VAR_STAM ||
            node->node_type == AST_NODE_PUB_STAM) {
        // A declared binding can lower to a runtime type check whose failure
        // leaves the current task scope. Untyped bindings reserve an unused
        // label, keeping this planner conservative and syntax-directed.
        scan->count++;
    }
    if (node->node_type == AST_NODE_RETURN_STAM ||
            node->node_type == AST_NODE_RAISE_STAM ||
            node->node_type == AST_NODE_ASSIGN_STAM ||
            node->node_type == AST_NODE_INDEX_ASSIGN_STAM ||
            node->node_type == AST_NODE_MEMBER_ASSIGN_STAM ||
            node->node_type == AST_NODE_PIPE_FILE_STAM) {
        // These syntax edges can leave a lexical block before its tail, so
        // each owns an unwind continuation in the resumable transform.
        scan->count++;
    }
    if (node->node_type == AST_NODE_CONTENT) {
        // Every lexical content block in a resumable pn has a synthetic scope
        // leave. Empty scopes return immediately; owning scopes may park.
        scan->count++;
    }
    return true;
}

typedef struct ConcurrencyValidation {
    Transpiler* tp;
} ConcurrencyValidation;

static AstStartNode* returned_start_node(AstNode* value) {
    value = unwrap_primary_ast(value);
    if (!value) return NULL;
    if (value->node_type == AST_NODE_START) return (AstStartNode*)value;
    if (value->node_type != AST_NODE_IDENT) return NULL;
    NameEntry* entry = ((AstIdentNode*)value)->entry;
    if (!entry || !entry->node || entry->node->node_type != AST_NODE_ASSIGN) return NULL;
    AstNode* assigned = unwrap_primary_ast(((AstNamedNode*)entry->node)->as);
    return assigned && assigned->node_type == AST_NODE_START ? (AstStartNode*)assigned : NULL;
}

static bool validate_concurrency_node(AstNode* node, void* data) {
    ConcurrencyValidation* validation = (ConcurrencyValidation*)data;
    if (node->node_type == AST_NODE_RETURN_STAM) {
        AstStartNode* start = returned_start_node(((AstReturnNode*)node)->value);
        if (start) start->escapes = true;
        return true;
    }
    if (node->node_type != AST_NODE_START) return true;

    AstStartNode* start = (AstStartNode*)node;
    AstFuncNode* callee = direct_pn_callee(start->call);
    if (!callee) return false;
    for (FnCapture* capture = callee->captures; capture; capture = capture->next) {
        if (capture->entry && capture->entry->is_mutable) {
            // A spawned task may resume after its lexical parent moves on, so
            // borrowing an outer var by reference would create shared mutation.
            record_semantic_error_span(validation->tp, start->source_span, ERR_INVALID_EXPR_CONTEXT,
                "`start` cannot capture mutable var '%.*s'; copy it to a `let` value or use message passing",
                (int)capture->lambda_name->len, capture->lambda_name->chars);
        }
    }
    return false;
}

typedef struct HandlerAwaitValidation {
    Transpiler* tp;
} HandlerAwaitValidation;

static bool validate_handler_await_node(AstNode* node, void* data) {
    if (node->node_type != AST_NODE_HANDLER_EXPR &&
            node->node_type != AST_NODE_HANDLER_STAM) return true;
    HandlerAwaitValidation* validation = (HandlerAwaitValidation*)data;
    AstHandlerNode* handler = (AstHandlerNode*)node;
    MayAwaitScan scan = {};
    walk_lambda_ast(handler->operand, scan_may_await_node, &scan, false);
    bool proc_statement = handler->is_statement &&
        handler_operand_is_proc(handler->operand);
    if (scan.found && !proc_statement) {
        // A statement pn handler consumes the call's explicit Item completion
        // after the async resume point; only value handlers still require a
        // live native result context that cannot span a scheduler yield.
        record_semantic_error_span(validation->tp, node->source_span, ERR_INVALID_EXPR_CONTEXT,
            "error handler operand may suspend (%s); await before applying `^ { ... }`",
            scan.cause ? scan.cause : "possible await");
    }
    return true;
}

static void analyze_lambda_concurrency(Transpiler* tp, AstScript* script) {
    ArrayList* functions = arraylist_new(16);
    if (!functions) return;
    walk_lambda_ast((AstNode*)script, collect_concurrency_function, functions, true);

    for (int i = 0; i < functions->length; i++) {
        AstFuncNode* fn = (AstFuncNode*)functions->data[i];
        if (!fn->analysis) fn->analysis = (FnAnalysis*)pool_calloc(tp->pool, sizeof(FnAnalysis));
        fn->analysis->may_await = false;
        fn->analysis->needs_task_context = false;
        fn->analysis->has_indirect_pn_call = false;
        fn->analysis->await_point_count = 0;
        fn->analysis->async_fault_handler_count = 0;
        fn->analysis->may_await_cause = NULL;
    }

    // Escape marking must precede may-await closure inference because a
    // returned handle suppresses the birth block's implicit join.
    ConcurrencyValidation validation = {.tp = tp};
    walk_lambda_ast((AstNode*)script, validate_concurrency_node, &validation, true);

    // Re-scan to a fixed point because forward calls can make callers suspend
    // only after the callee's bit becomes known.
    bool changed = true;
    while (changed) {
        changed = false;
        for (int i = 0; i < functions->length; i++) {
            AstFuncNode* fn = (AstFuncNode*)functions->data[i];
            if (fn->node_type != AST_NODE_PROC || fn->analysis->may_await) continue;
            MayAwaitScan scan = {};
            walk_lambda_ast(fn->body, scan_may_await_node, &scan, false);
            if (scan.found) {
                fn->analysis->may_await = true;
                fn->analysis->has_indirect_pn_call = scan.indirect;
                fn->analysis->may_await_cause = scan.cause;
                changed = true;
                if (strcmp(scan.cause, "implicit scoped-task join") == 0) {
                    log_debug("concurrency explain: pn %.*s suspends because it owns an %s",
                        fn->name ? (int)fn->name->len : 6,
                        fn->name ? fn->name->chars : "<anon>", scan.cause);
                } else {
                    log_debug("concurrency explain: pn %.*s suspends because it calls %s",
                        fn->name ? (int)fn->name->len : 6,
                        fn->name ? fn->name->chars : "<anon>", scan.cause);
                }
            }
        }
    }

    HandlerAwaitValidation handler_validation = {.tp = tp};
    walk_lambda_ast((AstNode*)script, validate_handler_await_node,
        &handler_validation, true);

    // A procedure that starts a child but never parks still needs a scheduler
    // task so `self()` and scoped ownership have a concrete parent. Propagate
    // that requirement independently from the may-await closure.
    changed = true;
    while (changed) {
        changed = false;
        for (int i = 0; i < functions->length; i++) {
            AstFuncNode* fn = (AstFuncNode*)functions->data[i];
            if (fn->node_type != AST_NODE_PROC || fn->analysis->needs_task_context) continue;
            TaskContextScan scan = {};
            walk_lambda_ast(fn->body, scan_task_context_node, &scan, false);
            if (scan.found) {
                fn->analysis->needs_task_context = true;
                changed = true;
            }
        }
    }

    for (int i = 0; i < functions->length; i++) {
        AstFuncNode* fn = (AstFuncNode*)functions->data[i];
        if (fn->node_type != AST_NODE_PROC || !fn->analysis->may_await) continue;
        AwaitPointScan scan = {};
        walk_lambda_ast(fn->body, count_await_point_node, &scan, false);
        fn->analysis->await_point_count = scan.count;
        HandlerStateScan handler_scan = {scan.count, 0};
        walk_lambda_ast(fn->body, assign_async_handler_state, &handler_scan, false);
        fn->analysis->async_fault_handler_count = handler_scan.count;
    }

    arraylist_free(functions);
}

static void finalize_lambda_script_ast(Transpiler* tp, AstScript* script) {
    if (!tp || !script || tp->error_count != 0) return;
    for (AstNode* item = script->child; item; item = item->next) {
        validate_top_level_enforcing_calls(tp, item);
        validate_top_level_cross_frame_binding_reads(tp, item);
    }
    // both parser front ends share this final pass. Direct AST construction
    // used to skip it, leaving suspend-capable procedures without their
    // resumable task state machine (D6.1.2).
    if (tp->error_count == 0) analyze_lambda_concurrency(tp, script);
}





// --- direct recursive-descent AST sink ------------------------------------
// The sink deliberately lives beside the shared constructors so reductions
// share allocation, literal, name, and type ownership. It starts with the
// reductions whose child contract is already complete; complex declaration
// reductions are added only after the parser publishes their binding metadata.

struct LambdaDirectAstSink {
    Transpiler* tp;
    AstScript* root;
    bool failed;
    bool handler_context[64];
    uint32_t handler_context_depth;
    bool that_context[64];
    uint32_t that_context_depth;
    NameScope* loop_scopes[64];
    AstForNode* for_nodes[64];
    uint32_t loop_scope_depth;
    NameScope* group_scopes[64];
    uint32_t group_scope_depth;
    NameScope* completed_group_scope;
    NameScope* branch_scopes[64];
    uint32_t branch_scope_depth;
    AstFuncNode* function_nodes[64];
    NameScope* function_scopes[64];
    uint32_t function_depth;
    AstViewNode* view_nodes[64];
    NameScope* view_scopes[64];
    AstStateEntry* view_state_tails[64];
    AstEventHandler* view_handler_tails[64];
    uint32_t view_depth;
    AstEventHandler* event_handlers[64];
    NameScope* event_handler_scopes[64];
    uint32_t event_handler_depth;
    uint32_t type_object_depth;
    AstObjectTypeNode* object_node;
    TypeObject* object_type;
    NameScope* object_scope;
    AstNode* object_field_tail;
    ShapeEntry* object_shape_tail;
    AstNode* object_method_tail;
    int object_byte_offset;
    AstObjectTypeNode* completed_object;
    AstNamedNode* pending_type_alias;
};

static LambdaParseValue direct_ast_value(AstNode* node) {
    return (LambdaParseValue)(uintptr_t)node;
}

static AstNode* direct_ast_node(LambdaParseValue value) {
    return (AstNode*)(uintptr_t)value;
}

static StrView direct_token_text(Transpiler* tp, LambdaToken token) {
    return source_span_text(tp, token.span);
}

static StrView direct_key_text(Transpiler* tp, LambdaToken token) {
    StrView name = direct_token_text(tp, token);
    // symbol keys retain their quotes in source, but map and element shapes
    // must store the bare field name so quoted attributes use normal lookup.
    if (token.kind == LAMBDA_TOK_SYMBOL && name.length >= 2 &&
            name.str[0] == '\'' && name.str[name.length - 1] == '\'') {
        name.str++;
        name.length -= 2;
    }
    return name;
}

static void direct_object_copy_base(LambdaDirectAstSink* sink,
        StrView base_name) {
    if (!base_name.length) return;
    Transpiler* tp = sink->tp;
    TypeObject* base = lookup_object_type_for_tag(tp, base_name);
    if (!base) {
        record_semantic_error_span(tp, sink->object_node->source_span,
            ERR_SEMANTIC_ERROR, "unknown object base type '%.*s'",
            (int)base_name.length, base_name.str);
        return;
    }
    sink->object_type->base = base;
    for (ShapeEntry* parent = base->shape; parent; parent = parent->next) {
        ShapeEntry* entry = (ShapeEntry*)pool_calloc(tp->pool, sizeof(ShapeEntry));
        entry->name = parent->name;
        entry->type = parent->type;
        entry->byte_offset = sink->object_byte_offset;
        if (!sink->object_type->shape) sink->object_type->shape = entry;
        else sink->object_shape_tail->next = entry;
        sink->object_shape_tail = entry;
        sink->object_type->length++;
        sink->object_byte_offset += sizeof(void*);

        AstNamedNode* field = (AstNamedNode*)pool_calloc(tp->pool,
            sizeof(AstNamedNode));
        field->node_type = AST_NODE_KEY_EXPR;
        field->name = name_pool_create_len(tp->name_pool,
            parent->name->str, parent->name->length);
        field->type = parent->type;
        lambda_ast_register_name(tp, field);
    }
}

static void direct_object_begin(LambdaDirectAstSink* sink,
        const LambdaParseReduction* reduction) {
    Transpiler* tp = sink->tp;
    StrView name = direct_token_text(tp, reduction->secondary_token);
    AstObjectTypeNode* object = (AstObjectTypeNode*)alloc_ast_node_from_span(tp,
        AST_NODE_OBJECT_TYPE, reduction->span, sizeof(AstObjectTypeNode));
    TypeObject* object_type = (TypeObject*)pool_calloc(tp->pool,
        sizeof(TypeObject));
    object_type->type_id = LMD_TYPE_OBJECT;
    TypeType* type_value = (TypeType*)alloc_type(tp->pool, LMD_TYPE_TYPE,
        sizeof(TypeType));
    type_value->type = (Type*)object_type;
    object->type = (Type*)type_value;
    object->name = name_pool_create_strview(tp->name_pool, name);
    object->is_public = (reduction->flags & LAMBDA_REDUCTION_FLAG_PUBLIC) != 0;
    object->local_type_index = -1;
    object_type->type_name = (StrView){object->name->chars, object->name->len};
    object_type->struct_name = object->name->chars;
    object_type->is_trusted_contract = true;

    sink->object_node = object;
    sink->object_type = object_type;
    sink->object_field_tail = NULL;
    sink->object_shape_tail = NULL;
    sink->object_method_tail = NULL;
    sink->object_byte_offset = 0;
    sink->completed_object = NULL;
    lambda_ast_register_name(tp, (AstNamedNode*)object);
    sink->object_scope = lambda_ast_enter_scope(tp, false);
    direct_object_copy_base(sink, direct_token_text(tp, reduction->detail_token));
}

static void direct_object_add_field(LambdaDirectAstSink* sink,
        const LambdaParseReduction* reduction) {
    if (!sink->object_node || !reduction->child_count) return;
    Transpiler* tp = sink->tp;
    AstNode* type_node = direct_ast_node(reduction->children[0]);
    AstNode* default_value = reduction->child_count > 1
        ? direct_ast_node(reduction->children[1]) : NULL;
    AstNamedNode* field = (AstNamedNode*)alloc_ast_node_from_span(tp,
        AST_NODE_KEY_EXPR, reduction->span, sizeof(AstNamedNode));
    field->name = name_pool_create_strview(tp->name_pool,
        direct_token_text(tp, reduction->detail_token));
    field->as = type_node;
    field->type = type_node && type_node->type ? type_node->type : &TYPE_ANY;
    if (!sink->object_node->item) sink->object_node->item = (AstNode*)field;
    else sink->object_field_tail->next = (AstNode*)field;
    sink->object_field_tail = (AstNode*)field;

    Type* field_type = unwrap_simple_type_type(field->type);
    ShapeEntry* shape = (ShapeEntry*)pool_calloc(tp->pool, sizeof(ShapeEntry));
    StrView* field_name = (StrView*)pool_calloc(tp->pool, sizeof(StrView));
    field_name->str = field->name->chars;
    field_name->length = field->name->len;
    shape->name = field_name;
    shape->type = field_type;
    shape->default_value = default_value;
    shape->byte_offset = sink->object_byte_offset;
    if (!sink->object_type->shape) sink->object_type->shape = shape;
    else sink->object_shape_tail->next = shape;
    sink->object_shape_tail = shape;
    sink->object_type->length++;
    sink->object_byte_offset += sizeof(void*);

    // Methods and constraints resolve bare field names in the object scope;
    // keep that scope entry separate from the annotation AST node.
    AstNamedNode* field_ref = (AstNamedNode*)pool_calloc(tp->pool,
        sizeof(AstNamedNode));
    field_ref->node_type = AST_NODE_KEY_EXPR;
    field_ref->name = field->name;
    field_ref->type = field_type;
    lambda_ast_register_name(tp, field_ref);
}

static AstNode* direct_object_content_from_parts(Transpiler* tp,
        SourceSpan span, AstNode* type_node) {
    AstListNode* content = (AstListNode*)alloc_ast_node_from_span(tp,
        AST_NODE_CONTENT_TYPE, span, sizeof(AstListNode));
    content->list_type = (TypeList*)alloc_type(tp->pool, LMD_TYPE_ARRAY,
        sizeof(TypeList));
    content->item = type_node;
    content->list_type->length = type_node ? 1 : 0;
    content->type = (Type*)content->list_type;
    return (AstNode*)content;
}

static void direct_object_add_method(LambdaDirectAstSink* sink, AstNode* method) {
    if (!sink->object_node || !method) return;
    AstFuncNode* fn = (AstFuncNode*)method;
    if (!sink->object_node->methods) sink->object_node->methods = method;
    else sink->object_method_tail->next = method;
    sink->object_method_tail = method;
    TypeMethod* tm = (TypeMethod*)pool_calloc(sink->tp->pool, sizeof(TypeMethod));
    StrView* method_name = (StrView*)pool_calloc(sink->tp->pool, sizeof(StrView));
    method_name->str = fn->name->chars;
    method_name->length = fn->name->len;
    tm->name = method_name;
    tm->fn_type = (TypeFunc*)fn->type;
    // T0 binds methods from the AST definition; without the direct-builder
    // identity fields it sees a name-only method and evaluates the member as
    // a non-callable value instead of entering the interpreted body.
    tm->ast_def = fn;
    tm->ast_module = sink->tp->script_owner;
    tm->arity = 0;
    for (AstNamedNode* param = fn->param; param;
            param = (AstNamedNode*)((AstNode*)param)->next) {
        tm->arity++;
    }
    tm->is_proc = method->node_type == AST_NODE_PROC;
    if (!sink->object_type->methods) sink->object_type->methods = tm;
    else sink->object_type->methods_last->next = tm;
    sink->object_type->methods_last = tm;
    sink->object_type->method_count++;
}

static void direct_object_end(LambdaDirectAstSink* sink) {
    if (!sink->object_node) return;
    sink->object_type->byte_size = sink->object_byte_offset;
    sink->object_type->last = sink->object_shape_tail;
    arraylist_append(sink->tp->type_list, sink->object_node->type);
    sink->object_type->type_index = sink->tp->type_list->length - 1;
    lambda_ast_leave_scope(sink->tp, sink->object_scope);
    sink->completed_object = sink->object_node;
    sink->object_node = NULL;
    sink->object_type = NULL;
    sink->object_scope = NULL;
    sink->object_field_tail = NULL;
    sink->object_shape_tail = NULL;
    sink->object_method_tail = NULL;
    sink->object_byte_offset = 0;
}

static bool direct_function_name_token(LambdaTokenKind kind) {
    // Keep this small lexical predicate aligned with the parser's context-
    // sensitive `token_is_key`: declarations may use soft keywords as names.
    return kind == LAMBDA_TOK_IDENTIFIER || kind == LAMBDA_TOK_BASE_TYPE ||
        kind == LAMBDA_TOK_SYMBOL || kind == LAMBDA_TOK_TYPE ||
        (kind >= LAMBDA_TOK_LET && kind <= LAMBDA_TOK_GT_WORD) ||
        kind == LAMBDA_TOK_STAR;
}

static void direct_predeclare_top_level_functions(Transpiler* tp,
        const char* source, size_t length) {
    LambdaLexer lexer;
    lambda_lexer_init(&lexer, source, length);
    LambdaToken token = lambda_lexer_next(&lexer);
    uint32_t delimiter_depth = 0;
    bool public_pending = false;
    while (token.kind != LAMBDA_TOK_EOF && token.kind != LAMBDA_TOK_ERROR) {
        if (delimiter_depth == 0 && token.kind == LAMBDA_TOK_PUB) {
            public_pending = true;
            token = lambda_lexer_next(&lexer);
            continue;
        }
        if (delimiter_depth == 0 &&
                (token.kind == LAMBDA_TOK_FN || token.kind == LAMBDA_TOK_PN)) {
            bool is_proc = token.kind == LAMBDA_TOK_PN;
            LambdaToken name = lambda_lexer_next(&lexer);
            if (direct_function_name_token(name.kind)) {
                StrView name_view = {source + name.span.start_byte,
                    name.span.end_byte - name.span.start_byte};
                String* pooled_name = name_pool_create_strview(tp->name_pool,
                    name_view);
                NameEntry* existing = lookup_name_in_current_scope(tp,
                    pooled_name);
                if (!existing || !existing->node ||
                        (existing->node->source_span.start_byte !=
                            token.span.start_byte)) {
                    AstFuncNode* fn = build_function_placeholder_from_parts(tp,
                        (SourceSpan){token.span.start_byte, name.span.end_byte},
                        name_view, is_proc);
                    ((TypeFunc*)fn->type)->is_public = public_pending;
                    lambda_ast_register_name(tp, (AstNamedNode*)fn);
                } else if (existing->node->node_type == AST_NODE_FUNC ||
                        existing->node->node_type == AST_NODE_PROC) {
                    ((TypeFunc*)existing->node->type)->is_public = public_pending;
                }
            }
            public_pending = false;
            token = lambda_lexer_next(&lexer);
            continue;
        }
        if (token.kind == LAMBDA_TOK_NEWLINE ||
                token.kind == LAMBDA_TOK_SEMICOLON) {
            public_pending = false;
        }
        if (token.kind == LAMBDA_TOK_LPAREN ||
                token.kind == LAMBDA_TOK_LBRACKET ||
                token.kind == LAMBDA_TOK_LBRACE) {
            delimiter_depth++;
        } else if (token.kind == LAMBDA_TOK_RPAREN ||
                token.kind == LAMBDA_TOK_RBRACKET ||
                token.kind == LAMBDA_TOK_RBRACE) {
            if (delimiter_depth) delimiter_depth--;
        }
        token = lambda_lexer_next(&lexer);
    }
}

static bool direct_literal_kind(LambdaTokenKind token, LambdaAstLiteralKind* kind) {
    if (!kind) return false;
    switch (token) {
    case LAMBDA_TOK_STRING: *kind = LAMBDA_AST_LITERAL_STRING; return true;
    case LAMBDA_TOK_SYMBOL: *kind = LAMBDA_AST_LITERAL_SYMBOL; return true;
    case LAMBDA_TOK_BINARY: *kind = LAMBDA_AST_LITERAL_BINARY; return true;
    case LAMBDA_TOK_DATETIME: *kind = LAMBDA_AST_LITERAL_DATETIME; return true;
    case LAMBDA_TOK_NAMED_VALUE: *kind = LAMBDA_AST_LITERAL_NAMED_VALUE; return true;
    case LAMBDA_TOK_INTEGER: *kind = LAMBDA_AST_LITERAL_INTEGER; return true;
    case LAMBDA_TOK_FLOAT: *kind = LAMBDA_AST_LITERAL_FLOAT; return true;
    case LAMBDA_TOK_DECIMAL: *kind = LAMBDA_AST_LITERAL_DECIMAL; return true;
    case LAMBDA_TOK_SIZED_INTEGER: *kind = LAMBDA_AST_LITERAL_SIZED_INTEGER; return true;
    case LAMBDA_TOK_SIZED_FLOAT: *kind = LAMBDA_AST_LITERAL_SIZED_FLOAT; return true;
    case LAMBDA_TOK_IMAGINARY: *kind = LAMBDA_AST_LITERAL_IMAGINARY; return true;
    default: return false;
    }
}

static AstNode* direct_member_field(Transpiler* tp, LambdaToken token) {
    LambdaAstLiteralKind literal_kind;
    if (direct_literal_kind(token.kind, &literal_kind)) {
        return build_literal_from_span(tp, token.span, literal_kind);
    }
    AstIdentNode* field = (AstIdentNode*)alloc_ast_node_from_span(tp,
        AST_NODE_IDENT, token.span, sizeof(AstIdentNode));
    field->name = name_pool_create_strview(tp->name_pool,
        source_span_text(tp, token.span));
    // A member key is a spelling, not a lexical variable reference. In
    // particular, built-ins such as `name` must not turn `record.name` into a
    // system-function node (D4.6.1v2).
    field->type = set_type_any(tp, ANY_DYNAMIC_NAME);
    return (AstNode*)field;
}

static AstNode* direct_base_type_from_span(Transpiler* tp, SourceSpan span) {
    AstTypeNode* node = (AstTypeNode*)alloc_ast_node_from_span(tp, AST_NODE_TYPE,
        span, sizeof(AstTypeNode));
    StrView name = source_span_text(tp, span);
    node->type = lookup_base_type_name(tp, name);
    if (!node->type) {
        // Some conversion builtins (notably `int64`) share the lexer token
        // class used by type names. Resolve the callable spelling before
        // reporting an unknown type so expression-position aliases retain
        // the same semantic meaning.
        AstNode* builtin = build_identifier_from_span(tp, span);
        if (builtin && builtin->node_type == AST_NODE_SYS_FUNC) return builtin;
        record_unknown_base_type_span(tp, span, name);
        node->type = (Type*)&LIT_TYPE_ERROR;
    }
    return (AstNode*)node;
}

static AstNode* direct_type_error_from_span(Transpiler* tp, SourceSpan span) {
    AstTypeNode* node = (AstTypeNode*)alloc_ast_node_from_span(tp, AST_NODE_TYPE,
        span, sizeof(AstTypeNode));
    node->type = (Type*)&LIT_TYPE_ERROR;
    return (AstNode*)node;
}

static AstNode* direct_append(AstNode* first, AstNode* item) {
    if (!item) return first;
    item->next = NULL;
    if (!first) return item;
    AstNode* tail = first;
    while (tail->next) tail = tail->next;
    tail->next = item;
    return first;
}

static AstViewNode* direct_active_view(LambdaDirectAstSink* sink) {
    return sink->view_depth ? sink->view_nodes[sink->view_depth - 1] : NULL;
}

// a view has a functional body but procedural event handlers. Reducing it as
// only its body lost the state scope, so handler assignments were rejected as
// function-level mutations instead of binding to the view state (D2.2.2).
static bool direct_view_begin(LambdaDirectAstSink* sink,
        const LambdaParseReduction* reduction) {
    if (sink->view_depth >= 64 || reduction->child_count != 1) return false;
    Transpiler* tp = sink->tp;
    uint32_t slot = sink->view_depth++;
    AstViewNode* view = (AstViewNode*)alloc_ast_node_from_span(tp,
        AST_NODE_VIEW, reduction->span, sizeof(AstViewNode));
    view->type = set_type_any(tp, ANY_STATEMENT);
    view->is_edit = reduction->detail_token.kind == LAMBDA_TOK_EDIT;
    if (reduction->secondary_token.kind) {
        view->name = name_pool_create_strview(tp->name_pool,
            direct_token_text(tp, reduction->secondary_token));
    }
    view->pattern = direct_ast_node(reduction->children[0]);
    view->vars = lambda_ast_enter_scope(tp, false);
    sink->view_nodes[slot] = view;
    sink->view_scopes[slot] = view->vars;
    sink->view_state_tails[slot] = NULL;
    sink->view_handler_tails[slot] = NULL;
    return true;
}

static bool direct_view_add_state(LambdaDirectAstSink* sink,
        const LambdaParseReduction* reduction) {
    AstViewNode* view = direct_active_view(sink);
    if (!view || reduction->child_count > 1) return false;
    Transpiler* tp = sink->tp;
    AstStateEntry* state = (AstStateEntry*)alloc_ast_node_from_span(tp,
        AST_NODE_STATE_ENTRY, reduction->span, sizeof(AstStateEntry));
    state->type = set_type_any(tp, ANY_STATEMENT);
    state->name = name_pool_create_strview(tp->name_pool,
        direct_token_text(tp, reduction->detail_token));
    // a null value marks an engine-backed binding (bare `state name`); an
    // explicit `state name: null` still yields a literal node, so the two stay
    // distinguishable downstream.
    state->value = reduction->child_count == 1
        ? direct_ast_node(reduction->children[0]) : NULL;

    uint32_t slot = sink->view_depth - 1;
    if (sink->view_state_tails[slot]) {
        sink->view_state_tails[slot]->next_state = state;
    } else {
        view->state = state;
    }
    sink->view_state_tails[slot] = state;

    AstNamedNode* binding = (AstNamedNode*)alloc_ast_node_from_span(tp,
        AST_NODE_PARAM, reduction->span, sizeof(AstNamedNode));
    binding->name = state->name;
    binding->type = state->value && state->value->type
        ? state->value->type : &TYPE_ANY;
    lambda_ast_register_name(tp, binding);
    NameEntry* entry = lookup_name_in_current_scope(tp, binding->name);
    if (entry) entry->is_mutable = true;
    return true;
}

static bool direct_view_begin_handler(LambdaDirectAstSink* sink,
        const LambdaParseReduction* reduction) {
    AstViewNode* view = direct_active_view(sink);
    if (!view || sink->event_handler_depth >= 64) return false;
    Transpiler* tp = sink->tp;
    AstEventHandler* handler = (AstEventHandler*)alloc_ast_node_from_span(tp,
        AST_NODE_EVENT_HANDLER, reduction->span, sizeof(AstEventHandler));
    handler->type = set_type_any(tp, ANY_STATEMENT);
    handler->event = name_pool_create_strview(tp->name_pool,
        direct_token_text(tp, reduction->detail_token));
    handler->vars = lambda_ast_enter_scope_with_parent(tp, view->vars, true);

    uint32_t view_slot = sink->view_depth - 1;
    if (sink->view_handler_tails[view_slot]) {
        sink->view_handler_tails[view_slot]->next_handler = handler;
    } else {
        view->handler = handler;
    }
    sink->view_handler_tails[view_slot] = handler;

    uint32_t slot = sink->event_handler_depth++;
    sink->event_handlers[slot] = handler;
    sink->event_handler_scopes[slot] = handler->vars;
    return true;
}

static bool direct_view_finish_handler(LambdaDirectAstSink* sink,
        const LambdaParseReduction* reduction) {
    if (!sink->event_handler_depth || reduction->child_count != 2) return false;
    AstEventHandler* handler = sink->event_handlers[sink->event_handler_depth - 1];
    handler->source_span = reduction->span;
    handler->param = (AstNamedNode*)direct_ast_node(reduction->children[0]);
    handler->body = direct_ast_node(reduction->children[1]);
    return true;
}

static bool direct_view_end_handler(LambdaDirectAstSink* sink) {
    if (!sink->event_handler_depth) return false;
    uint32_t slot = --sink->event_handler_depth;
    lambda_ast_leave_scope(sink->tp, sink->event_handler_scopes[slot]);
    return true;
}

static AstNode* direct_view_finish(LambdaDirectAstSink* sink,
        const LambdaParseReduction* reduction) {
    AstViewNode* view = direct_active_view(sink);
    if (!view || reduction->child_count != 2) return NULL;
    view->source_span = reduction->span;
    view->param = (AstNamedNode*)direct_ast_node(reduction->children[0]);
    view->body = direct_ast_node(reduction->children[1]);
    return (AstNode*)view;
}

static bool direct_view_end(LambdaDirectAstSink* sink) {
    if (!sink->view_depth) return false;
    uint32_t slot = --sink->view_depth;
    AstViewNode* view = sink->view_nodes[slot];
    lambda_ast_leave_scope(sink->tp, sink->view_scopes[slot]);
    if (view->name) lambda_ast_register_name(sink->tp, (AstNamedNode*)view);
    return true;
}

static AstNode* direct_list_node(Transpiler* tp, SourceSpan span,
        AstNode* items) {
    for (AstNode* item = items; item; item = item->next) {
        reject_procedural_block_operand(tp, item, "a tuple or list element");
    }
    AstListNode* list = (AstListNode*)alloc_ast_node_from_span(tp,
        AST_NODE_LIST, span, sizeof(AstListNode));
    list->item = items;
    list->list_type = (TypeList*)alloc_type(tp->pool, LMD_TYPE_ARRAY,
        sizeof(TypeList));
    list->type = items && !items->next && items->type ? items->type :
        set_type_any(tp, ANY_LIST);
    for (AstNode* item = items; item; item = item->next) {
        list->list_type->length++;
    }
    return (AstNode*)list;
}

static AstNode* direct_content_node(Transpiler* tp, SourceSpan span,
        AstNode* items) {
    AstListNode* content = (AstListNode*)alloc_ast_node_from_span(tp,
        AST_NODE_CONTENT, span, sizeof(AstListNode));
    AstNode* filtered = NULL;
    for (AstNode* item = items; item;) {
        AstNode* next = item->next;
        item->next = NULL;
        if (item->node_type != AST_NODE_NULL) {
            filtered = direct_append(filtered, item);
        }
        item = next;
    }
    content->item = filtered;
    content->list_type = (TypeList*)alloc_type(tp->pool, LMD_TYPE_ARRAY,
        sizeof(TypeList));
    for (AstNode* item = filtered; item; item = item->next) {
        content->list_type->length++;
    }
    // Keep block typing stable: a multi-item functional
    // block is an open list value, not the type of its first declaration. The
    // first-item shortcut reinterprets a later native result at the caller
    // boundary (D2.2.2).
    if (content->list_type->length == 1 && filtered && filtered->type) {
        content->type = filtered->type;
    } else if (tp->current_scope && tp->current_scope->is_proc && filtered) {
        AstNode* last = filtered;
        while (last->next) last = last->next;
        content->type = last->type ? last->type : set_type_any(tp, ANY_LIST);
    } else {
        content->type = set_type_any(tp, ANY_LIST);
    }
    return (AstNode*)content;
}

static AstNode* direct_type_stam(Transpiler* tp, SourceSpan span,
        AstNode* declaration, bool is_public) {
    AstLetNode* node = (AstLetNode*)alloc_ast_node_from_span(tp,
        is_public ? AST_NODE_PUB_STAM : AST_NODE_TYPE_STAM, span,
        sizeof(AstLetNode));
    node->declare = declaration;
    node->type = is_public ? set_type_any(tp, ANY_STATEMENT) : &LIT_NULL;
    return (AstNode*)node;
}

// Pre-bind a `type Name = ...` declaration before its body parses so a
// self-referential alias (`type Node = {left: Node?}`) resolves to this
// binding instead of degrading to ANY. The placeholder map is the map IDENTITY
// recursive fields capture; direct_adopt_pending_alias_map publishes the
// completed shape through that same identity when the declaration reduction
// fires (mirrors the retired CST builder's pre-registration).
static void direct_type_alias_begin(LambdaDirectAstSink* sink,
        const LambdaParseReduction* reduction) {
    Transpiler* tp = sink->tp;
    AstNamedNode* alias = (AstNamedNode*)alloc_ast_node_from_span(tp,
        AST_NODE_ASSIGN, reduction->span, sizeof(AstNamedNode));
    alias->name = name_pool_create_strview(tp->name_pool,
        direct_token_text(tp, reduction->detail_token));
    alias->is_type_definition = true;
    TypeType* pre_type = (TypeType*)alloc_type(tp->pool, LMD_TYPE_TYPE,
        sizeof(TypeType));
    TypeMap* pre_map = (TypeMap*)alloc_type(tp->pool, LMD_TYPE_MAP,
        sizeof(TypeMap));
    pre_map->struct_name = alias->name->chars;
    pre_map->is_trusted_contract = true;
    pre_type->type = (Type*)pre_map;
    alias->type = (Type*)pre_type;
    lambda_ast_register_name(tp, alias);
    sink->pending_type_alias = alias;
}

// Recursive fields were built against the pre-registered placeholder map.
// Publish the completed shape through that same identity so function contracts
// and self-references cannot split into placeholder and final maps.
static void direct_adopt_pending_alias_map(Transpiler* tp, AstNamedNode* alias,
        TypeType* pre_type) {
    TypeMap* pre_map = (TypeMap*)pre_type->type;
    Type* definition = alias->type;
    Type* actual = unwrap_simple_type_type(definition);
    if (!actual || actual->type_id != LMD_TYPE_MAP || actual == &TYPE_MAP ||
            actual == (Type*)pre_map || !pre_map ||
            pre_map->type_id != LMD_TYPE_MAP) return;
    TypeMap* actual_map = (TypeMap*)actual;
    *pre_map = *actual_map;
    pre_map->struct_name = alias->name->chars;
    pre_map->is_trusted_contract = true;
    if (pre_map->type_index >= 0 && pre_map->type_index < tp->type_list->length &&
            tp->type_list->data[pre_map->type_index] == actual_map) {
        tp->type_list->data[pre_map->type_index] = pre_map;
    }
    if (definition->type_id == LMD_TYPE_TYPE) {
        ((TypeType*)definition)->type = (Type*)pre_map;
    }
    alias->type = (Type*)pre_type;
}

static void direct_finalize_type_alias(Transpiler* tp, AstNamedNode* alias) {
    if (!tp || !alias || !alias->type) return;
    Type* definition = alias->type;
    Type* actual = unwrap_simple_type_type(definition);
    if (actual && actual->type_id == LMD_TYPE_MAP && actual != &TYPE_MAP && alias->name) {
        // A named map contract carries its alias into runtime validators and
        // field lowering; anonymous `map` would lose both identities.
        TypeMap* map = (TypeMap*)actual;
        map->struct_name = alias->name->chars;
        map->is_trusted_contract = true;
    }
    bool literal_alias = (definition->type_id == LMD_TYPE_STRING ||
        definition->type_id == LMD_TYPE_SYMBOL) && definition->is_literal;
    bool range_alias = definition->type_id == LMD_TYPE_RANGE &&
        definition->kind == TYPE_KIND_RANGE;
    if (!literal_alias && !range_alias) return;

    // Named literal/range aliases are first-class type values. Keep the
    // payload under the same TypeType carrier and type-list identity; emitting
    // the raw value type makes `x is Alias` test data
    // storage rather than the alias contract (D2.2.2).
    TypeType* wrapper = (TypeType*)alloc_type(tp->pool, LMD_TYPE_TYPE,
        sizeof(TypeType));
    wrapper->type = definition;
    alias->type = (Type*)wrapper;
    arraylist_append(tp->type_list, alias->type);
}

static AstNode* direct_constrained_type(Transpiler* tp, SourceSpan span,
        AstNode* base, AstNode* constraint) {
    AstConstrainedTypeNode* node = (AstConstrainedTypeNode*)alloc_ast_node_from_span(
        tp, AST_NODE_CONSTRAINED_TYPE, span, sizeof(AstConstrainedTypeNode));
    node->base = base;
    node->constraint = constraint;
    TypeConstrained* type = (TypeConstrained*)alloc_type_kind(tp->pool,
        TYPE_KIND_CONSTRAINED, sizeof(TypeConstrained));
    Type* base_type = base && base->type ? base->type : &TYPE_ANY;
    base_type = unwrap_simple_type_type(base_type);
    type->base = base_type ? base_type : &TYPE_ANY;
    type->constraint = constraint;
    node->type = (Type*)type;
    arraylist_append(tp->type_list, node->type);
    type->type_index = tp->type_list->length - 1;
    return (AstNode*)node;
}

static AstNode* direct_pattern_definition(Transpiler* tp,
        SourceSpan span, StrView name, AstNode* island,
        AstNamedNode* pre_bound) {
    AstPatternIslandNode* source = (AstPatternIslandNode*)island;
    AstPatternDefNode* pattern = (AstPatternDefNode*)alloc_ast_node_from_span(tp,
        source->is_symbol ? AST_NODE_SYMBOL_PATTERN : AST_NODE_STRING_PATTERN,
        span, sizeof(AstPatternDefNode));
    pattern->name = name_pool_create_strview(tp->name_pool, name);
    pattern->is_symbol = source->is_symbol;
    pattern->as = source->type == &TYPE_ERROR ? NULL : source->pattern;
    if (!pattern->as) {
        pattern->type = &TYPE_ERROR;
    } else {
        TypePattern* pattern_type = (TypePattern*)alloc_type_kind(tp->pool,
            TYPE_KIND_PATTERN, sizeof(TypePattern));
        pattern_type->is_symbol = pattern->is_symbol;
        pattern_type->pattern_index = -1;
        pattern_type->re2 = NULL;
        pattern_type->re2_unanchored = NULL;
        pattern_type->source = NULL;
        pattern_type->regex_source = NULL;
        pattern->type = (Type*)pattern_type;
    }
    // a parenthesized island (`type P = (\..\)`) still fires the pre-binding;
    // repoint that entry instead of registering a duplicate definition
    NameEntry* entry = pre_bound
        ? lookup_name_in_current_scope(tp, pattern->name) : NULL;
    if (entry && entry->node == (AstNode*)pre_bound) {
        entry->node = (AstNode*)pattern;
    } else {
        lambda_ast_register_name(tp, (AstNamedNode*)pattern);
    }
    return (AstNode*)pattern;
}

static void direct_move_binding(NameScope* from, NameScope* to,
        AstNode* declaration) {
    if (!from || !to || !declaration ||
            (declaration->node_type != AST_NODE_ASSIGN &&
             declaration->node_type != AST_NODE_PARAM)) return;
    AstNamedNode* named = (AstNamedNode*)declaration;
    NameEntry* prior = NULL;
    NameEntry* entry = from->first;
    while (entry && entry->node != declaration) {
        prior = entry;
        entry = entry->next;
    }
    if (!entry) return;
    if (prior) prior->next = entry->next;
    else from->first = entry->next;
    if (from->last == entry) from->last = prior;
    entry->next = NULL;
    entry->scope = to;
    if (!to->first) to->first = entry;
    else to->last->next = entry;
    to->last = entry;
    named->entry = entry;
}

static AstNode* direct_let_group(Transpiler* tp, SourceSpan span,
        AstNode* items, NameScope* existing_scope) {
    bool has_declaration = false;
    for (AstNode* item = items; item; item = item->next) {
        if (item->node_type == AST_NODE_ASSIGN ||
                item->node_type == AST_NODE_DECOMPOSE ||
                item->node_type == AST_NODE_LET_STAM) {
            has_declaration = true;
            break;
        }
    }
    if (!has_declaration) return direct_list_node(tp, span, items);
    AstListNode* list = (AstListNode*)alloc_ast_node_from_span(tp,
        AST_NODE_LIST, span, sizeof(AstListNode));
    list->list_type = (TypeList*)alloc_type(tp->pool, LMD_TYPE_ARRAY,
        sizeof(TypeList));
    NameScope* parent = tp->current_scope;
    NameScope* scope = existing_scope ? existing_scope :
        lambda_ast_enter_scope(tp, false);
    list->vars = scope;
    AstNode* declaration_tail = NULL;
    AstNode* body = NULL;
    for (AstNode* item = items; item;) {
        AstNode* next = item->next;
        item->next = NULL;
        AstNode* declaration = item;
        if (item->node_type == AST_NODE_LET_STAM) {
            // The outer `let` reduction is a statement wrapper; retain only
            // its declaration in the block's declaration chain.
            declaration = ((AstLetNode*)item)->declare;
        }
        if (declaration && (declaration->node_type == AST_NODE_ASSIGN ||
                declaration->node_type == AST_NODE_DECOMPOSE)) {
            if (!existing_scope && declaration->node_type == AST_NODE_ASSIGN) {
                direct_move_binding(parent, scope, declaration);
            }
            if (!list->declare) list->declare = declaration;
            else declaration_tail->next = declaration;
            declaration_tail = declaration;
        } else {
            body = item;
        }
        item = next;
    }
    list->item = body;
    list->list_type->length = body ? 1 : 0;
    list->type = body && body->type ? body->type : &TYPE_NULL;
    if (!existing_scope) lambda_ast_leave_scope(tp, scope);
    return (AstNode*)list;
}

static Type* direct_open_binary_result_type(Transpiler* tp, AnyReason reason,
        Type* left, Type* right) {
    set_type_any(tp, reason);
    // Preserve an open operand's exclusion contract when an
    // operator's result remains `any`. Returning global `any` here lost
    // `any \\ error` and made later call boundaries invent errors.
    if (right->type_id == LMD_TYPE_ANY) return right;
    if (left->type_id == LMD_TYPE_ANY) return left;
    return &TYPE_ANY;
}

static Type* direct_binary_result_type(Transpiler* tp, Operator op,
        AstNode* left, AstNode* right) {
    Type* lt = left && left->type ? left->type : &TYPE_ANY;
    Type* rt = right && right->type ? right->type : &TYPE_ANY;
    if (op == OPERATOR_TO) return &TYPE_RANGE;
    if (op == OPERATOR_EQ || op == OPERATOR_NE ||
            op == OPERATOR_IS || op == OPERATOR_IN || op == OPERATOR_AT) {
        return &TYPE_BOOL;
    }
    if (op == OPERATOR_LT || op == OPERATOR_LE || op == OPERATOR_GT ||
            op == OPERATOR_GE) {
        // Dynamic magnitude comparisons retain an open result. Treating every
        // comparison as bool erased the Tree path's uncertainty and made a
        // later implicit fn boundary report a spurious E208 (S5.5.2).
        bool left_open = lt->type_id == LMD_TYPE_ANY || lt->type_id == LMD_TYPE_NULL;
        bool right_open = rt->type_id == LMD_TYPE_ANY || rt->type_id == LMD_TYPE_NULL;
        return !left_open && !right_open &&
            known_magnitude_comparable(lt->type_id, rt->type_id)
            ? &TYPE_BOOL : set_type_any(tp, ANY_COMPARE);
    }
    if (is_elementwise_comparison_op(op)) {
        return set_type_any(tp, ANY_COMPARE);
    }
    if (op == OPERATOR_AND) {
        return lambda_type_union_normalized(tp->pool, lt, rt);
    }
    if (op == OPERATOR_OR) {
        Type* clean = lambda_type_remove_error_and_null(tp->pool, lt);
        return lambda_type_union_normalized(tp->pool, clean, rt);
    }
    if (op == OPERATOR_PIPE || op == OPERATOR_WHERE) {
        if (op == OPERATOR_WHERE) return lt;
        if (has_current_item_ref(right)) {
            TypeArray* mapped = (TypeArray*)alloc_type(tp->pool,
                LMD_TYPE_ARRAY, sizeof(TypeArray));
            mapped->nested = rt;
            mapped->type_index = -1;
            return (Type*)mapped;
        }
        return rt;
    }
    if ((op == OPERATOR_ADD || op == OPERATOR_SUB || op == OPERATOR_MUL ||
            op == OPERATOR_DIV || op == OPERATOR_POW) &&
            (lt->type_id == LMD_TYPE_COMPLEX || rt->type_id == LMD_TYPE_COMPLEX)) {
        return &TYPE_COMPLEX;
    }
    // Power deliberately remains boxed: negative exponents and mixed numeric
    // domains are resolved by `fn_pow`, and the MIR native lane rejects an
    // inferred scalar type for this operator.
    if (op == OPERATOR_POW) return direct_open_binary_result_type(tp,
        ANY_JOIN_OP, lt, rt);
    if (op == OPERATOR_ADD || op == OPERATOR_SUB || op == OPERATOR_MUL ||
            op == OPERATOR_DIV || op == OPERATOR_IDIV || op == OPERATOR_MOD ||
            op == OPERATOR_POW) {
        LambdaNumericOpFamily family = op == OPERATOR_ADD ? LAMBDA_NUM_OP_ADD :
            op == OPERATOR_SUB ? LAMBDA_NUM_OP_SUB :
            op == OPERATOR_MUL ? LAMBDA_NUM_OP_MUL :
            op == OPERATOR_DIV ? LAMBDA_NUM_OP_TRUE_DIV :
            op == OPERATOR_IDIV ? LAMBDA_NUM_OP_IDIV : LAMBDA_NUM_OP_MOD;
        LambdaNumericDecision decision = lambda_numeric_classify(family,
            lambda_numeric_kind_from_type(lt), lambda_numeric_kind_from_type(rt));
        if (decision.valid) return lambda_numeric_type_from_kind(decision.result);
        return direct_open_binary_result_type(tp, ANY_ARITH_OPERAND, lt, rt);
    }
    return direct_open_binary_result_type(tp, ANY_JOIN_OP, lt, rt);
}

static bool direct_validate_relational_operands(Transpiler* tp,
        SourceSpan span, Operator op, AstNode* left, AstNode* right) {
    if (!is_relational_op(op)) return true;
    AstNode* left_value = ast_unwrap_primary(left);
    if (left_value && left_value->node_type == AST_NODE_BINARY &&
            is_relational_op(((AstBinaryNode*)left_value)->op)) {
        // A left-associated chain is normalized to two scalar predicates
        // below. Its temporary bool node is not an operand of the final
        // comparison, so validate after that normalization instead.
        return true;
    }
    Type* left_type = left && left->type ? lambda_type_remove_error_and_null(
        tp->pool, left->type) : NULL;
    Type* right_type = right && right->type ? lambda_type_remove_error_and_null(
        tp->pool, right->type) : NULL;
    if (!left_type || !right_type) return true;
    // Reuse the magnitude predicate instead of treating every `type` tag
    // as a container: abstract `number` is represented by that tag, and
    // conversions such as `int(value) < 0` are valid scalar comparisons.
    if (known_magnitude_comparable_type_set(left_type, right_type)) {
        return true;
    }
    record_semantic_error_span(tp, span, ERR_INVALID_OPERATION,
        "ordered comparison has no magnitude for these types; use sort() for total ordering");
    return false;
}

static AstNode* direct_promote_bare_pipe_sysfunc(Transpiler* tp,
        AstNode* right) {
    AstNode* target = ast_unwrap_primary(right);
    if (!target || (target->node_type != AST_NODE_SYS_FUNC &&
            target->node_type != AST_NODE_IDENT)) return NULL;
    StrView name = {0};
    if (target->node_type == AST_NODE_SYS_FUNC) {
        SysFuncInfo* current = ((AstSysFuncNode*)target)->fn_info;
        if (!current) return NULL;
        name = strview_from_cstr(current->name);
    } else {
        AstIdentNode* ident = (AstIdentNode*)target;
        if (ident->entry) return NULL;
        name = strview_init(ident->name->chars, ident->name->len);
    }
    if (!get_sys_func_info(&name, 1) &&
            !lookup_global_imported_sys_func(tp, &name, 1)) return NULL;
    int prior_pipe_inject = tp->pipe_inject_args;
    tp->pipe_inject_args = 1;
    AstNode* call = build_call_node_from_parts(tp, right->source_span,
        target, NULL, 0);
    tp->pipe_inject_args = prior_pipe_inject;
    return call;
}

AstNode* build_binary_node_from_parts(Transpiler* tp, SourceSpan span,
        StrView op_spelling, AstNode* left, AstNode* right) {
    AstBinaryNode* node = (AstBinaryNode*)alloc_ast_node_from_span(tp,
        AST_NODE_BINARY, span, sizeof(AstBinaryNode));
    node->left = left;
    node->right = right;
    node->op_str = op_spelling;
    reject_procedural_block_operand(tp, left, "an operand");
    reject_procedural_block_operand(tp, right, "an operand");
    if (!lambda_binary_operator_from_spelling(op_spelling, &node->op)) {
        record_semantic_error_span(tp, span, ERR_INVALID_OPERATION,
            "unknown binary operator '%.*s'", (int)op_spelling.length,
            op_spelling.str);
        node->type = &TYPE_ERROR;
        return (AstNode*)node;
    }
    if (node->op == OPERATOR_PIPE || node->op == OPERATOR_WHERE) {
        node->node_type = AST_NODE_PIPE;
    }
    if (node->op == OPERATOR_PIPE && !has_current_item_ref(right)) {
        AstNode* promoted = direct_promote_bare_pipe_sysfunc(tp, right);
        if (promoted) {
            node->right = right = promoted;
        }
    }
    if (ast_binary_is_type_set_op(node->op) && promote_type_union_expr(tp, node)) {
        return (AstNode*)node;
    }
    if (node->op == OPERATOR_OR && ast_is_explicit_type_value(left) &&
            ast_is_explicit_type_value(right)) {
        // `or` handles runtime error/null values; preserve the type-union
        // spelling check after the direct parser has already built both sides.
        record_semantic_error_span(tp, span, ERR_INVALID_OPERATION,
            "operator `or` cannot combine type values; use `|` to form a union type");
        node->type = &TYPE_ERROR;
        return (AstNode*)node;
    }
    if ((node->op == OPERATOR_IDIV || node->op == OPERATOR_MOD) &&
            ast_static_numeric_literal_is_zero(tp, right)) {
        // Literal integral zero is a compile-time invalid operation, before
        // MIR lowering could obscure the source diagnostic (S3.3.4).
        record_semantic_error_span(tp, right->source_span, ERR_INVALID_OPERATION,
            "integral division or remainder by literal zero");
        node->type = &TYPE_ERROR;
        return (AstNode*)node;
    }
    if ((node->op == OPERATOR_ADD || node->op == OPERATOR_SUB ||
            node->op == OPERATOR_MUL || node->op == OPERATOR_DIV ||
            node->op == OPERATOR_POW) &&
            ((left && left->type && left->type->type_id == LMD_TYPE_COMPLEX) ||
             (right && right->type && right->type->type_id == LMD_TYPE_COMPLEX))) {
        TypeId left_type = left && left->type ? left->type->type_id : LMD_TYPE_ANY;
        TypeId right_type = right && right->type ? right->type->type_id : LMD_TYPE_ANY;
        bool left_valid = left_type == LMD_TYPE_COMPLEX ||
            is_complex_component_type(left_type);
        bool right_valid = right_type == LMD_TYPE_COMPLEX ||
            is_complex_component_type(right_type);
        if (!left_valid || !right_valid) {
            // Preserve the concrete complex arithmetic contract. Leaving this
            // pair as open any makes a typed call add
            // a spurious error arm for an expression that cannot fail here.
            record_semantic_error_span(tp, span, ERR_INVALID_OPERATION,
                "operator '%.*s' is not defined for %s and %s",
                (int)op_spelling.length, op_spelling.str,
                get_type_name(left_type), get_type_name(right_type));
            node->type = &TYPE_ERROR;
            return (AstNode*)node;
        }
    }
    if (!direct_validate_relational_operands(tp, span, node->op, left, right)) {
        node->type = &TYPE_ERROR;
        return (AstNode*)node;
    }
    // The grammar parses relational chains left-associatively, but Lambda's
    // semantics are pairwise: `a < b < c` means `(a < b) and (b < c)`.
    // Preserve the shared AST shape here; otherwise the second comparison
    // consumes the first boolean and every chained comparison becomes an
    // invalid numeric operation (D2.2.2).
    AstNode* left_value = ast_unwrap_primary(left);
    if (is_relational_op(node->op) && left_value &&
            left_value->node_type == AST_NODE_BINARY &&
            is_relational_op(((AstBinaryNode*)left_value)->op)) {
        AstBinaryNode* left_cmp = (AstBinaryNode*)left_value;
        AstBinaryNode* right_cmp = (AstBinaryNode*)alloc_ast_node_from_span(tp,
            AST_NODE_BINARY,
            (SourceSpan){left_cmp->right->source_span.start_byte,
                span.end_byte}, sizeof(AstBinaryNode));
        right_cmp->left = left_cmp->right;
        right_cmp->right = right;
        right_cmp->op = node->op;
        right_cmp->op_str = op_spelling;
        right_cmp->type = &TYPE_BOOL;
        node->left = left;
        node->op = OPERATOR_AND;
        node->op_str = strview_from_cstr("and");
        node->right = (AstNode*)right_cmp;
        node->type = &TYPE_BOOL;
        return (AstNode*)node;
    }
    if (node->op == OPERATOR_IS && right && right->node_type == AST_NODE_PRIMARY) {
        AstPrimaryNode* primary = (AstPrimaryNode*)right;
        if (primary->type && primary->type->type_id == LMD_TYPE_FLOAT &&
                __builtin_isnan(((TypeFloat*)primary->type)->double_val)) {
            // `nan` is a value-pattern atom, not a type witness; retain the
            // dedicated runtime predicate so NaN payloads are recognized on
            // the direct path (S3.4.2).
            node->op = OPERATOR_IS_NAN;
            node->type = &TYPE_BOOL;
            return (AstNode*)node;
        }
    }
    node->type = direct_binary_result_type(tp, node->op, left, right);
    if (is_elementwise_comparison_op(node->op) &&
            ((left && is_array_family_type_id(left->type ? left->type->type_id : LMD_TYPE_ANY)) ||
             (right && is_array_family_type_id(right->type ? right->type->type_id : LMD_TYPE_ANY)))) {
        // Keyword comparisons over arrays produce a bool mask. Keeping the
        // scalar bool result here makes `a[a gt 0]` pass a bool to the indexer,
        // which then returns a raw pointer instead of a boxed selection.
        node->type = alloc_array_num_result_type(tp, node);
    }
    if (!node->type) node->type = &TYPE_ANY;
    return (AstNode*)node;
}

static Type* direct_field_result_type(Transpiler* tp, AstNode* object,
        AstNode* field, AstNodeType node_type) {
    if (!object || !object->type) return set_type_any(tp, ANY_MEMBER_SHAPE);
    Type* object_type = object->type;
    if (node_type == AST_NODE_INDEX_EXPR) {
        if (object_type->type_id == LMD_TYPE_ARRAY_NUM ||
                object_type->type_id == LMD_TYPE_ARRAY) {
            AstNode* index = unwrap_primary_node(field);
            if (index && index->node_type == AST_NODE_BINARY &&
                    ((AstBinaryNode*)index)->op == OPERATOR_TO) {
                // A range index produces a sliced collection, not the
                // element lane. Returning the nested scalar here makes a
                // surrounding map allocate a native slot and rejects the
                // actual array result at runtime (S7.1).
                return set_type_any(tp, ANY_INDEX_ELEM);
            }
            if (index && index->type &&
                    (index->type->type_id == LMD_TYPE_ARRAY_NUM ||
                     index->type->type_id == LMD_TYPE_ARRAY ||
                     index->type->type_id == LMD_TYPE_TYPE)) {
                // A mask or type query returns a collection of matches, not a
                // scalar element. Keep the boxed result contract before map
                // shape construction (S7.1).
                return set_type_any(tp, ANY_INDEX_ELEM);
            }
            AstNode* object_base = ast_unwrap_primary(object);
            bool mutable_binding = object_base &&
                object_base->node_type == AST_NODE_IDENT &&
                ((AstIdentNode*)object_base)->entry &&
                (((AstIdentNode*)object_base)->entry->is_mutable ||
                 ((AstIdentNode*)object_base)->entry->type_widened);
            if (mutable_binding) return set_type_any(tp, ANY_INDEX_ELEM);
            TypeArray* array = (TypeArray*)object_type;
            return array->nested && array->nested->type_id != LMD_TYPE_ANY
                ? lambda_type_nullable_normalized(tp->pool, array->nested)
                : set_type_any(tp, ANY_INDEX_ELEM);
        }
        return set_type_any(tp, ANY_INDEX_ELEM);
    }
    if ((object_type->type_id == LMD_TYPE_MAP ||
            object_type->type_id == LMD_TYPE_OBJECT) &&
            !is_global_simple_type(object_type) && field &&
            field->node_type == AST_NODE_IDENT) {
        TypeMap* map = (TypeMap*)object_type;
        if (!map->struct_name || !map->shape) {
            // An inferred map literal is mutable and its field shape can
            // change after this read was built. Only named record/object
            // contracts may publish a native field lane; otherwise a later
            // map write is read back through the literal's stale slot type.
            return set_type_any(tp, ANY_MEMBER_SHAPE);
        }
        AstIdentNode* ident = (AstIdentNode*)field;
        Type* matched = NULL;
        FOR_EACH_MAP_FIELD(map, entry) {
            if (entry->name && entry->name->length == ident->name->len &&
                    strncmp(entry->name->str, ident->name->chars,
                            entry->name->length) == 0) {
                // Shape entries are retained in source order; runtime lookup
                // is last-writer-wins, so the type oracle must keep scanning
                // duplicate keys instead of publishing the first contract.
                matched = unwrap_simple_type_type(entry->type);
            }
        }
        if (matched) {
            TypeId field_tid = matched->type_id;
            if (is_native_numeric_type_id(field_tid) ||
                    field_tid == LMD_TYPE_BOOL || field_tid == LMD_TYPE_STRING) {
                // A scalar member expression exposes its payload lane, not the
                // shape occurrence wrapper. Retaining `string?` here made an
                // unannotated local box raw null String* bits as a Type item,
                // collapsing S7.1.1 absence into an empty string. Match the
                // Tree path; only container fields need their shape for a
                // following member access.
                return alloc_type(tp->pool, field_tid, sizeof(Type));
            }
            if (field_tid == LMD_TYPE_MAP || field_tid == LMD_TYPE_ELEMENT ||
                    field_tid == LMD_TYPE_OBJECT) {
                return matched;
            }
        }
    }
    return set_type_any(tp, ANY_MEMBER_SHAPE);
}

AstNode* build_field_node_from_parts(Transpiler* tp, SourceSpan span,
        AstNodeType node_type, AstNode* object, AstNode* field) {
    if (node_type == AST_NODE_MEMBER_EXPR) {
        AstNode* object_value = ast_unwrap_primary(object);
        AstNode* field_value = ast_unwrap_primary(field);
        if (object_value && field_value &&
                field_value->node_type == AST_NODE_IDENT) {
            AstIdentNode* field_ident = (AstIdentNode*)field_value;
            StrView object_name = source_span_text(tp, object_value->source_span);
            char qualified[256];
            snprintf(qualified, sizeof(qualified), "%.*s.%.*s",
                (int)object_name.length, object_name.str,
                (int)field_ident->name->len, field_ident->name->chars);
            NameEntry* imported = lookup_name(tp, strview_from_cstr(qualified));
            if (imported && imported->import && imported->node) {
                // An import alias may share a system-function spelling (for
                // example `stack`). Resolve its qualified export before the
                // bare spelling can become a callable value (D4.6.1v2).
                AstIdentNode* resolved = (AstIdentNode*)alloc_ast_node_from_span(
                    tp, AST_NODE_IDENT, span, sizeof(AstIdentNode));
                resolved->name = imported->name;
                resolved->entry = imported;
                resolved->type = imported->node->type ? imported->node->type : &TYPE_ANY;
                return build_primary_wrapper_from_parts(tp, span, (AstNode*)resolved);
            }
        }
        if (object_value && object_value->node_type == AST_NODE_IDENT &&
                field_value && field_value->node_type == AST_NODE_IDENT) {
            AstIdentNode* object_ident = (AstIdentNode*)object_value;
            AstIdentNode* field_ident = (AstIdentNode*)field_value;
            StrView module_name = strview_init(object_ident->name->chars,
                object_ident->name->len);
            NamespaceEntry* ns_entry = lookup_namespace(tp, object_ident->name);
            if (ns_entry) {
                // A namespace prefix in expression position denotes a
                // qualified symbol, not an unresolved variable member.
                return build_namespace_symbol_from_parts(tp, span,
                    object_ident->name, field_ident->name, ns_entry);
            }
            const char* module = resolve_imported_module(tp, &module_name);
            if (module && strcmp(module, "math") == 0) {
                double value = 0.0;
                bool is_float = false;
                if (field_ident->name->len == 7 &&
                        memcmp(field_ident->name->chars, "max_int", 7) == 0) {
                    // keep the named math constant on the same literal lane
                    // as the shared literal lane; otherwise overflow checks reject the
                    // canonical compact-int boundary (D2.2.2).
                    TypeInt64* constant = (TypeInt64*)alloc_type(tp->pool,
                        LMD_TYPE_INT, sizeof(TypeInt64));
                    constant->int64_val = INT53_MAX;
                    constant->is_const = 1;
                    constant->is_literal = 1;
                    arraylist_append(tp->const_list, &constant->int64_val);
                    constant->const_index = tp->const_list->length - 1;
                    AstPrimaryNode* result = (AstPrimaryNode*)alloc_ast_node_from_span(
                        tp, AST_NODE_PRIMARY, span, sizeof(AstPrimaryNode));
                    result->type = (Type*)constant;
                    return (AstNode*)result;
                }
                if (field_ident->name->len == 2 &&
                        memcmp(field_ident->name->chars, "pi", 2) == 0) {
                    value = 3.14159265358979323846;
                    is_float = true;
                } else if (field_ident->name->len == 1 &&
                        field_ident->name->chars[0] == 'e') {
                    value = 2.71828182845904523536;
                    is_float = true;
                }
                if (is_float) {
                    TypeFloat* constant = (TypeFloat*)alloc_type(tp->pool,
                        LMD_TYPE_FLOAT, sizeof(TypeFloat));
                    constant->double_val = value;
                    constant->is_const = 1;
                    constant->is_literal = 1;
                    arraylist_append(tp->const_list, &constant->double_val);
                    constant->const_index = tp->const_list->length - 1;
                    AstPrimaryNode* result = (AstPrimaryNode*)alloc_ast_node_from_span(
                        tp, AST_NODE_PRIMARY, span, sizeof(AstPrimaryNode));
                    result->type = (Type*)constant;
                    return (AstNode*)result;
                }
            }
        }
    }
    AstFieldNode* node = (AstFieldNode*)alloc_ast_node_from_span(tp, node_type,
        span, sizeof(AstFieldNode));
    node->object = object;
    node->field = field;
    node->computed = node_type == AST_NODE_INDEX_EXPR;
    if (node_type == AST_NODE_INDEX_EXPR) {
        Type* declared = declared_compound_destination_type(tp,
            (AstNode*)node, NULL);
        if (declared) {
            // Indexed reads are total. Preserve the annotated element contract
            // as nullable so an OOB read cannot bypass its declaration check.
            node->type = lambda_type_nullable_normalized(tp->pool, declared);
            return (AstNode*)node;
        }
    }
    node->type = direct_field_result_type(tp, object, field, node_type);
    return (AstNode*)node;
}

AstNode* build_query_node_from_parts(Transpiler* tp, SourceSpan span,
        AstNode* object, AstNode* query, bool direct) {
    AstQueryNode* node = (AstQueryNode*)alloc_ast_node_from_span(tp,
        AST_NODE_QUERY_EXPR, span, sizeof(AstQueryNode));
    node->object = object;
    node->query = query;
    node->direct = direct;
    node->type = alloc_type(tp->pool, LMD_TYPE_ARRAY, sizeof(TypeList));
    return (AstNode*)node;
}

// S12.1.4/S12.3.4: `call` is effect-polymorphic, so it has two registry rows —
// one per colour — and the `(name, arg_count)` lookup cannot tell them apart
// (the LR09-1 hazard). Resolve the tie here, where the enclosing colour is
// known: the ENCLOSING context fixes the error convention (fn returns, pn
// raises), while the TARGET's colour is what S12.1.4 checks.
static SysFuncInfo* resolve_effect_polymorphic_row(Transpiler* tp,
        SysFuncInfo* info) {
    if (!info || (info->fn != SYSFUNC_CALL && info->fn != SYSPROC_CALL)) return info;
    bool enclosing_proc = tp->current_scope && tp->current_scope->is_proc;
    bool already_selected = enclosing_proc
        ? info->fn == SYSPROC_CALL : info->fn == SYSFUNC_CALL;
    if (already_selected) return info;
    SysFunc desired = enclosing_proc ? SYSPROC_CALL : SYSFUNC_CALL;
    for (int i = 0; i < sys_func_def_count; i++) {
        if (sys_func_defs[i].fn == desired) return &sys_func_defs[i];
    }
    return info;
}

static AstNode* direct_sys_function(Transpiler* tp, SourceSpan span,
        SysFuncInfo* info) {
    info = resolve_effect_polymorphic_row(tp, info);
    AstSysFuncNode* node = (AstSysFuncNode*)alloc_ast_node_from_span(tp,
        AST_NODE_SYS_FUNC, span, sizeof(AstSysFuncNode));
    node->fn_info = info;
    // A resolved call callee carries its return-typed node. A
    // standalone sysfunc value uses the separate callable-signature builder.
    // The transpiler relies on this distinction when boxing native results at
    // handler and collection boundaries (D2.2.2).
    node->type = info->return_type;
    return (AstNode*)node;
}

static void direct_validate_mutable_compound(Transpiler* tp,
        SourceSpan span, AstNode* object);

static AstNode* direct_start_node(Transpiler* tp, SourceSpan span,
        AstCallNode* source_call, int arg_count) {
    AstStartNode* start = (AstStartNode*)alloc_ast_node_from_span(tp,
        AST_NODE_START, span, sizeof(AstStartNode));
    start->owner_scope = tp->current_scope;
    start->mode = START_MODE_TASK;
    start->type = set_type_any(tp, ANY_LEGACY_UNCLASSIFIED);
    if (!tp->current_scope || !tp->current_scope->is_proc) {
        // Task creation participates in procedure return/join flow, so a
        // direct call must retain the same scope firewall.
        record_semantic_error_span(tp, span, ERR_PROC_IN_FN,
            "`start` is only allowed inside a procedure (pn)");
        start->type = &TYPE_ERROR;
    }
    if (arg_count < 1 || arg_count > 3) {
        record_semantic_error_span(tp, span, ERR_ARGUMENT_COUNT_MISMATCH,
            "`start` expects 1 to 3 arguments, got %d", arg_count);
        start->type = &TYPE_ERROR;
        return (AstNode*)start;
    }
    AstNode* target = source_call->argument;
    AstNode* args = target ? target->next : NULL;
    AstNode* options = args ? args->next : NULL;
    if (start_has_named_arguments(target)) {
        record_semantic_error_span(tp, span, ERR_INVALID_OPERATION,
            "`start` uses positional arguments: start(pn, args, options)");
        start->type = &TYPE_ERROR;
        return (AstNode*)start;
    }
    if (target) target->next = NULL;
    if (args) args->next = NULL;
    if (options) options->next = NULL;
    TypeFunc* fn_type = target && target->type && target->type->type_id == LMD_TYPE_FUNC
        ? (TypeFunc*)target->type : NULL;
    AstCallNode* target_call = (AstCallNode*)alloc_ast_node_from_span(tp,
        AST_NODE_CALL_EXPR, span, sizeof(AstCallNode));
    target_call->function = target;
    for (AstNode* a = args; a; a = a->next) {
        reject_procedural_block_operand(tp, a, "a call argument");
    }
    target_call->argument = args;
    target_call->type = fn_type ? function_call_result_type(tp, fn_type) : &TYPE_ERROR;
    target_call->can_raise = fn_type && fn_type->can_raise;
    start->call = target_call;
    validate_start_parts(tp, start, span, target, args, options, target_call);
    return (AstNode*)start;
}

// S12.3.3: a user-defined field or method on the receiver shadows a
// method-eligible builtin of the same name. `out_has_user_member` reports any
// match, field or method, so the caller can suppress the builtin even when the
// match is a plain field — a field carries no TypeMethod to return, and
// returning NULL alone would let the builtin capture the call.
static TypeMethod* direct_lookup_object_method(Transpiler* tp,
        AstNode* receiver, StrView name, bool* out_has_user_member) {
    if (out_has_user_member) *out_has_user_member = false;
    if (!receiver || !receiver->type) return NULL;
    Type* receiver_type = receiver->type;
    if (!is_map_family_type_id(receiver_type->type_id) ||
            is_global_simple_type(receiver_type)) return NULL;
    if (receiver_type->type_id != LMD_TYPE_OBJECT) {
        // maps, vmaps, and elements carry shape entries but no method table.
        FOR_EACH_MAP_FIELD((TypeMap*)receiver_type, field) {
            if (field->name && field->name->length == name.length &&
                    strncmp(field->name->str, name.str, name.length) == 0) {
                if (out_has_user_member) *out_has_user_member = true;
                break;
            }
        }
        return NULL;
    }
    TypeObject* object_type = (TypeObject*)receiver_type;
    for (TypeObject* owner = object_type; owner; owner = owner->base) {
        for (ShapeEntry* field = owner->shape; field; field = field->next) {
            if (field->name && field->name->length == name.length &&
                    strncmp(field->name->str, name.str, name.length) == 0) {
                // fields shadow methods.
                if (out_has_user_member) *out_has_user_member = true;
                return NULL;
            }
        }
        for (TypeMethod* method = owner->methods; method; method = method->next) {
            if (method->name && method->name->length == name.length &&
                    strncmp(method->name->str, name.str, name.length) == 0) {
                if (out_has_user_member) *out_has_user_member = true;
                return method;
            }
        }
    }
    return NULL;
}

// S12.1.4: `call(f, args)` takes its colour from `f`. Resolve that statically
// whenever `f` names a known function — a compile error beats the runtime one,
// and the runtime check cannot see a closure whose `fn_type` was never
// populated. A dynamically-computed `f` falls through to the runtime check.
static void validate_effect_polymorphic_call(Transpiler* tp, SourceSpan span,
        AstNode* function, AstNode* arguments) {
    AstNode* callee = ast_unwrap_primary(function);
    if (!callee || callee->node_type != AST_NODE_SYS_FUNC) return;
    SysFuncInfo* info = ((AstSysFuncNode*)callee)->fn_info;
    if (!info || (info->fn != SYSFUNC_CALL && info->fn != SYSPROC_CALL)) return;
    if (tp->current_scope && tp->current_scope->is_proc) return;  // pn context: any target
    AstNode* target = ast_unwrap_primary(arguments);
    if (!target || target->node_type != AST_NODE_IDENT) return;
    AstIdentNode* ident = (AstIdentNode*)target;
    AstNode* binding = ident->entry ? ident->entry->node : NULL;
    TypeFunc* signature = binding && binding->type &&
            binding->type->type_id == LMD_TYPE_FUNC
        ? (TypeFunc*)binding->type : NULL;
    if (signature && signature->is_proc) {
        record_semantic_error_span(tp, span, ERR_PROC_IN_FN,
            "call: '%.*s' is a procedure (pn) and cannot be called from a "
            "function (fn)",
            ident->name ? (int)ident->name->len : 0,
            ident->name ? ident->name->chars : "");
    }
}

AstNode* build_call_node_from_parts(Transpiler* tp, SourceSpan span,
        AstNode* function, AstNode* arguments, int arg_count) {
    AstCallNode* call = (AstCallNode*)alloc_ast_node_from_span(tp,
        AST_NODE_CALL_EXPR, span, sizeof(AstCallNode));
    call->function = function;
    for (AstNode* a = arguments; a; a = a->next) {
        reject_procedural_block_operand(tp, a, "a call argument");
    }
    call->argument = arguments;
    (void)validate_lambda_argument_limit(tp, span, arg_count, "call argument");
    StrView name = {0};
    SysFuncInfo* info = NULL;
    bool method_call = false;
    bool user_method_found = false;
    bool user_method_is_proc = false;
    AstNode* user_method_receiver = NULL;
    StrView user_method_name = {0};
    int lookup_arg_count = arg_count + tp->pipe_inject_args;
    AstNode* effective = ast_unwrap_primary(function);
    if (effective && effective->node_type == AST_NODE_MEMBER_EXPR) {
        AstFieldNode* member = (AstFieldNode*)effective;
        AstNode* object = ast_unwrap_primary(member->object);
        AstNode* field = ast_unwrap_primary(member->field);
        if (object && object->node_type == AST_NODE_IDENT &&
                field && field->node_type == AST_NODE_IDENT) {
            StrView module_name = strview_init(
                ((AstIdentNode*)object)->name->chars,
                ((AstIdentNode*)object)->name->len);
            StrView field_name = strview_init(
                ((AstIdentNode*)field)->name->chars,
                ((AstIdentNode*)field)->name->len);
            const char* module = resolve_imported_module(tp, &module_name);
            if (module) {
                char qualified[128];
                snprintf(qualified, sizeof(qualified), "%s_%.*s", module,
                    (int)field_name.length, field_name.str);
                StrView qualified_name = strview_from_cstr(qualified);
                info = get_sys_func_info(&qualified_name, lookup_arg_count);
                if (info) call->function = direct_sys_function(tp,
                    effective->source_span, info);
            } else {
                char qualified[256];
                snprintf(qualified, sizeof(qualified), "%.*s.%.*s",
                    (int)module_name.length, module_name.str,
                    (int)field_name.length, field_name.str);
                NameEntry* entry = lookup_name(tp, strview_from_cstr(qualified));
                if (entry && entry->node) {
                    call->function = entry->node;
                    effective = entry->node;
                }
                // otherwise fall through to the shared receiver-aware lookup
                // below. Resolving a method-only builtin here skipped the
                // user-member check and let `sum`/`avg` shadow an identically
                // named object method, violating S12.3.3.
            }
        }
        if (!info && field && field->node_type == AST_NODE_IDENT) {
            // Method receivers are not limited to identifiers: literals,
            // arrays, and chained calls must use the same receiver-aware
            // registry lookup as `x.method()`.
            AstNode* receiver = object ? object : member->object;
            StrView field_name = strview_init(((AstIdentNode*)field)->name->chars,
                ((AstIdentNode*)field)->name->len);
            TypeId object_type = receiver && receiver->type ? receiver->type->type_id : LMD_TYPE_ANY;
            bool receiver_has_member = false;
            TypeMethod* user_method = direct_lookup_object_method(tp, receiver,
                field_name, &receiver_has_member);
            if (user_method) {
                user_method_found = true;
                user_method_is_proc = user_method->is_proc;
                user_method_receiver = receiver;
                user_method_name = field_name;
            } else if (!receiver_has_member) {
                info = get_sys_func_for_method(&field_name, arg_count, object_type);
                if (info) {
                    method_call = true;
                    if (receiver) {
                        receiver->next = arguments;
                        call->argument = receiver;
                    }
                }
            }
        }
    }
    if (effective && effective->node_type == AST_NODE_SYS_FUNC) {
        info = ((AstSysFuncNode*)effective)->fn_info;
        if (tp->pipe_inject_args > 0 && info) {
            name = strview_from_cstr(info->name);
            SysFuncInfo* piped = get_sys_func_info(&name, lookup_arg_count);
            if (!piped) piped = lookup_global_imported_sys_func(tp, &name,
                lookup_arg_count);
            if (piped) info = piped;
        }
    } else if (effective && effective->node_type == AST_NODE_IDENT) {
        AstIdentNode* ident = (AstIdentNode*)effective;
        name = (StrView){ident->name->chars, ident->name->len};
        // S12.3.7: ANY module binding shadows the sys func, not just a
        // callable one. Testing for FUNC/PROC alone let `let sum = 5` fall
        // through to the builtin, so `sum([1,2])` silently returned 3
        // instead of the not-callable error the binding demands.
        bool user_function = ident->entry && ident->entry->node;
        if (!user_function) info = get_sys_func_info(&name, lookup_arg_count);
        if (!info && !user_function) {
            // Qualified imports are resolved above; bare calls need the same
            // module-prefix fallback (`sqrt` -> `math_sqrt`).
            info = lookup_global_imported_sys_func(tp, &name, lookup_arg_count);
        }
        if (!info && !user_function) {
            info = lookup_complex_math_builtin(&name, lookup_arg_count);
        }
    } else if (effective && effective->node_type == AST_NODE_TYPE) {
        // The lexer classifies built-in spellings such as `string` and `map`
        // as base types before postfix parsing. In expression position a
        // following call is still the ordinary conversion/constructor builtin;
        // resolve it by the same name and arity path as an identifier call.
        name = source_span_text(tp, effective->source_span);
        NameEntry* entry = lookup_name(tp, name);
        bool user_function = entry && entry->node &&
            (entry->node->node_type == AST_NODE_FUNC ||
             entry->node->node_type == AST_NODE_PROC);
        if (user_function) {
            call->function = build_identifier_from_span(tp, effective->source_span);
            effective = ast_unwrap_primary(call->function);
        } else {
            info = get_sys_func_info(&name, lookup_arg_count);
            if (!info) info = lookup_global_imported_sys_func(tp, &name,
                lookup_arg_count);
            if (!info) info = lookup_complex_math_builtin(&name,
                lookup_arg_count);
        }
    }
    if (info && info->fn == SYSPROC_START) return direct_start_node(tp, span, call, arg_count);
    if (info) {
        // Keep the resolved sysfunc as the call callee even when the parser
        // supplied a primary wrapper around the spelling. The MIR pipe fast
        // path (and the Tree builder) dispatch on AST_NODE_SYS_FUNC itself;
        // retaining the wrapper would fall back to generic fn_pipe_call.
        call->function = direct_sys_function(tp, function->source_span, info);
        if (info->fn == SYSPROC_PUSH || info->fn == SYSPROC_SPLICE) {
            // Mutation builtins write through their first argument just like
            // compound assignment; apply the same immutable-root rule before
            // lowering can select the raw in-place helper (S9.1.1, S9.1.6).
            direct_validate_mutable_compound(tp, span, call->argument);
        }
        call->can_raise = info->can_raise;
        call->pipe_inject = tp->pipe_inject_args > 0 && !method_call;
        call->type = sys_func_call_result_type(tp, info,
            sys_func_call_may_return_error(tp, info, call->argument), call->argument);
        Type* bitwise_type = infer_bitwise_call_type(info->fn,
            call->argument, call->argument ? call->argument->next : NULL);
        if (bitwise_type) {
            call->type = bitwise_type;
        }
    } else if (effective && effective->type && effective->type->type_id == LMD_TYPE_FUNC) {
        TypeFunc* type = (TypeFunc*)effective->type;
        call->can_raise = type->can_raise;
        call->type = function_call_result_type(tp, type);
    } else {
        // Sized/base type calls (`i32(value)`, `u8(value)`) have no registry
        // function; preserve their target type for the MIR coercion lane
        // instead of lowering them as dynamic calls (D2.2.2).
        Type* target_type = ast_called_type_target(call->function);
        bool unresolved_system_spelling = effective &&
            effective->node_type == AST_NODE_TYPE && name.length &&
            is_sys_func_name(name.str, (int)name.length);
        // `error` is both a type spelling and a constructor name (S7.4.4).
        // If no arity-compatible system entry was found, the Tree builder
        // keeps that call dynamic; treating the lexical TYPE token as a value
        // constructor here incorrectly turns an unresolved named call into a
        // proven error return and rejects its enclosing fn with E208.
        if (unresolved_system_spelling) {
            call->type = set_type_any(tp, ANY_CALL_RESULT);
        } else {
            call->type = target_type ? target_type :
                set_type_any(tp, ANY_CALL_RESULT);
        }
    }
    if (!info && arg_count == 1) {
        // Only unresolved calls can denote a type conversion. Builtins such
        // as type() use the shared TYPE_TYPE marker, not a TypeType payload.
        Type* conversion_target = ast_called_type_target(call->function);
        if (conversion_target && conversion_target->type_id == LMD_TYPE_NUM_SIZED) {
            NumSizedType num_type = type_num_sized_kind(conversion_target);
            int64_t const_value = 0;
            if (num_type != NUM_FLOAT16 && num_type != NUM_FLOAT32 &&
                    ast_constant_integer_value(tp, call->argument, &const_value) &&
                    !constant_fits_sized_integer(num_type, const_value)) {
                // Constant sized conversions reject overflow before runtime
                // truncation, matching the Go-style numeric contract.
                record_semantic_error_span(tp, span, ERR_INVALID_NUMBER,
                    "constant conversion to %s overflows", get_num_sized_type_name(num_type));
                call->type = &TYPE_ERROR;
            }
        }
    }
    if (user_method_found && user_method_is_proc) {
        if (!tp->current_scope || !tp->current_scope->is_proc) {
            AstIdentNode* field = effective && effective->node_type == AST_NODE_MEMBER_EXPR
                ? (AstIdentNode*)ast_unwrap_primary(((AstFieldNode*)effective)->field) : NULL;
            record_semantic_error_span(tp, span, ERR_PROC_IN_FN,
                "procedure method '%.*s' cannot be called in a function",
                field && field->name ? (int)field->name->len : 0,
                field && field->name ? field->name->chars : "");
            call->type = &TYPE_ERROR;
        } else {
            AstIdentNode* receiver_root = compound_root_ident(user_method_receiver);
            if (!receiver_root || !receiver_root->entry || !receiver_root->entry->is_mutable) {
                // A pn method writes its implicit receiver back on return;
                // reject a let root before that write can be lost.
                record_semantic_error_span(tp, span, ERR_IMMUTABLE_ASSIGNMENT,
                    "mutating method '%.*s' needs a `var` binding receiver",
                    (int)user_method_name.length, user_method_name.str);
                call->type = &TYPE_ERROR;
            }
            call->is_proc_method = true;
        }
    }
    if (!lambda_ast_validate_call_arguments(tp, call, span,
            lookup_arg_count + (method_call ? 1 : 0))) {
        call->type = &TYPE_ERROR;
    }
    // After resolution: the callee is an IDENT until the builder rewrites it
    // to the AST_NODE_SYS_FUNC, so this must run here, not on the inputs.
    validate_effect_polymorphic_call(tp, span, call->function, call->argument);
    return (AstNode*)call;
}

AstNode* build_raise_node_from_parts(Transpiler* tp, SourceSpan span,
        AstNode* value, bool statement_form) {
    AstRaiseNode* node = (AstRaiseNode*)alloc_ast_node_from_span(tp,
        statement_form ? AST_NODE_RAISE_STAM : AST_NODE_RAISE_EXPR,
        span, sizeof(AstRaiseNode));
    node->value = value;
    node->type = value && value->type ? value->type : &TYPE_ERROR;
    return (AstNode*)node;
}

AstNode* build_spread_node_from_parts(Transpiler* tp, SourceSpan span,
        AstNode* operand) {
    AstUnaryNode* node = (AstUnaryNode*)alloc_ast_node_from_span(tp,
        AST_NODE_SPREAD, span, sizeof(AstUnaryNode));
    node->operand = operand;
    node->op = OPERATOR_SPREAD;
    node->op_str = (StrView){"*", 1};
    node->type = operand && operand->type ? operand->type : &TYPE_ERROR;
    return (AstNode*)node;
}

AstNode* build_type_negation_from_parts(Transpiler* tp, SourceSpan span,
        AstNode* operand) {
    AstBinaryNode* node = (AstBinaryNode*)alloc_ast_node_from_span(tp,
        AST_NODE_BINARY_TYPE, span, sizeof(AstBinaryNode));
    TypeType* type_value = (TypeType*)alloc_type(tp->pool, LMD_TYPE_TYPE,
        sizeof(TypeType));
    TypeBinary* type = (TypeBinary*)alloc_type_kind(tp->pool,
        TYPE_KIND_BINARY, sizeof(TypeBinary));
    type_value->type = (Type*)type;
    AstPrimaryNode* any = (AstPrimaryNode*)alloc_ast_node_from_span(tp,
        AST_NODE_PRIMARY, span, sizeof(AstPrimaryNode));
    any->type = (Type*)alloc_type_kind(tp->pool, TYPE_KIND_SIMPLE, sizeof(Type));
    any->type->type_id = LMD_TYPE_TYPE;
    ((TypeType*)any->type)->type = set_type_any(tp, ANY_EXPLICIT);
    node->left = (AstNode*)any;
    node->right = operand;
    node->op = OPERATOR_EXCLUDE;
    node->op_str = (StrView){"!", 1};
    node->type = (Type*)type_value;
    type->left = any->type;
    type->right = operand && operand->type ? operand->type : &TYPE_ANY;
    type->op = OPERATOR_EXCLUDE;
    arraylist_append(tp->type_list, node->type);
    type->type_index = tp->type_list->length - 1;
    return (AstNode*)node;
}

AstNode* build_element_from_parts(Transpiler* tp, SourceSpan span,
        SourceSpan tag_span, AstNode* children) {
    StrView tag = source_span_text(tp, tag_span);
    if (tag.length >= 2 && tag.str[0] == '\'' &&
            tag.str[tag.length - 1] == '\'') {
        tag.str++;
        tag.length -= 2;
    }
    // Canonicalize whitespace around dotted tag names; preserve that
    // invariant for direct parsing so `<svg .rect>` and `<svg.rect>` share the
    // same element type/name (S2.4.3v2).
    bool has_tag_space = false;
    for (size_t i = 0; i < tag.length; i++) {
        char c = tag.str[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            has_tag_space = true;
            break;
        }
    }
    if (has_tag_space) {
        char* compact = (char*)pool_alloc(tp->pool, tag.length + 1);
        size_t compact_len = 0;
        for (size_t i = 0; i < tag.length; i++) {
            char c = tag.str[i];
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
                compact[compact_len++] = c;
            }
        }
        compact[compact_len] = '\0';
        tag = (StrView){compact, compact_len};
    }
    TypeObject* object_type = lookup_object_type_for_tag(tp, tag);
    if (object_type) {
        // Object-typed tags are object literals, not ordinary elements; keep
        // this decision shared with other element construction so `is Type` sees the same
        // runtime object contract (D2.2.2).
        return build_object_literal_from_items(tp, span, tag, object_type,
            children);
    }

    AstElementNode* node = (AstElementNode*)alloc_ast_node_from_span(tp,
        AST_NODE_ELEMENT, span, sizeof(AstElementNode));
    TypeElmt* type = (TypeElmt*)alloc_type(tp->pool, LMD_TYPE_ELEMENT,
        sizeof(TypeElmt));
    String* name = name_pool_create_strview(tp->name_pool, tag);
    type->name = (StrView){name->chars, name->len};
    node->type = (Type*)type;
    AstNode* prev = NULL;
    ShapeEntry* prev_shape = NULL;
    int byte_offset = 0;
    for (AstNode* raw = children; raw;) {
        AstNode* next = raw->next;
        raw->next = NULL;
        if (raw->node_type == AST_NODE_CONTENT) {
            node->content = raw;
            type->content_length = ((AstListNode*)raw)->list_type->length;
        } else {
            AstNode* candidate = raw;
            // Direct reductions carry the source spelling `ns.local` as one
            // key.  Reuse the legacy desugaring (`ns: {local: value}`) before
            // shape construction, including merge of repeated namespace keys.
            if (raw->node_type == AST_NODE_KEY_EXPR) {
                AstNamedNode* qualified = (AstNamedNode*)raw;
                if (qualified->name && qualified->name->len > 2) {
                    const char* dot = (const char*)memchr(qualified->name->chars,
                        '.', qualified->name->len);
                    if (dot && dot != qualified->name->chars &&
                            dot + 1 < qualified->name->chars + qualified->name->len) {
                        StrView prefix = {qualified->name->chars,
                            (size_t)(dot - qualified->name->chars)};
                        StrView local = {dot + 1,
                            (size_t)(qualified->name->chars + qualified->name->len - dot - 1)};
                        AstNamedNode* grouped = (AstNamedNode*)alloc_ast_node_from_span(tp,
                            AST_NODE_KEY_EXPR, raw->source_span, sizeof(AstNamedNode));
                        grouped->name = name_pool_create_strview(tp->name_pool, prefix);
                        grouped->as = build_ns_attr_map_from_parts(tp, local,
                            qualified->as, raw->source_span);
                        grouped->type = grouped->as ? grouped->as->type : &TYPE_ERROR;
                        AstNamedNode* existing = find_existing_named_item(node->item,
                            grouped->name);
                        if (existing && existing->as && grouped->as &&
                                existing->as->type->type_id == LMD_TYPE_MAP &&
                                grouped->as->type->type_id == LMD_TYPE_MAP) {
                            merge_ns_attr_maps(tp, existing->as, grouped->as);
                            existing->type = existing->as->type;
                            raw = next;
                            continue;
                        }
                        candidate = (AstNode*)grouped;
                    }
                }
            }
            bool spread = ast_node_is_syntactic_spread_key(tp, candidate);
            AstNode* item = spread ? ((AstNamedNode*)candidate)->as : candidate;
            if (item) {
                if (!node->item) node->item = item;
                else prev->next = item;
                prev = item;
                ShapeEntry* shape = build_map_shape_entry(tp, type, item, spread, false);
                shape->byte_offset = byte_offset;
                if (!prev_shape) type->shape = shape;
                else prev_shape->next = shape;
                prev_shape = shape;
                type->length++;
                byte_offset += spread ? (int)sizeof(void*) :
                    (int)type_info[type_field_storage_type_id(item->type)].byte_size;
            }
        }
        raw = next;
    }
    type->byte_size = byte_offset;
    arraylist_append(tp->type_list, type);
    type->type_index = tp->type_list->length - 1;
    return (AstNode*)node;
}

AstNamedNode* build_param_from_parts(Transpiler* tp, SourceSpan span,
        StrView name, AstNode* type_expr, AstNode* default_value,
        bool optional, bool is_var) {
    AstNamedNode* param = (AstNamedNode*)alloc_ast_node_from_span(tp,
        AST_NODE_PARAM, span, sizeof(AstNamedNode));
    param->name = name_pool_create_strview(tp->name_pool, name);
    param->as = default_value;
    TypeParam* param_type = (TypeParam*)alloc_type(tp->pool, LMD_TYPE_ANY,
        sizeof(TypeParam));
    Type* declared = type_expr ? type_expr->type : NULL;
    declared = unwrap_simple_type_type(declared);
    if (declared) {
        *(Type*)param_type = *declared;
        param_type->full_type = declared;
        param->declared_type = declared;
        set_param_contract(param_type,
            parameter_contract_for_declared(tp, declared, optional, default_value), true);
    } else if (default_value && default_value->type) {
        *(Type*)param_type = *default_value->type;
        param_type->full_type = NULL;
        set_param_contract(param_type, &TYPE_ANY_NO_ERROR, false);
    } else {
        *(Type*)param_type = TYPE_ANY;
        param_type->full_type = NULL;
        set_param_contract(param_type, &TYPE_ANY_NO_ERROR, false);
    }
    param_type->kind = TYPE_KIND_PARAM;
    param_type->is_optional = optional;
    param_type->is_var_param = is_var;
    param_type->default_value = default_value;
    param->type = (Type*)param_type;
    lambda_ast_register_name(tp, param);
    if (tp->current_scope && tp->current_scope->is_proc) {
        NameEntry* entry = lookup_name_in_current_scope(tp, param->name);
        if (entry) {
            entry->is_mutable = true;
            entry->is_var_param = is_var;
            // Record the pn-parameter fact itself rather than leaving consumers
            // to infer it from is_mutable (which a plain `var` local also sets).
            // Both tiers decide in-place-vs-detached container writes from it.
            entry->is_proc_param = true;
        }
    }
    return param;
}

static Type* direct_function_contract(AstNode* type_node) {
    if (!type_node || !type_node->type) return NULL;
    return unwrap_simple_type_type(type_node->type);
}

static AstNode* build_control_statement_from_parts(Transpiler* tp,
        SourceSpan span, LambdaReductionForm form, AstNode* value) {
    const char* subject = form == LAMBDA_REDUCTION_FORM_RETURN ? "`return`"
        : form == LAMBDA_REDUCTION_FORM_BREAK ? "`break`" : "`continue`";
    if (!tp->current_scope || !tp->current_scope->is_proc) {
        record_semantic_error_span(tp, span, ERR_PROC_IN_FN,
            "%s is only allowed inside a procedure (pn)", subject);
    }
    if (form == LAMBDA_REDUCTION_FORM_RETURN) {
        AstReturnNode* node = (AstReturnNode*)alloc_ast_node_from_span(tp,
            AST_NODE_RETURN_STAM, span, sizeof(AstReturnNode));
        node->value = value;
        node->type = value && value->type ? value->type : &TYPE_NULL;
        return (AstNode*)node;
    }
    AstNode* node = alloc_ast_node_from_span(tp,
        form == LAMBDA_REDUCTION_FORM_BREAK
            ? AST_NODE_BREAK_STAM : AST_NODE_CONTINUE_STAM,
        span, sizeof(AstNode));
    node->type = set_type_any(tp, ANY_STATEMENT);
    return node;
}

AstNamedNode* build_assignment_from_parts(Transpiler* tp, SourceSpan span,
        StrView name, AstNode* type_expr, AstNode* value) {
    AstNamedNode* assignment = (AstNamedNode*)alloc_ast_node_from_span(tp,
        AST_NODE_ASSIGN, span, sizeof(AstNamedNode));
    assignment->name = name_pool_create_strview(tp->name_pool, name);
    assignment->as = value;
    assignment->type = value && value->type ? value->type : &TYPE_ANY;

    Type* declared = type_expr ? direct_function_contract(type_expr) : NULL;
    bool invalid_annotation = declared && declared->type_id == LMD_TYPE_ERROR;
    if (declared && !invalid_annotation) {
        assignment->declared_type = declared;
        // Range annotations are membership contracts, not a storage lane.
        // Preserve the initializer's concrete type so a string range value is
        // not emitted as a raw Range pointer.
        assignment->type = declared->type_id == LMD_TYPE_RANGE && value &&
                value->type ? value->type : declared;
        int line = (int)lambda_source_span_start_point(tp->source, span).row + 1;
        check_declaration_static_boundary(tp, assignment, declared, line);
    }
    if (invalid_annotation && tp->static_warning && value && value->type) {
        // A downgraded unknown annotation is no storage contract. Retaining
        // TYPE_ERROR here makes MIR reinterpret the initializer and crash.
        assignment->type = value->type;
    }
    lambda_ast_register_name(tp, assignment);
    return assignment;
}

static void direct_validate_mutable_compound(Transpiler* tp,
        SourceSpan span, AstNode* object) {
    AstIdentNode* root = compound_root_ident(object);
    if (!root || !root->entry || root->entry->is_mutable) return;
    record_semantic_error_span(tp, span, ERR_IMMUTABLE_ASSIGNMENT,
        "cannot mutate through immutable binding '%.*s'. declare it with `var` or pass it as `var`.",
        (int)root->name->len, root->name->chars);
}

AstNode* build_assignment_statement_from_parts(Transpiler* tp,
        SourceSpan span, AstNode* target, AstNode* value) {
    if (!tp->current_scope || !tp->current_scope->is_proc) {
        record_semantic_error_span(tp, span, ERR_PROC_IN_FN,
            "assignment is only allowed inside a procedure (pn)");
        return NULL;
    }
    target = unwrap_primary_node(target);
    if (!target) return NULL;
    if (target->node_type == AST_NODE_INDEX_EXPR ||
            target->node_type == AST_NODE_MEMBER_EXPR) {
        AstFieldNode* field = (AstFieldNode*)target;
        AstCompoundAssignNode* assignment = (AstCompoundAssignNode*)
            alloc_ast_node_from_span(tp,
                target->node_type == AST_NODE_INDEX_EXPR
                    ? AST_NODE_INDEX_ASSIGN_STAM : AST_NODE_MEMBER_ASSIGN_STAM,
                span, sizeof(AstCompoundAssignNode));
        assignment->object = field->object;
        assignment->key = field->field;
        assignment->value = value;
        assignment->type = value && value->type ? value->type : &TYPE_ANY;
        assignment->op = OPERATOR_ASSIGN;
        assignment->left = target;
        assignment->right = value;
        direct_validate_mutable_compound(tp, span, field->object);
        check_compound_assignment_static_boundary(tp, span, target, value,
            target->node_type == AST_NODE_INDEX_EXPR ? "array element" : "map member");
        return (AstNode*)assignment;
    }
    if (target->node_type != AST_NODE_IDENT) return NULL;

    AstIdentNode* ident = (AstIdentNode*)target;
    NameEntry* entry = ident->entry ? ident->entry : lookup_name(tp,
        strview_init(ident->name->chars, ident->name->len));
    AstAssignStamNode* assignment = (AstAssignStamNode*)alloc_ast_node_from_span(
        tp, AST_NODE_ASSIGN_STAM, span, sizeof(AstAssignStamNode));
    assignment->target = ident->name;
    assignment->target_node = entry ? entry->node : NULL;
    assignment->target_entry = entry;
    assignment->value = value;
    assignment->type = value && value->type ? value->type : &TYPE_ANY;
    assignment->op = OPERATOR_ASSIGN;
    assignment->left = target;
    assignment->right = value;
    bool is_object_field_in_proc = entry && entry->node &&
        entry->node->node_type == AST_NODE_KEY_EXPR &&
        tp->current_scope && tp->current_scope->is_proc;
    if (entry && !entry->is_mutable && !is_object_field_in_proc) {
        // Object fields are the receiver's mutable storage lane inside a pn
        // method; treating their scope entries as let bindings rejects the
        // same bare-field assignment accepted by object methods (D2.2.2).
        record_semantic_error_span(tp, span, ERR_IMMUTABLE_ASSIGNMENT,
            "cannot assign to let binding '%.*s'. declare it with `var` instead.",
            (int)ident->name->len, ident->name->chars);
    }
    if (entry && entry->is_mutable && assignment->value && assignment->value->type &&
            entry->node && entry->node->type && !entry->has_type_annotation &&
            entry->node->type->type_id != assignment->value->type->type_id &&
            !entry->type_widened && entry->node->type->type_id != LMD_TYPE_ANY) {
        // Keep the direct binding metadata consistent:
        // an inferred var that changes type must use the Item carrier on all
        // later reads, rather than reinterpreting its new value through the
        // initializer's native lane (D2.2.2).
        entry->type_widened = true;
    }
    return (AstNode*)assignment;
}

AstNode* build_function_from_parts(Transpiler* tp, SourceSpan span,
        StrView name, AstNode* params, AstNode* returned, AstNode* error_type,
        AstNode* body, bool is_proc, bool variadic, bool raised) {
    AstFuncNode* fn = build_function_placeholder_from_parts(tp, span, name,
        is_proc);
    TypeFunc* function_type = (TypeFunc*)fn->type;
    NameScope* parent = tp->current_scope;
    NameScope* function_scope = lambda_ast_enter_scope(tp, is_proc);
    fn->vars = function_scope;
    AstNamedNode* previous = NULL;
    for (AstNamedNode* param = (AstNamedNode*)params; param;
            param = (AstNamedNode*)param->next) {
        direct_move_binding(parent, function_scope, (AstNode*)param);
        if (!previous) fn->param = param;
        else previous->next = (AstNode*)param;
        previous = param;
        TypeParam* param_type = (TypeParam*)param->type;
        if (!function_type->param) function_type->param = param_type;
        else {
            TypeParam* tail = function_type->param;
            while (tail->next) tail = tail->next;
            tail->next = param_type;
        }
        function_type->param_count++;
        if (!param_type->is_optional) function_type->required_param_count++;
    }
    fn->body = body;
    function_type->is_variadic = variadic;
    function_type->can_raise = raised;
    Type* returned_type = direct_function_contract(returned);
    Type* error = direct_function_contract(error_type);
    if (raised && !error) error = &TYPE_ERROR;
    if (returned_type) {
        function_type->returned = returned_type;
        function_type->inferred_return = returned_type;
        set_function_return_contract(function_type, returned_type, true);
    }
    if (error) {
        function_type->error_type = error;
        function_type->can_raise = true;
    }
    if (!returned_type) {
        function_type->inferred_return = is_proc
            ? infer_procedural_return_type(tp, fn)
            : body && body->type ? body->type : &TYPE_ANY;
        // The forward placeholder is the dynamic ABI seen by earlier calls;
        // only inferred_return narrows after the completed body is available.
        function_type->returned = &TYPE_ANY;
    }
    lambda_ast_leave_scope(tp, function_scope);
    if (name.length) lambda_ast_register_name(tp, (AstNamedNode*)fn);
    return (AstNode*)fn;
}

static AstNode* direct_complete_function(Transpiler* tp, SourceSpan span,
        AstFuncNode* fn, NameScope* function_scope, AstNode* params,
        AstNode* returned, AstNode* error_type, AstNode* body,
        bool is_proc, bool variadic, bool raised) {
    if (!fn || !function_scope) return NULL;
    fn->vars = function_scope;
    fn->param = (AstNamedNode*)params;
    if (body && body->node_type == AST_NODE_CONTENT) {
        AstListNode* content = (AstListNode*)body;
        if (content->item && !content->item->next) {
            // `build_content(..., true, ...)` collapses a one-item function
            // block. Preserve that AST contract so a
            // terminal assignment remains a procedure's implicit result
            // instead of being discarded as a CONTENT side effect (D6.1.2).
            body = content->item;
        }
    }
    fn->body = body;
    TypeFunc* function_type = (TypeFunc*)fn->type;
    function_type->is_proc = is_proc;
    function_type->is_variadic = variadic;
    function_type->can_raise = raised;
    function_type->param = NULL;
    function_type->param_count = 0;
    function_type->required_param_count = 0;
    for (AstNamedNode* param = (AstNamedNode*)params; param;
            param = (AstNamedNode*)param->next) {
        TypeParam* param_type = (TypeParam*)param->type;
        if (!function_type->param) function_type->param = param_type;
        else {
            TypeParam* tail = function_type->param;
            while (tail->next) tail = tail->next;
            tail->next = param_type;
        }
        function_type->param_count++;
        if (!param_type->is_optional) function_type->required_param_count++;
    }
    Type* returned_type = direct_function_contract(returned);
    Type* error = direct_function_contract(error_type);
    if (raised && !error) error = &TYPE_ERROR;
    if (returned_type) {
        function_type->returned = returned_type;
        function_type->inferred_return = returned_type;
        set_function_return_contract(function_type, returned_type, true);
    }
    if (error) {
        function_type->error_type = error;
        function_type->can_raise = true;
    }
    // Keep the inferred body result separate from its declared contract.
    // Otherwise the direct path cannot diagnose a mismatched return or an
    // implicit error escape after a completed function body is available.
    function_type->inferred_return = is_proc
        ? infer_procedural_return_type(tp, fn)
        : body && body->type ? body->type : &TYPE_ANY;
    (void)validate_lambda_argument_limit(tp, span,
        function_type->param_count + (function_type->is_variadic ? 1 : 0),
        "function formal");
    if (!returned_type) {
        // Earlier calls can still target this placeholder through the boxed
        // ABI; retain that stable public carrier while MIR uses inferred_return.
        function_type->returned = &TYPE_ANY;
    }
    validate_function_return_contract(tp, fn, function_type);
    return (AstNode*)fn;
}

static AstNode* build_module_import_from_parts(Transpiler* tp,
        SourceSpan span, StrView alias_view, StrView module) {
    AstImportNode* node = (AstImportNode*)alloc_ast_node_from_span(tp,
        AST_NODE_IMPORT, span, sizeof(AstImportNode));
    node->type = &TYPE_NULL;
    node->module = module;
    node->alias = alias_view.length
        ? name_pool_create_strview(tp->name_pool, alias_view) : NULL;
    if (!module.length) return (AstNode*)node;
    if (strview_equal(&module, "math")) {
        if (node->alias) tp->builtin_alias_math = node->alias;
        else tp->builtin_import_math = true;
        return alloc_ast_node_from_span(tp, AST_NODE_NULL, span, sizeof(AstNode));
    }
    if (strview_equal(&module, "io")) {
        if (node->alias) tp->builtin_alias_io = node->alias;
        else tp->builtin_import_io = true;
        return alloc_ast_node_from_span(tp, AST_NODE_NULL, span, sizeof(AstNode));
    }
#ifndef SIMPLE_SCHEMA_PARSER
    char module_buf[128];
    if (module.length < sizeof(module_buf)) {
        memcpy(module_buf, module.str, module.length);
        module_buf[module.length] = '\0';
        jube_register_builtin_modules();
        const JubeModuleDef* jube = jube_find_static_module(module_buf);
        if (jube) {
            add_jube_module_import(tp,
                name_pool_create_strview(tp->name_pool,
                    strview_from_cstr(jube->name)), node->alias);
            return alloc_ast_node_from_span(tp, AST_NODE_NULL, span, sizeof(AstNode));
        }
    }
#endif
    if (module.str[0] == '\'' && module.length > 1) {
        if (node->alias) {
            Target* target = (Target*)pool_calloc(tp->pool, sizeof(Target));
            StrView uri = {module.str + 1, module.length - 2};
            String* uri_string = name_pool_create_strview(tp->name_pool, uri);
            target->original = uri_string->chars;
            add_namespace(tp, node->alias, target);
        }
        return alloc_ast_node_from_span(tp, AST_NODE_NULL, span, sizeof(AstNode));
    }
    StrBuf* path = strbuf_new();
    bool relative = module.str[0] == '.';
    if (relative) {
        const char* base = tp->directory ? tp->directory : "./";
        strbuf_append_format(path, "%s%.*s", base,
            (int)module.length - 1, module.str + 1);
        for (char* ch = path->str; *ch; ch++) if (*ch == '.') *ch = '/';
    } else {
        strbuf_append_format(path, "./%.*s", (int)module.length, module.str);
        for (char* ch = path->str + 2; *ch; ch++) if (*ch == '.') *ch = '/';
        char* slash = strchr(path->str + 2, '/');
        if (slash) {
            StrBuf* fixed = strbuf_new();
            const char* home = g_lambda_home;
            if (home[0] == '.' && home[1] == '/') home += 2;
            strbuf_append_str(fixed, "./");
            strbuf_append_str(fixed, home);
            strbuf_append_str(fixed, slash);
            strbuf_free(path);
            path = fixed;
        }
    }
    strbuf_append_str(path, ".ls");
    node->is_relative = relative;
    bool lambda_source_exists = file_exists(path->str);
    bool imported = false;
    node->script = load_script(tp->runtime, path->str, NULL, true);
    if (node->script && node->script->ast_root) {
        declare_module_import(tp, node);
        imported = true;
    } else if (!lambda_source_exists) {
        // A missing Lambda module may resolve to a hosted JavaScript module.
        path->str[path->length - 2] = 'j';
        path->str[path->length - 1] = 's';
        Item namespace_obj = load_js_module(tp->runtime, path->str);
        if (namespace_obj.item != ItemNull.item) {
            node->script = (Script*)create_module_import_script(
                path->str, namespace_obj, tp->runtime);
            node->is_cross_lang = node->script != NULL;
            if (node->script) {
                declare_module_import(tp, node);
                imported = true;
            }
        }
    }
    if (!imported) {
        record_semantic_error_span(tp, span, ERR_IMPORT_ERROR,
            "failed to import Lambda module '%.*s' (resolved: %s)",
            (int)module.length, module.str, path->str);
    }
    strbuf_free(path);
    return (AstNode*)node;
}

// S16.6.8/S16.6.9 branch classification, by INTERIOR on the S12.1 boundary.
//
// A block is CONTROL when its top level holds a pn-only statement, or holds a
// nested `if`/`match` that is itself CONTROL — the recursion matters: the outer
// then-branch of a nested if/else contains only an IF node, so a top-level-only
// scan called it a value branch and rejected working procedural code.
// An EMPTY block is NEUTRAL and pairs with either side.
AstBranchKind ast_branch_kind(AstNode* node) {
    if (!node) return AST_BRANCH_NEUTRAL;
    switch (node->node_type) {
    case AST_NODE_CONTENT: {
        AstBranchKind kind = AST_BRANCH_NEUTRAL;
        for (AstNode* item = ((AstListNode*)node)->item; item; item = item->next) {
            if (is_procedural_only_stam(item->node_type)) return AST_BRANCH_CONTROL;
            if (ast_branch_kind(item) == AST_BRANCH_CONTROL) return AST_BRANCH_CONTROL;
            kind = AST_BRANCH_VALUE;
        }
        return kind;  // NEUTRAL only when the block is empty
    }
    case AST_NODE_IF_EXPR: {
        AstIfNode* n = (AstIfNode*)node;
        if (ast_branch_kind(n->then) == AST_BRANCH_CONTROL ||
                ast_branch_kind(n->otherwise) == AST_BRANCH_CONTROL) {
            return AST_BRANCH_CONTROL;
        }
        return AST_BRANCH_VALUE;
    }
    case AST_NODE_MATCH_EXPR: {
        for (AstMatchArm* arm = ((AstMatchNode*)node)->first_arm; arm;
                arm = (AstMatchArm*)arm->next) {
            if (ast_branch_kind(arm->body) == AST_BRANCH_CONTROL) {
                return AST_BRANCH_CONTROL;
            }
        }
        return AST_BRANCH_VALUE;
    }
    default:
        return is_procedural_only_stam(node->node_type)
            ? AST_BRANCH_CONTROL : AST_BRANCH_VALUE;
    }
}

// S16.6.8: a procedural block is a statement and may not stand where a value is
// required. Reported at the operand rather than the enclosing construct, since
// that is where the repair goes: bind the block's effect first, then use the
// name. Functional blocks and maps are unaffected — `ast_block_is_procedural`
// classifies by interior.
static void reject_procedural_block_operand(Transpiler* tp, AstNode* operand,
        const char* position) {
    if (!ast_block_is_procedural(operand)) return;
    record_semantic_error_span(tp, operand->source_span, ERR_INVALID_OPERATION,
        "a statement block is not an expression and cannot appear as %s; run it "
        "as its own statement and bind the result first", position);
}

// S16.6.9 for `if`/`else`. Classification is by INTERIOR on the S12.1 boundary,
// not by brace shape: a braced-but-functional else is a value branch, which is
// what keeps the documented block-else idiom
// (`if (x > 0) "ok" else { let r = diagnose(x); ... }`) legal. An absent else
// contributes nothing, so a lone `if (c) { return x }` stays a control form.
// A chained `else if` is itself an IF node and is validated on its own.
static void validate_if_branch_homogeneity(Transpiler* tp, SourceSpan span,
        AstNode* then_branch, AstNode* else_branch) {
    if (!else_branch) return;
    AstBranchKind then_kind = ast_branch_kind(then_branch);
    AstNode* tail = ast_unwrap_primary(else_branch);
    // An `else if` chain defers to that node's own check rather than being
    // classified here, where its branches are not visible.
    if (tail && tail->node_type == AST_NODE_IF_EXPR) return;
    AstBranchKind else_kind = ast_branch_kind(else_branch);
    // NEUTRAL is the empty branch: it commits to neither and pairs with both.
    if (then_kind == AST_BRANCH_NEUTRAL || else_kind == AST_BRANCH_NEUTRAL) return;
    if (then_kind == else_kind) return;
    record_semantic_error_span(tp, span, ERR_INVALID_OPERATION,
        "if branches must be all value or all control; one branch is a "
        "statement block while the other yields a value - lift the statement "
        "branch out (`if (c) { ... }` on its own, then bind the value)");
}

AstNode* build_if_node_from_parts(Transpiler* tp, SourceSpan span,
        AstNode* condition, AstNode* then_branch, AstNode* else_branch) {
    AstIfNode* node = (AstIfNode*)alloc_ast_node_from_span(tp,
        AST_NODE_IF_EXPR, span, sizeof(AstIfNode));
    node->cond = condition;
    node->then = then_branch;
    node->otherwise = else_branch;
    // Direct reductions have source spans. Route them through the
    // shared condition lint so parser choice cannot suppress diagnostics.
    lint_condition_span(tp, span, condition, "if");
    if (!then_branch || !then_branch->type ||
            (else_branch && !else_branch->type)) {
        node->type = &TYPE_ERROR;
        return (AstNode*)node;
    }
    // Keep ordinary mixed joins widened through the shared rule so later
    // boundary validation observes the same function return contracts.
    node->type = infer_if_result_type(tp, then_branch, else_branch);
    validate_if_branch_homogeneity(tp, span, then_branch, else_branch);
    return (AstNode*)node;
}

static Type* direct_range_element_type(Transpiler* tp, AstNode* source) {
    AstNode* value = ast_unwrap_primary(source);
    if (value && value->node_type == AST_NODE_IDENT) {
        AstIdentNode* ident = (AstIdentNode*)value;
        if (ident->entry && ident->entry->node &&
                ident->entry->node->node_type == AST_NODE_ASSIGN) {
            value = ast_unwrap_primary(((AstNamedNode*)ident->entry->node)->as);
        }
    }
    if (value && value->node_type == AST_NODE_BINARY &&
            ((AstBinaryNode*)value)->op == OPERATOR_TO) {
        AstBinaryNode* range = (AstBinaryNode*)value;
        TypeId left = range->left && range->left->type
            ? range->left->type->type_id : LMD_TYPE_ANY;
        TypeId right = range->right && range->right->type
            ? range->right->type->type_id : LMD_TYPE_ANY;
        if (left == LMD_TYPE_STRING && right == LMD_TYPE_STRING) {
            // String ranges iterate codepoint strings; treating every range as
            // an int unboxes each Item result and turns characters into zero
            // (S7.1, D2.2.2).
            return &TYPE_STRING;
        }
    }
    return &TYPE_INT;
}

static Type* direct_loop_value_type(Transpiler* tp, AstNode* source,
        bool key_only) {
    if (key_only || !source || !source->type) {
        return set_type_any(tp, ANY_LOOP_SRC);
    }
    Type* source_type = source->type;
    if (source_type->type_id == LMD_TYPE_ARRAY_NUM ||
            source_type->type_id == LMD_TYPE_ARRAY) {
        TypeArray* array = !is_global_simple_type(source_type)
            ? (TypeArray*)source_type : NULL;
        if (array && array->nested && (uintptr_t)array->nested > 0x1000) {
            return array->nested;
        }
        return set_type_any(tp, ANY_LOOP_SRC);
    }
    if (source_type->type_id == LMD_TYPE_RANGE) {
        return direct_range_element_type(tp, source);
    }
    if (source_type->type_id == LMD_TYPE_BINARY) return &TYPE_U8;
    return set_type_any(tp, ANY_LOOP_SRC);
}

// The direct reduction builder constructs a join predicate before the binding
// reduction installs its new loop name. Reattach only unresolved identifiers
// for that binding; otherwise T0 evaluates `c.id`/`r.id` as ItemError while MIR
// still resolves the same source name during lowering (D7.2.1/D8.1.1v2).
static void direct_rebind_join_ident(AstNode* node, String* name,
        NameEntry* entry) {
    if (!node || !name || !entry) return;
    switch (node->node_type) {
    case AST_NODE_IDENT: {
        AstIdentNode* ident = (AstIdentNode*)node;
        if (!ident->entry && ident->name && ident->name->len == name->len &&
                memcmp(ident->name->chars, name->chars, name->len) == 0) {
            ident->entry = entry;
            if (entry->node && entry->node->type) ident->type = entry->node->type;
        }
        return;
    }
    case AST_NODE_PRIMARY:
        direct_rebind_join_ident(((AstPrimaryNode*)node)->expr, name, entry);
        return;
    case AST_NODE_BINARY:
    case AST_NODE_PIPE: {
        AstBinaryNode* binary = (AstBinaryNode*)node;
        direct_rebind_join_ident(binary->left, name, entry);
        direct_rebind_join_ident(binary->right, name, entry);
        return;
    }
    case AST_NODE_UNARY:
    case AST_NODE_SPREAD:
        direct_rebind_join_ident(((AstUnaryNode*)node)->operand, name, entry);
        return;
    case AST_NODE_MEMBER_EXPR: {
        // Dotted field names are keys, not bindings; only the object side can
        // refer to the just-installed loop variable.
        direct_rebind_join_ident(((AstFieldNode*)node)->object, name, entry);
        return;
    }
    case AST_NODE_INDEX_EXPR: {
        AstFieldNode* field = (AstFieldNode*)node;
        direct_rebind_join_ident(field->object, name, entry);
        if (!field->field || field->field->node_type != AST_NODE_IDENT)
            direct_rebind_join_ident(field->field, name, entry);
        return;
    }
    case AST_NODE_CALL_EXPR: {
        AstCallNode* call = (AstCallNode*)node;
        direct_rebind_join_ident(call->function, name, entry);
        for (AstNode* arg = call->argument; arg; arg = arg->next)
            direct_rebind_join_ident(arg, name, entry);
        return;
    }
    case AST_NODE_IF_EXPR: {
        AstIfNode* branch = (AstIfNode*)node;
        direct_rebind_join_ident(branch->cond, name, entry);
        direct_rebind_join_ident(branch->then, name, entry);
        direct_rebind_join_ident(branch->otherwise, name, entry);
        return;
    }
    default:
        return;
    }
}

AstNode* build_loop_from_parts(Transpiler* tp, SourceSpan span,
        LambdaToken name_token, LambdaToken index_token, uint32_t flags,
        AstNode* index_type, AstNode* source, AstNode* join) {
    AstLoopNode* loop = (AstLoopNode*)alloc_ast_node_from_span(tp,
        AST_NODE_LOOP, span, sizeof(AstLoopNode));
    StrView name = source_span_text(tp, name_token.span);
    loop->name = name_pool_create_strview(tp->name_pool, name);
    loop->index_name = index_token.kind
        ? name_pool_create_strview(tp->name_pool,
            source_span_text(tp, index_token.span)) : NULL;
    loop->as = source;
    loop->on = join;
    loop->key_filter = (flags & LAMBDA_REDUCTION_FLAG_KEY_ONLY)
        ? LOOP_KEY_SYMBOL : LOOP_KEY_ALL;
    // S8.1.3: the AXIS (`in`/`at`) selects which members are walked — that is
    // `key_filter` above — while the ARITY selects the projection. `for (k at c)`
    // binds the key, but the paired `for (k, v at c)` walks the same name-keyed
    // members and binds (key, value): `index_name` is the key slot and `name` is
    // the value slot by construction, so key_only must not also redirect `name`
    // to the key. Leaving it set bound BOTH names to the key (LR02-8).
    loop->key_only = (flags & LAMBDA_REDUCTION_FLAG_KEY_ONLY) != 0 &&
        !loop->index_name;
    loop->optional = (flags & LAMBDA_REDUCTION_FLAG_OPTIONAL) != 0;
    loop->type = direct_loop_value_type(tp, source, loop->key_only);

    Type* key_contract = index_type ? direct_function_contract(index_type) : NULL;
    if (loop->index_name && key_contract) {
        if (key_contract->type_id == LMD_TYPE_INT) loop->key_filter = LOOP_KEY_INT;
        else if (key_contract->type_id == LMD_TYPE_SYMBOL) loop->key_filter = LOOP_KEY_SYMBOL;
        else record_semantic_error_span(tp, index_type->source_span,
            ERR_INVALID_OPERATION, "for index type must be int or symbol");
    }
    if (loop->index_name) {
        AstNamedNode* index = (AstNamedNode*)alloc_ast_node_from_span(tp,
            AST_NODE_LOOP, index_token.span, sizeof(AstNamedNode));
        index->name = loop->index_name;
        index->type = loop->key_filter == LOOP_KEY_INT ? &TYPE_INT :
            loop->key_filter == LOOP_KEY_SYMBOL ? &TYPE_SYMBOL :
            set_type_any(tp, ANY_LOOP_SRC);
        lambda_ast_register_name(tp, index);
    }
    lambda_ast_register_name(tp, (AstNamedNode*)loop);
    if (join) {
        NameEntry* loop_entry = lookup_name_in_current_scope(tp, loop->name);
        direct_rebind_join_ident(join, loop->name, loop_entry);
        build_join_key_specs(tp, loop, join);
    }
    if (loop->optional && !join) {
        record_semantic_error_span(tp, span, ERR_INVALID_OPERATION,
            "optional for binding requires an `on` condition");
    }
    return (AstNode*)loop;
}

static void direct_append_clause(AstNode** first, AstNode* item) {
    if (!item) return;
    item->next = NULL;
    if (!*first) {
        *first = item;
        return;
    }
    AstNode* tail = *first;
    while (tail->next) tail = tail->next;
    tail->next = item;
}

AstNode* build_for_from_parts(Transpiler* tp, SourceSpan span,
        AstNode* clauses, AstNode* body, NameScope* loop_scope,
        bool statement_form) {
    (void)clauses;
    AstForNode* node = (AstForNode*)alloc_ast_node_from_span(tp,
        statement_form ? AST_NODE_FOR_STAM : AST_NODE_FOR_EXPR, span,
        sizeof(AstForNode));
    node->vars = loop_scope;
    node->then = body;
    if (body && body->node_type == AST_NODE_CONTENT &&
            !((AstListNode*)body)->vars) {
        ((AstListNode*)body)->vars = loop_scope;
    }
    node->type = statement_form ? set_type_any(tp, ANY_STATEMENT) :
        set_type_any(tp, ANY_LIST);
    return (AstNode*)node;
}

AstNode* build_while_from_parts(Transpiler* tp, SourceSpan span,
        AstNode* condition, AstNode* body, NameScope* loop_scope) {
    AstWhileNode* node = (AstWhileNode*)alloc_ast_node_from_span(tp,
        AST_NODE_WHILE_STAM, span, sizeof(AstWhileNode));
    node->vars = loop_scope;
    node->cond = condition;
    node->body = body;
    if (!tp->current_scope || !tp->current_scope->is_proc) {
        // A loop scope inherits procedure capability; it must not manufacture
        // one for a top-level `while` before this source guard runs.
        record_semantic_error_span(tp, span, ERR_PROC_IN_FN,
            "`while` is only allowed inside a procedure (pn)");
    }
    // Keep procedural conditions subject to the mask/container lint; this
    // guards truthy-array control flow (S5.4.1).
    lint_condition_span(tp, span, condition, "while");
    if (body && body->node_type == AST_NODE_CONTENT &&
            !((AstListNode*)body)->vars) {
        ((AstListNode*)body)->vars = loop_scope;
    }
    node->type = set_type_any(tp, ANY_STATEMENT);
    return (AstNode*)node;
}

AstNode* build_propagate_node_from_parts(Transpiler* tp, SourceSpan span,
        AstNode* operand) {
    AstNode* effective = operand;
    while (effective && effective->node_type == AST_NODE_PRIMARY) {
        effective = ((AstPrimaryNode*)effective)->expr;
    }
    bool may_error = operand && operand->type &&
        lambda_type_accepts_error(operand->type);
    if (effective && effective->node_type == AST_NODE_CALL_EXPR) {
        AstCallNode* call = (AstCallNode*)effective;
        call->propagate = true;
        may_error = may_error || call->can_raise;
        if (call->can_raise && call->function && call->function->type &&
                call->function->type->type_id == LMD_TYPE_FUNC) {
            Type* success = function_success_result_type((TypeFunc*)call->function->type);
            if (success) call->type = success;
        }
    }
    if (!may_error) {
        record_semantic_error_span(tp, span, ERR_SEMANTIC_ERROR,
            "postfix `^` used on an expression that does not return errors");
    }
    Type* success = operand && operand->type
        ? lambda_type_remove_error(tp->pool, operand->type) : NULL;
    if (!success) success = set_type_any(tp, ANY_ERROR_RECOVERY);
    if (effective && effective->node_type == AST_NODE_CALL_EXPR) {
        operand->type = success;
        effective->type = success;
        return effective;
    }
    AstUnaryNode* node = (AstUnaryNode*)alloc_ast_node_from_span(tp,
        AST_NODE_UNARY, span, sizeof(AstUnaryNode));
    node->operand = operand;
    node->op = OPERATOR_PROPAGATE;
    node->prefix = false;
    node->type = success;
    node->op_str = source_span_text(tp, span);
    return (AstNode*)node;
}

static LambdaParseValue direct_ast_reduce(void* context,
        const LambdaParseReduction* reduction) {
    LambdaDirectAstSink* sink = (LambdaDirectAstSink*)context;
    if (!sink || !reduction || sink->failed) return 0;
    Transpiler* tp = sink->tp;
    AstNode* child0 = reduction->child_count > 0
        ? direct_ast_node(reduction->children[0]) : NULL;

    if (reduction->kind == LAMBDA_REDUCE_CONTEXT) {
        if (reduction->form == LAMBDA_REDUCTION_FORM_VIEW_BEGIN) {
            if (!direct_view_begin(sink, reduction)) sink->failed = true;
            return 0;
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_VIEW_END) {
            if (!direct_view_end(sink)) sink->failed = true;
            return 0;
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_VIEW_HANDLER_BEGIN) {
            if (!direct_view_begin_handler(sink, reduction)) sink->failed = true;
            return 0;
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_VIEW_HANDLER_END) {
            if (!direct_view_end_handler(sink)) sink->failed = true;
            return 0;
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_HANDLER_BEGIN) {
            if (sink->handler_context_depth >= 64) {
                log_error("direct sink handler context overflow");
                sink->failed = true;
                return 0;
            }
            sink->handler_context[sink->handler_context_depth++] =
                tp->building_handler_body;
            tp->building_handler_body = true;
            return 0;
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_HANDLER_END) {
            if (!sink->handler_context_depth) {
                log_error("direct sink handler context underflow");
                sink->failed = true;
                return 0;
            }
            tp->building_handler_body =
                sink->handler_context[--sink->handler_context_depth];
            return 0;
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_THAT_BEGIN) {
            if (sink->that_context_depth >= 64) {
                log_error("direct sink that context overflow");
                sink->failed = true;
                return 0;
            }
            sink->that_context[sink->that_context_depth++] = tp->in_that_clause;
            tp->in_that_clause = true;
            return 0;
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_THAT_END) {
            if (!sink->that_context_depth) {
                log_error("direct sink that context underflow");
                sink->failed = true;
                return 0;
            }
            tp->in_that_clause =
                sink->that_context[--sink->that_context_depth];
            return 0;
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_FOR_BEGIN ||
                reduction->form == LAMBDA_REDUCTION_FORM_WHILE_BEGIN) {
            if (sink->loop_scope_depth >= 64) {
                log_error("direct sink loop scope overflow");
                sink->failed = true;
                return 0;
            }
            bool is_while = reduction->form == LAMBDA_REDUCTION_FORM_WHILE_BEGIN;
            sink->loop_scopes[sink->loop_scope_depth] = lambda_ast_enter_scope(tp,
                tp->current_scope && tp->current_scope->is_proc);
            sink->for_nodes[sink->loop_scope_depth] = NULL;
            if (!is_while) {
                sink->for_nodes[sink->loop_scope_depth] =
                    (AstForNode*)alloc_ast_node_from_span(tp, AST_NODE_FOR_EXPR,
                        reduction->span, sizeof(AstForNode));
                sink->for_nodes[sink->loop_scope_depth]->vars =
                    sink->loop_scopes[sink->loop_scope_depth];
            }
            sink->loop_scope_depth++;
            return 0;
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_FOR_END ||
                reduction->form == LAMBDA_REDUCTION_FORM_WHILE_END) {
            if (!sink->loop_scope_depth) {
                log_error("direct sink loop scope underflow");
                sink->failed = true;
                return 0;
            }
            uint32_t slot = --sink->loop_scope_depth;
            NameScope* scope = sink->loop_scopes[slot];
            AstForNode* loop = sink->for_nodes[slot];
            if (loop && loop->group) {
                // `group by` commits the row scope and replaces it with an
                // aggregate scope. Closing the saved row scope leaves that
                // child active and shifts every following binding (D2.2.2).
                scope = tp->current_scope;
            }
            lambda_ast_leave_scope(tp, scope);
            return 0;
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_GROUP_BEGIN) {
            if (sink->group_scope_depth >= 64) {
                log_error("direct sink group scope overflow");
                sink->failed = true;
                return 0;
            }
            sink->group_scopes[sink->group_scope_depth++] =
                lambda_ast_enter_scope(tp, tp->current_scope &&
                    tp->current_scope->is_proc);
            return 0;
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_GROUP_END) {
            if (!sink->group_scope_depth) {
                log_error("direct sink group scope underflow");
                sink->failed = true;
                return 0;
            }
            sink->completed_group_scope =
                sink->group_scopes[--sink->group_scope_depth];
            lambda_ast_leave_scope(tp, sink->completed_group_scope);
            return 0;
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_IF_BRANCH_BEGIN ||
                reduction->form == LAMBDA_REDUCTION_FORM_MATCH_ARM_BEGIN) {
            if (sink->branch_scope_depth >= 64) {
                log_error("direct sink branch scope overflow");
                sink->failed = true;
                return 0;
            }
            sink->branch_scopes[sink->branch_scope_depth++] = lambda_ast_enter_scope(tp,
                tp->current_scope && tp->current_scope->is_proc);
            return 0;
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_IF_BRANCH_END ||
                reduction->form == LAMBDA_REDUCTION_FORM_MATCH_ARM_END) {
            if (!sink->branch_scope_depth) {
                log_error("direct sink branch scope underflow");
                sink->failed = true;
                return 0;
            }
            NameScope* scope = sink->branch_scopes[--sink->branch_scope_depth];
            lambda_ast_leave_scope(tp, scope);
            return 0;
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_FUNCTION_BEGIN) {
            if (sink->function_depth >= 64) {
                sink->failed = true;
                return 0;
            }
            StrView name = direct_token_text(tp, reduction->secondary_token);
            bool is_proc = (reduction->flags & LAMBDA_REDUCTION_FLAG_PROC) != 0;
            AstFuncNode* fn = NULL;
            if (name.length && !sink->type_object_depth) {
                String* pooled_name = name_pool_create_strview(tp->name_pool, name);
                NameEntry* existing = lookup_name_in_current_scope(tp,
                    pooled_name);
                if (existing && existing->node &&
                        existing->node->source_span.start_byte ==
                            reduction->span.start_byte &&
                        (existing->node->node_type == AST_NODE_FUNC ||
                         existing->node->node_type == AST_NODE_PROC)) {
                    fn = (AstFuncNode*)existing->node;
                }
            }
            if (!fn) {
                fn = build_function_placeholder_from_parts(tp,
                    reduction->span, name, is_proc);
                if (name.length && !sink->type_object_depth) {
                    lambda_ast_register_name(tp, (AstNamedNode*)fn);
                }
            }
            if (!name.length) ((TypeFunc*)fn->type)->is_anonymous = true;
            ((TypeFunc*)fn->type)->is_public =
                (reduction->flags & LAMBDA_REDUCTION_FLAG_PUBLIC) != 0 ||
                ((TypeFunc*)fn->type)->is_public;
            sink->function_nodes[sink->function_depth] = fn;
            sink->function_scopes[sink->function_depth++] =
                lambda_ast_enter_scope(tp, is_proc);
            return 0;
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_TYPE_ALIAS_BEGIN) {
            direct_type_alias_begin(sink, reduction);
            return 0;
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_TYPE_OBJECT_BEGIN) {
            direct_object_begin(sink, reduction);
            sink->type_object_depth++;
            return 0;
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_TYPE_OBJECT_END) {
            if (sink->type_object_depth) {
                direct_object_end(sink);
                sink->type_object_depth--;
            }
            return 0;
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_TYPE_OBJECT_CONSTRAINT_BEGIN) {
            tp->in_that_clause = true;
            return 0;
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_TYPE_OBJECT_CONSTRAINT_END) {
            tp->in_that_clause = false;
            return 0;
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_FUNCTION_END) {
            if (!sink->function_depth) {
                sink->failed = true;
                return 0;
            }
            uint32_t slot = --sink->function_depth;
            AstFuncNode* fn = sink->function_nodes[slot];
            NameScope* function_scope = sink->function_scopes[slot];
            lambda_ast_leave_scope(tp, function_scope);
            // Capture analysis must run after the body and nested functions
            // are complete; otherwise an outer closure loses transitive
            // captures and MIR sees those names as undefined variables.
            if (fn) {
                analyze_captures(tp, fn, find_global_scope(function_scope->parent));
                validate_cross_frame_binding_reads(tp, fn);
            }
            return 0;
        }
    }

    switch (reduction->kind) {
    case LAMBDA_REDUCE_VIEW:
        if (reduction->form == LAMBDA_REDUCTION_FORM_VIEW_STATE) {
            if (!direct_view_add_state(sink, reduction)) break;
            return direct_ast_value(child0);
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_VIEW_HANDLER) {
            if (!direct_view_finish_handler(sink, reduction)) break;
            return direct_ast_value(direct_ast_node(reduction->children[1]));
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_VIEW) {
            AstNode* view = direct_view_finish(sink, reduction);
            if (view) return direct_ast_value(view);
        }
        break;
    case LAMBDA_REDUCE_LIST: {
        if (reduction->form == LAMBDA_REDUCTION_FORM_FOR_CLAUSES) {
            return direct_ast_value(reduction->child_count > 1
                ? direct_ast_node(reduction->children[0]) : child0);
        }
        AstNode* item = reduction->child_count > 1
            ? direct_ast_node(reduction->children[1]) : child0;
        if (reduction->child_count > 1) return direct_ast_value(direct_append(child0, item));
        return direct_ast_value(item);
    }
    case LAMBDA_REDUCE_CONTENT: {
        AstNode* content = direct_content_node(tp, reduction->span, child0);
        if (content && sink->branch_scope_depth) {
            ((AstListNode*)content)->vars = sink->branch_scopes[
                sink->branch_scope_depth - 1];
        }
        return direct_ast_value(content);
    }
    case LAMBDA_REDUCE_ATOM: {
        LambdaToken token = reduction->detail_token;
        LambdaAstLiteralKind literal_kind;
        AstNode* node = NULL;
        if (direct_literal_kind(token.kind, &literal_kind)) {
            node = build_literal_from_span(tp, token.span, literal_kind);
        } else if (token.kind == LAMBDA_TOK_BASE_TYPE ||
                token.kind == LAMBDA_TOK_TYPE) {
            StrView base_name = source_span_text(tp, token.span);
            if (strview_equal(&base_name, "null")) {
                // `null` shares the base-type token with the type-pattern
                // spelling, but in expression position it is a value literal.
                AstPrimaryNode* null_node = (AstPrimaryNode*)alloc_ast_node_from_span(
                    tp, AST_NODE_PRIMARY, token.span, sizeof(AstPrimaryNode));
                // `null` shares the base-type token with type expressions, but
                // in value position it must retain the literal marker so both
                // MIR and the interpreter materialize ItemNull (S3.1).
                null_node->type = &LIT_NULL;
                node = (AstNode*)null_node;
            } else {
                node = direct_base_type_from_span(tp, token.span);
            }
        } else if (token.kind == LAMBDA_TOK_TILDE) {
            node = build_current_item_from_span(tp, token.span, false);
        } else if (token.kind == LAMBDA_TOK_TILDE_INDEX) {
            node = build_current_item_from_span(tp, token.span, true);
        } else if (token.kind == LAMBDA_TOK_PARENT) {
            node = build_current_parent_navigation_from_span(tp, token.span);
        } else if (token.kind == LAMBDA_TOK_CARET) {
            node = build_current_error_from_span(tp, token.span);
        } else if (token.kind == LAMBDA_TOK_LAST) {
            node = alloc_ast_node_from_span(tp, AST_NODE_LAST_INDEX, token.span,
                sizeof(AstNode));
            node->type = &TYPE_INT;
        } else if (token.kind == LAMBDA_TOK_PATTERN_ISLAND) {
            StrView source = source_span_text(tp, token.span);
            node = parse_type_pattern_text_span(tp, source.str,
                source.str + source.length, token.span);
            if (!node) node = direct_type_error_from_span(tp, token.span);
        } else {
            node = build_primary_wrapper_from_parts(tp, token.span,
                build_identifier_from_span(tp, token.span));
        }
        return direct_ast_value(node);
    }
    case LAMBDA_REDUCE_PREFIX: {
        if (reduction->child_count != 1) break;
        AstNode* operand = direct_ast_node(reduction->children[0]);
        StrView op = direct_token_text(tp, reduction->detail_token);
        if (reduction->detail_token.kind == LAMBDA_TOK_STAR) {
            return direct_ast_value(build_spread_node_from_parts(tp,
                reduction->span, operand));
        }
        if (reduction->detail_token.kind == LAMBDA_TOK_RAISE) {
            return direct_ast_value(build_raise_node_from_parts(tp,
                reduction->span, operand, false));
        }
        if (reduction->detail_token.kind == LAMBDA_TOK_BANG) {
            return direct_ast_value(build_type_negation_from_parts(tp,
                reduction->span, operand));
        }
        return direct_ast_value(build_unary_node_from_parts(tp,
            reduction->span, op, operand));
    }
    case LAMBDA_REDUCE_GROUP: {
        if (reduction->child_count == 1) {
            return direct_ast_value(build_primary_wrapper_from_parts(tp,
                reduction->span, child0));
        }
        AstNode* grouped = NULL;
        for (uint32_t i = 0; i < reduction->child_count; i++) {
            grouped = direct_append(grouped,
                direct_ast_node(reduction->children[i]));
        }
        NameScope* group_scope = sink->completed_group_scope;
        sink->completed_group_scope = NULL;
        return direct_ast_value(direct_let_group(tp, reduction->span, grouped,
            group_scope));
    }
    case LAMBDA_REDUCE_ARRAY:
        return direct_ast_value(build_array_from_items(tp, reduction->span, child0));
    case LAMBDA_REDUCE_MAP:
        return direct_ast_value(build_map_from_items(tp, reduction->span, child0));
    case LAMBDA_REDUCE_ELEMENT: {
        AstNode* children = NULL;
        for (uint32_t i = 0; i < reduction->child_count; i++) {
            children = direct_append(children,
                direct_ast_node(reduction->children[i]));
        }
        return direct_ast_value(build_element_from_parts(tp, reduction->span,
            reduction->detail_token.span, children));
    }
    case LAMBDA_REDUCE_LET: {
        if (reduction->form == LAMBDA_REDUCTION_FORM_DECOMPOSE &&
                reduction->name_tokens && reduction->name_count > 0 &&
                reduction->child_count > 0) {
            String** names = (String**)pool_calloc(tp->pool,
                sizeof(String*) * reduction->name_count);
            for (uint32_t i = 0; i < reduction->name_count; i++) {
                names[i] = name_pool_create_strview(tp->name_pool,
                    direct_token_text(tp, reduction->name_tokens[i]));
            }
            AstNode* value = direct_ast_node(
                reduction->children[reduction->child_count - 1]);
            return direct_ast_value(build_decompose_from_parts(tp,
                reduction->span, names, (int)reduction->name_count, value,
                (reduction->flags & LAMBDA_REDUCTION_FLAG_DECOMPOSE_NAMED) != 0));
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_TOKEN) {
            StrView name = direct_token_text(tp, reduction->detail_token);
            AstNode* type_expr = (reduction->flags & LAMBDA_REDUCTION_FLAG_TYPED)
                ? child0 : NULL;
            AstNode* value = (reduction->flags & LAMBDA_REDUCTION_FLAG_TYPED)
                ? (reduction->child_count > 1
                    ? direct_ast_node(reduction->children[1]) : NULL)
                : child0;
            return direct_ast_value((AstNode*)build_assignment_from_parts(tp,
                reduction->span, name, type_expr, value));
        }
        return direct_ast_value(child0);
    }
    case LAMBDA_REDUCE_FUNCTION: {
        if (reduction->form != LAMBDA_REDUCTION_FORM_FUNCTION ||
                reduction->child_count == 0) break;
        AstNode* params = NULL;
        AstNode* returned = NULL;
        AstNode* error_type = NULL;
        AstNode* body = direct_ast_node(
            reduction->children[reduction->child_count - 1]);
        // S16.6.8: an `=>` body is an expression position. The braced form
        // carries BODY_BLOCK and is the statement spelling, so it is exempt.
        if (!(reduction->flags & LAMBDA_REDUCTION_FLAG_BODY_BLOCK)) {
            reject_procedural_block_operand(tp, body, "an arrow `=>` body");
        }
        uint32_t i = 0;
        if (i < reduction->child_count &&
                direct_ast_node(reduction->children[i]) &&
                direct_ast_node(reduction->children[i])->node_type == AST_NODE_PARAM) {
            params = direct_ast_node(reduction->children[i++]);
        }
        while (i + 1 < reduction->child_count) {
            AstNode* type_node = direct_ast_node(reduction->children[i++]);
            if (!returned) returned = type_node;
            else if (!error_type) error_type = type_node;
        }
        StrView name = source_span_text(tp, reduction->secondary_token.span);
        if (sink->function_depth) {
            uint32_t slot = sink->function_depth - 1;
            AstNode* completed = (AstNode*)direct_complete_function(tp, reduction->span,
                sink->function_nodes[slot], sink->function_scopes[slot], params,
                returned, error_type, body,
                (reduction->flags & LAMBDA_REDUCTION_FLAG_PROC) != 0,
                (reduction->flags & LAMBDA_REDUCTION_FLAG_VARIADIC) != 0,
                (reduction->flags & LAMBDA_REDUCTION_FLAG_RAISED) != 0);
            if (sink->type_object_depth && sink->function_depth == 1) {
                direct_object_add_method(sink, completed);
            }
            return direct_ast_value(completed);
        }
        return direct_ast_value(build_function_from_parts(tp, reduction->span,
            name, params, returned, error_type, body,
            (reduction->flags & LAMBDA_REDUCTION_FLAG_PROC) != 0,
            (reduction->flags & LAMBDA_REDUCTION_FLAG_VARIADIC) != 0,
            (reduction->flags & LAMBDA_REDUCTION_FLAG_RAISED) != 0));
    }
    case LAMBDA_REDUCE_IF: {
        if (reduction->child_count < 2 || reduction->child_count > 3) break;
        return direct_ast_value(build_if_node_from_parts(tp, reduction->span,
            direct_ast_node(reduction->children[0]),
            direct_ast_node(reduction->children[1]),
            reduction->child_count == 3
                ? direct_ast_node(reduction->children[2]) : NULL));
    }
    case LAMBDA_REDUCE_MATCH: {
        if (reduction->child_count != 2) break;
        return direct_ast_value(build_match_from_parts(tp, reduction->span,
            direct_ast_node(reduction->children[0]),
            direct_ast_node(reduction->children[1])));
    }
    case LAMBDA_REDUCE_FOR: {
        if (!sink->loop_scope_depth || reduction->child_count != 2) break;
        uint32_t slot = sink->loop_scope_depth - 1;
        if (reduction->form == LAMBDA_REDUCTION_FORM_FOR_WHILE) {
            return direct_ast_value(build_while_from_parts(tp, reduction->span,
                direct_ast_node(reduction->children[0]),
                direct_ast_node(reduction->children[1]),
                sink->loop_scopes[slot]));
        }
        AstForNode* active = sink->for_nodes[slot];
        if (!active) break;
        active->then = direct_ast_node(reduction->children[1]);
        bool statement_form = (reduction->flags & LAMBDA_REDUCTION_FLAG_BODY_BLOCK) != 0;
        active->node_type = statement_form ? AST_NODE_FOR_STAM : AST_NODE_FOR_EXPR;
        if (active->then && active->then->node_type == AST_NODE_CONTENT &&
                !((AstListNode*)active->then)->vars) {
            ((AstListNode*)active->then)->vars = active->vars;
        }
        active->type = statement_form ? set_type_any(tp, ANY_STATEMENT) :
            set_type_any(tp, ANY_LIST);
        return direct_ast_value((AstNode*)active);
    }
    case LAMBDA_REDUCE_BINARY: {
        if (reduction->child_count != 2) break;
        return direct_ast_value(build_binary_node_from_parts(tp, reduction->span,
            direct_token_text(tp, reduction->detail_token), child0,
            direct_ast_node(reduction->children[1])));
    }
    case LAMBDA_REDUCE_POSTFIX: {
        if (!reduction->child_count) break;
        AstNode* object = child0;
        if (reduction->form == LAMBDA_REDUCTION_FORM_MEMBER) {
            StrView source = source_span_text(tp, reduction->span);
            AstNode* path = try_parse_path_expr_text_span(tp, source.str,
                source.str + source.length, reduction->span);
            if (path) return direct_ast_value(path);
            if (reduction->detail_token.kind == LAMBDA_TOK_SLASH) {
                // A slash after an existing value is the navigation root
                // field (`record./.name`), not a normal member named `/`.
                return direct_ast_value(build_navigation_node_from_parts(tp,
                    reduction->span, object, true));
            }
            if (reduction->detail_token.kind == LAMBDA_TOK_PARENT) {
                return direct_ast_value(build_navigation_node_from_parts(tp,
                    reduction->span, object, false));
            }
            AstNode* field = direct_member_field(tp, reduction->detail_token);
            return direct_ast_value(build_field_node_from_parts(tp,
                reduction->span, AST_NODE_MEMBER_EXPR, object, field));
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_INDEX) {
            AstNode* fields = NULL;
            for (uint32_t i = 1; i < reduction->child_count; i++) {
                fields = direct_append(fields,
                    direct_ast_node(reduction->children[i]));
            }
            return direct_ast_value(build_field_node_from_parts(tp,
                reduction->span, AST_NODE_INDEX_EXPR, object, fields));
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_QUERY &&
                reduction->child_count == 2) {
            return direct_ast_value(build_query_node_from_parts(tp,
                reduction->span, object,
                direct_ast_node(reduction->children[1]),
                reduction->detail_token.kind == LAMBDA_TOK_DOT_QUESTION));
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_HANDLER) {
            if (reduction->child_count < 2 || reduction->child_count > 3) break;
            AstNode* body = direct_ast_node(reduction->children[1]);
            AstNode* value_body = reduction->child_count == 3
                ? direct_ast_node(reduction->children[2]) : NULL;
            return direct_ast_value(build_handler_from_parts(tp, reduction->span,
                object, body, value_body));
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_PROPAGATE &&
                reduction->child_count == 1) {
            return direct_ast_value(build_propagate_node_from_parts(tp,
                reduction->span, object));
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_CALL) {
            AstNode* args = NULL;
            for (uint32_t i = 1; i < reduction->child_count; i++) {
                args = direct_append(args,
                    direct_ast_node(reduction->children[i]));
            }
            int prior_pipe_inject = tp->pipe_inject_args;
            tp->pipe_inject_args = (reduction->flags &
                LAMBDA_REDUCTION_FLAG_PIPE_INJECT) ? 1 : 0;
            AstNode* call = build_call_node_from_parts(tp,
                reduction->span, object, args,
                (int)reduction->child_count - 1);
            tp->pipe_inject_args = prior_pipe_inject;
            return direct_ast_value(call);
        }
        break;
    }
    case LAMBDA_REDUCE_TYPE_SLOT: {
        if ((reduction->flags & LAMBDA_REDUCTION_FLAG_ANNOTATION_CONSTRAINT) &&
                reduction->child_count == 2) {
            AstNode* base = direct_ast_node(reduction->children[0]);
            AstNode* constraint = direct_ast_node(reduction->children[1]);
            return direct_ast_value(direct_constrained_type(tp, reduction->span,
                base, constraint));
        }
        StrView source = source_span_text(tp, reduction->span);
        AstNode* node = parse_type_pattern_text_span(tp, source.str,
            source.str + source.length, reduction->span);
        if (!node) {
            // A rejected annotation still needs a concrete reduction value so
            // its enclosing declaration can report the semantic error without
            // dereferencing a poisoned null slot (D8.1.1v3).
            node = direct_type_error_from_span(tp, reduction->span);
        }
        return direct_ast_value(node);
    }
    case LAMBDA_REDUCE_DECLARATION: {
        // `pub` is a visibility modifier, not a distinct AST
        // node. The committed assignment/function child already owns the
        // binding entry; preserve it instead of manufacturing a declaration.
        if (reduction->form == LAMBDA_REDUCTION_FORM_IMPORT) {
            return direct_ast_value(build_module_import_from_parts(tp,
                reduction->span,
                direct_token_text(tp, reduction->detail_token),
                direct_token_text(tp, reduction->secondary_token)));
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_TYPE_ALIAS) {
            StrView alias_name = direct_token_text(tp, reduction->detail_token);
            AstNamedNode* pending = sink->pending_type_alias;
            sink->pending_type_alias = NULL;
            if (pending && !(pending->name->len == alias_name.length &&
                    memcmp(pending->name->chars, alias_name.str,
                        alias_name.length) == 0)) {
                pending = NULL;
            }
            if (child0 && child0->node_type == AST_NODE_PATTERN_ISLAND) {
                AstNode* pattern = direct_pattern_definition(tp, reduction->span,
                    alias_name, child0, pending);
                return direct_ast_value(direct_type_stam(tp, reduction->span,
                    pattern,
                    (reduction->flags & LAMBDA_REDUCTION_FLAG_PUBLIC) != 0));
            }
            // reuse the pre-bound declaration node so every self-reference
            // captured during body parsing keeps the same binding identity
            AstNamedNode* alias = pending;
            TypeType* pre_type = pending && pending->type &&
                pending->type->type_id == LMD_TYPE_TYPE
                ? (TypeType*)pending->type : NULL;
            if (!alias) {
                alias = (AstNamedNode*)alloc_ast_node_from_span(tp,
                    AST_NODE_ASSIGN, reduction->span, sizeof(AstNamedNode));
                alias->name = name_pool_create_strview(tp->name_pool,
                    alias_name);
            } else {
                alias->source_span = reduction->span;
            }
            alias->as = child0;
            alias->type = child0 && child0->type ? child0->type : &TYPE_ANY;
            alias->is_type_definition = true;
            if (pre_type) direct_adopt_pending_alias_map(tp, alias, pre_type);
            direct_finalize_type_alias(tp, alias);
            if (!pending) lambda_ast_register_name(tp, alias);
            return direct_ast_value(direct_type_stam(tp, reduction->span,
                (AstNode*)alias,
                (reduction->flags & LAMBDA_REDUCTION_FLAG_PUBLIC) != 0));
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_TYPE_OBJECT) {
            if (sink->completed_object) {
                AstNode* object = (AstNode*)sink->completed_object;
                sink->completed_object = NULL;
                return direct_ast_value(object);
            }
            break;
        }
        if (child0 && (reduction->flags & LAMBDA_REDUCTION_FLAG_PUBLIC)) {
            AstLetNode* pub = (AstLetNode*)alloc_ast_node_from_span(tp,
                AST_NODE_PUB_STAM, reduction->span, sizeof(AstLetNode));
            pub->declare = child0;
            pub->type = set_type_any(tp, ANY_STATEMENT);
            return direct_ast_value((AstNode*)pub);
        }
        if (child0) return direct_ast_value(child0);
        AstNode* statement_noop = alloc_ast_node_from_span(tp, AST_NODE_NULL,
            reduction->span, sizeof(AstNode));
        statement_noop->type = &TYPE_NULL;
        return direct_ast_value(statement_noop);
    }
    case LAMBDA_REDUCE_PATH_SLOT: {
        StrView source = source_span_text(tp, reduction->span);
        AstNode* node = parse_path_expr_text_span(tp, source.str,
            source.str + source.length, reduction->span);
        if (!node) {
            // Keep a committed path reduction alive after a semantic path
            // rejection; the enclosing expression must report the error
            // rather than dereference a null reduction value (D8.1.1v3).
            // must be a full AstPathNode: the node is tagged AST_NODE_PATH_EXPR,
            // so the transpiler casts and reads `authority` — a bare AstNode
            // allocation left that read past the end of the object.
            node = alloc_ast_node_from_span(tp, AST_NODE_PATH_EXPR,
                reduction->span, sizeof(AstPathNode));
            node->type = &TYPE_ERROR;
        }
        return direct_ast_value(node);
    }
    case LAMBDA_REDUCE_ASSIGNMENT:
        if (reduction->child_count == 2) {
            AstNode* assignment = build_assignment_statement_from_parts(tp,
                reduction->span, direct_ast_node(reduction->children[0]),
                direct_ast_node(reduction->children[1]));
            if (assignment) return direct_ast_value(assignment);
            // Preserve a real reduction value after a semantic rejection;
            // a null callback result leaves the parser's value slot poisoned
            // and the enclosing content walk then dereferences it (D8.1.1).
            AstNode* noop = alloc_ast_node_from_span(tp, AST_NODE_NULL,
                reduction->span, sizeof(AstNode));
            noop->type = &TYPE_NULL;
            return direct_ast_value(noop);
        }
        break;
    case LAMBDA_REDUCE_STATEMENT: {
        if (reduction->form == LAMBDA_REDUCTION_FORM_RETURN ||
                reduction->form == LAMBDA_REDUCTION_FORM_BREAK ||
                reduction->form == LAMBDA_REDUCTION_FORM_CONTINUE) {
            return direct_ast_value(build_control_statement_from_parts(tp,
                reduction->span, reduction->form, child0));
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_TYPE_OBJECT_FIELD) {
            direct_object_add_field(sink, reduction);
            return direct_ast_value(child0);
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_TYPE_OBJECT_CONTENT) {
            if (sink->object_node && child0) {
                sink->object_node->content = direct_object_content_from_parts(
                    tp, reduction->span, child0);
            }
            return direct_ast_value(child0);
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_TYPE_OBJECT_CONSTRAINT) {
            if (sink->object_node && child0) {
                if (!sink->object_node->constraints) {
                    sink->object_node->constraints = child0;
                } else {
                    AstNode* tail = sink->object_node->constraints;
                    while (tail->next) tail = tail->next;
                    tail->next = child0;
                }
                sink->object_type->constraint = child0;
            }
            return direct_ast_value(child0);
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_VAR) {
            if (!tp->current_scope || !tp->current_scope->is_proc) {
                record_semantic_error_span(tp, reduction->span, ERR_PROC_IN_FN,
                    "`var` is only allowed inside a procedure (pn)");
            }
            AstLetNode* var = (AstLetNode*)alloc_ast_node_from_span(tp,
                AST_NODE_VAR_STAM, reduction->span, sizeof(AstLetNode));
            var->declare = child0;
            var->type = set_type_any(tp, ANY_STATEMENT);
            for (AstNode* declaration = child0; declaration;
                    declaration = declaration->next) {
                if (declaration->node_type != AST_NODE_ASSIGN) continue;
                AstNamedNode* named = (AstNamedNode*)declaration;
                NameEntry* entry = lookup_name_in_current_scope(tp, named->name);
                if (entry) {
                    entry->is_mutable = true;
                    named->entry = entry;
                }
            }
            return direct_ast_value((AstNode*)var);
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_FOR_BINDING) {
            if (!sink->loop_scope_depth || !sink->for_nodes[sink->loop_scope_depth - 1] ||
                    !reduction->child_count) break;
            bool has_index_type = (reduction->flags & LAMBDA_REDUCTION_FLAG_INDEX_TYPED) != 0;
            AstNode* index_type = has_index_type
                ? direct_ast_node(reduction->children[0]) : NULL;
            AstNode* source = direct_ast_node(reduction->children[has_index_type ? 1 : 0]);
            AstNode* join = reduction->child_count > (has_index_type ? 2u : 1u)
                ? direct_ast_node(reduction->children[has_index_type ? 2 : 1]) : NULL;
            AstNode* loop = build_loop_from_parts(tp, reduction->span,
                reduction->detail_token, reduction->secondary_token,
                reduction->flags, index_type, source, join);
            AstForNode* active = sink->for_nodes[sink->loop_scope_depth - 1];
            direct_append_clause(&active->loop, loop);
            return direct_ast_value(loop);
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_FOR_LET) {
            if (!sink->loop_scope_depth || !sink->for_nodes[sink->loop_scope_depth - 1]) break;
            AstNamedNode* let = (AstNamedNode*)alloc_ast_node_from_span(tp,
                AST_NODE_ASSIGN, reduction->span, sizeof(AstNamedNode));
            let->name = name_pool_create_strview(tp->name_pool,
                direct_token_text(tp, reduction->detail_token));
            let->as = child0;
            let->type = child0 && child0->type ? child0->type : &TYPE_ANY;
            lambda_ast_register_name(tp, let);
            AstForNode* active = sink->for_nodes[sink->loop_scope_depth - 1];
            direct_append_clause(&active->let_clause, (AstNode*)let);
            return direct_ast_value((AstNode*)let);
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_FOR_WHERE) {
            if (!sink->loop_scope_depth || !sink->for_nodes[sink->loop_scope_depth - 1]) break;
            sink->for_nodes[sink->loop_scope_depth - 1]->where = child0;
            lint_condition_span(tp, reduction->span, child0, "where");
            return direct_ast_value(child0);
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_FOR_GROUP_KEY) {
            // Group keys have a smaller layout than the owning clause; the
            // planner uses this tag to avoid treating a key as a clause entry.
            AstGroupKey* key = (AstGroupKey*)alloc_ast_node_from_span(tp,
                AST_NODE_GROUP_KEY, reduction->span, sizeof(AstGroupKey));
            key->expr = child0;
            key->alias = reduction->detail_token.kind
                ? name_pool_create_strview(tp->name_pool,
                    direct_token_text(tp, reduction->detail_token)) :
                infer_group_key_alias(tp, child0);
            return direct_ast_value((AstNode*)key);
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_FOR_GROUP) {
            if (!sink->loop_scope_depth || !sink->for_nodes[sink->loop_scope_depth - 1]) break;
            AstGroupClause* group = (AstGroupClause*)alloc_ast_node_from_span(tp,
                AST_NODE_GROUP_CLAUSE, reduction->span, sizeof(AstGroupClause));
            group->name = name_pool_create_strview(tp->name_pool,
                direct_token_text(tp, reduction->detail_token));
            group->keys = (AstGroupKey*)child0;
            for (AstGroupKey* key = group->keys; key;
                    key = (AstGroupKey*)key->next) group->key_count++;
            AstForNode* active = sink->for_nodes[sink->loop_scope_depth - 1];
            active->group = group;
            enter_for_group_scope(tp, active);
            AstNamedNode* grouped = (AstNamedNode*)alloc_ast_node_from_span(tp,
                AST_NODE_ASSIGN, reduction->span, sizeof(AstNamedNode));
            grouped->name = group->name;
            grouped->type = &TYPE_ELMT;
            lambda_ast_register_name(tp, grouped);
            // The aggregate scope is entered before `into` is registered;
            // retain its NameEntry so T0 can publish the materialized group
            // without re-looking the binding up in the closed row scope.
            group->entry = lookup_name_in_current_scope(tp, group->name);
            return direct_ast_value((AstNode*)group);
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_FOR_ORDER) {
            if (!sink->loop_scope_depth || !sink->for_nodes[sink->loop_scope_depth - 1]) break;
            AstOrderSpec* order = (AstOrderSpec*)alloc_ast_node_from_span(tp,
                AST_NODE_ORDER_SPEC, reduction->span, sizeof(AstOrderSpec));
            order->expr = child0;
            order->descending = reduction->detail_token.kind == LAMBDA_TOK_DESC;
            order->type = set_type_any(tp, ANY_STATEMENT);
            AstForNode* active = sink->for_nodes[sink->loop_scope_depth - 1];
            direct_append_clause(&active->order, (AstNode*)order);
            return direct_ast_value((AstNode*)order);
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_FOR_LIMIT ||
                reduction->form == LAMBDA_REDUCTION_FORM_FOR_OFFSET) {
            if (!sink->loop_scope_depth || !sink->for_nodes[sink->loop_scope_depth - 1]) break;
            AstForNode* active = sink->for_nodes[sink->loop_scope_depth - 1];
            if (reduction->form == LAMBDA_REDUCTION_FORM_FOR_LIMIT) {
                active->limit = child0;
                active->limit_from_end =
                    (reduction->flags & LAMBDA_REDUCTION_FLAG_OPTIONAL) != 0;
            } else active->offset = child0;
            return direct_ast_value(child0);
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_NAMED_ARGUMENT) {
            AstNode* value = reduction->child_count ? child0 : NULL;
            StrView name = direct_token_text(tp, reduction->detail_token);
            return direct_ast_value((AstNode*)build_named_argument_from_parts(tp,
                reduction->span, name, value));
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_MAP_ITEM) {
            AstNamedNode* item = (AstNamedNode*)alloc_ast_node_from_span(tp,
                AST_NODE_KEY_EXPR, reduction->span, sizeof(AstNamedNode));
            StrView name = direct_key_text(tp, reduction->detail_token);
            item->name = name_pool_create_strview(tp->name_pool, name);
            item->as = child0;
            item->type = child0 && child0->type ? child0->type : &TYPE_ANY;
            return direct_ast_value((AstNode*)item);
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_ELEMENT_ATTRIBUTE) {
            AstNamedNode* item = (AstNamedNode*)alloc_ast_node_from_span(tp,
                AST_NODE_KEY_EXPR, reduction->span, sizeof(AstNamedNode));
            StrView name = direct_key_text(tp, reduction->detail_token);
            item->name = name_pool_create_strview(tp->name_pool, name);
            item->as = child0;
            item->type = child0 && child0->type ? child0->type : &TYPE_ANY;
            return direct_ast_value((AstNode*)item);
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_MATCH_ARM) {
            AstMatchArm* arm = (AstMatchArm*)alloc_ast_node_from_span(tp,
                AST_NODE_MATCH_ARM, reduction->span, sizeof(AstMatchArm));
            arm->next = NULL;
            arm->pattern = reduction->child_count == 2
                ? direct_ast_node(reduction->children[0]) : NULL;
            arm->body = direct_ast_node(reduction->children[reduction->child_count - 1]);
            arm->body_braced =
                (reduction->flags & LAMBDA_REDUCTION_FLAG_BODY_BLOCK) != 0;
            return direct_ast_value((AstNode*)arm);
        }
        if (reduction->form == LAMBDA_REDUCTION_FORM_PARAMETER) {
            AstNode* type_node = NULL;
            AstNode* default_value = NULL;
            for (uint32_t i = 0; i < reduction->child_count; i++) {
                AstNode* child = direct_ast_node(reduction->children[i]);
                // named aliases reduce as AST_NODE_IDENT, so node kind alone
                // would silently drop a parameter's declared contract (D6.1.2).
                if (child && ast_is_explicit_type_value(child)) {
                    type_node = child;
                } else {
                    default_value = child;
                }
            }
            return direct_ast_value((AstNode*)build_param_from_parts(tp,
                reduction->span, direct_token_text(tp, reduction->detail_token),
                type_node, default_value,
                (reduction->flags & LAMBDA_REDUCTION_FLAG_OPTIONAL) != 0,
                (reduction->flags & LAMBDA_REDUCTION_FLAG_VAR) != 0));
        }
        if (child0 && (child0->node_type == AST_NODE_ASSIGN ||
                child0->node_type == AST_NODE_DECOMPOSE)) {
            AstLetNode* let = (AstLetNode*)alloc_ast_node_from_span(tp,
                AST_NODE_LET_STAM, reduction->span, sizeof(AstLetNode));
            let->declare = child0;
            let->type = child0->type;
            return direct_ast_value((AstNode*)let);
        }
        if (child0) return direct_ast_value(child0);
        // Statement separators may carry a syntax-only declaration (for
        // example `var` or `apply;`).  Publish a real sentinel so the parser
        // reduction value is not replaced by its structural hash and later
        // mistaken for an AstNode pointer.
        AstNode* noop = alloc_ast_node_from_span(tp, AST_NODE_NULL,
            reduction->span, sizeof(AstNode));
        noop->type = &TYPE_NULL;
        return direct_ast_value(noop);
    }
    case LAMBDA_REDUCE_DOCUMENT: {
        AstScript* root = (AstScript*)alloc_ast_node_from_span(tp, AST_SCRIPT,
            reduction->span, sizeof(AstScript));
        root->global_vars = tp->current_scope;
        // Top-level imports are script children in the legacy AST.  Keep that
        // contract here so module registration sees every dependency before it
        // scans the single content list for public declarations.
        AstNode* imports = NULL;
        AstNode* content_items = NULL;
        AstListNode* content = child0 && child0->node_type == AST_NODE_CONTENT
            ? (AstListNode*)child0 : NULL;
        if (content) {
            for (AstNode* item = content->item; item;) {
                AstNode* next = item->next;
                item->next = NULL;
                if (item->node_type == AST_NODE_IMPORT) {
                    imports = direct_append(imports, item);
                } else {
                    content_items = direct_append(content_items, item);
                }
                item = next;
            }
            content->item = content_items;
            content->list_type->length = 0;
            for (AstNode* item = content_items; item; item = item->next) {
                content->list_type->length++;
            }
            content->type = content_items && content_items->type
                ? content_items->type : &TYPE_NULL;
        }
        AstNode* tail = imports;
        while (tail && tail->next) tail = tail->next;
        AstNode* body = (AstNode*)content;
        if (content_items && !content_items->next) {
            // `build_content(..., true, true)` unwraps a sole top-level
            // declaration. Keeping a CONTENT wrapper here makes module MIR
            // materialize an otherwise absent list before invoking main.
            body = content_items;
        }
        if (tail) tail->next = body;
        root->child = imports ? imports : body;
        root->type = child0 && child0->type ? child0->type : &TYPE_ANY;
        sink->root = root;
        return direct_ast_value((AstNode*)root);
    }
    default:
        break;
    }

    // Keep unsupported forms fail-closed while the reduction contract is being
    // expanded. Publishing a partial semantic node would make comparison mode
    // accept a source with a silently different AST.
    log_error("direct AST reduction unsupported kind=%d form=%d span=%u..%u",
        (int)reduction->kind, (int)reduction->form,
        reduction->span.start_byte, reduction->span.end_byte);
    sink->failed = true;
    return 0;
}

LambdaParseStatus lambda_rd_build_ast(Transpiler* tp, const char* source,
        size_t length, AstScript** root_out, LambdaParseError* error) {
    if (root_out) *root_out = NULL;
    if (!tp || !source) return LAMBDA_PARSE_ERROR;
    tp->source = source;
    if (!tp->pool) {
        Input* input = Input::create(mem_pool_create(NULL, MEM_ROLE_AST, "direct-parser.pool"), nullptr);
        if (!input) return LAMBDA_PARSE_ERROR;
        tp->pool = input->pool;
        tp->arena = input->arena;
        tp->name_pool = input->name_pool;
        tp->type_list = input->type_list;
        tp->url = input->url;
        tp->path = input->path;
        tp->root = input->root;
    }
    if (!tp->const_list) tp->const_list = arraylist_new(16);
    if (!tp->current_scope) {
        tp->current_scope = (NameScope*)pool_calloc(tp->pool, sizeof(NameScope));
    }

    // Match the top-level pass: every named function is visible
    // while earlier bodies are reduced, so a declaration cannot be mistaken
    // for a same-spelled system function (for example `gamma`).
    direct_predeclare_top_level_functions(tp, source, length);

    LambdaDirectAstSink sink = {.tp = tp, .root = NULL, .failed = false};
    LambdaParseSink parse_sink = {direct_ast_reduce};
    LambdaParseStatus status = lambda_rd_parse_source(source, length, &parse_sink,
        &sink, NULL, error);
    if (status != LAMBDA_PARSE_OK || sink.failed || !sink.root) {
        log_error("direct parser failure status=%d sink=%d at=%u message=%s",
            (int)status, sink.failed ? 1 : 0,
            error ? error->span.start_byte : 0,
            error && error->message ? error->message : "<none>");
        if (error && !error->message) error->message = "direct AST reduction failed";
        return status == LAMBDA_PARSE_OK ? LAMBDA_PARSE_ERROR : status;
    }
    finalize_lambda_script_ast(tp, sink.root);
    if (root_out) *root_out = sink.root;
    return LAMBDA_PARSE_OK;
}
