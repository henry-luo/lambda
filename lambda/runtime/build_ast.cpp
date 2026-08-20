#include "transpiler.hpp"
#include "../core/lambda-decimal.hpp"
#include "lambda-number-types.hpp"
#include "lambda-error.h"
#include "type_contract.hpp"
#include "type_build.hpp"
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
#include <errno.h>
#include <stdlib.h>

// C16 ruling 9 (revised): an unsuffixed literal's type is LEXICAL, and an
// exponent makes it a float -- so an integer token is now only digits or hex,
// and the exponent scaling this used to perform is gone with the grammar rule
// that produced `10e1` as SYM_INT. Returns false when the value leaves the
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

// Caps build_expr recursion so a pathologically nested source reports an error
// instead of overflowing the stack (and tripping the SIGSEGV recovery). Well above
// any real expression nesting; input-data nesting is already capped by the parsers.
#define MAX_BUILD_DEPTH 1000

AstNamedNode* build_param_expr(Transpiler* tp, TSNode param_node, bool is_type);
AstNode* build_named_argument(Transpiler* tp, TSNode arg_node);
static StaticBoundaryResult static_boundary_relation(Type* source, Type* target);
static bool validate_lambda_call_arguments(Transpiler* tp, AstCallNode* call,
    TSNode diagnostic_node, int arg_count);

// Forward declarations for pattern building
AstNode* build_string_pattern(Transpiler* tp, TSNode node, bool is_symbol, AstNode* prebuilt_as);
AstNode* build_lit_node(Transpiler* tp, TSNode lit_node, bool quoted_value, TSSymbol symbol);
AstNode* build_identifier(Transpiler* tp, TSNode ident_node);

// Forward declarations for pipe expression building
AstNode* build_current_expr(Transpiler* tp, TSNode node);

// Forward declaration for sequential parenthesized lets (uses build_let_expr defined later)
AstNode* build_let_expr(Transpiler* tp, TSNode let_node);

// Forward declaration for imported module resolution
static const char* resolve_imported_module(Transpiler* tp, StrView* name);

// Forward declaration for type building (used by query expressions)

// Forward declaration for function building (used by object type methods)
AstNode* build_func(Transpiler* tp, TSNode func_node, bool is_named, bool is_global);

// Forward declaration for view/edit template building
AstNode* build_view_stam(Transpiler* tp, TSNode view_node);

// Defined with the E228 traversal below; match construction uses the same
// pattern classification for the implicit-parameter dead-arm lint.
static bool match_arm_is_error_handler(AstMatchArm* arm);
static bool match_has_error_handler(AstMatchNode* match);

// Forward declaration for apply; (splat) statement
AstNode* build_apply_stam(Transpiler* tp, TSNode apply_node);

// Forward declaration for object type building (used by pub type)
AstNode* build_object_type(Transpiler* tp, TSNode type_node);

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

static bool typed_array_literal_elements_compatible(AstNode* node, Type* expected_elem) {
    while (node && node->node_type == AST_NODE_PRIMARY) {
        AstPrimaryNode* primary = (AstPrimaryNode*)node;
        if (!primary->expr) break;
        node = primary->expr;
    }
    if (!node || node->node_type != AST_NODE_ARRAY) return true;
    AstNode* item = ((AstArrayNode*)node)->item;
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

static size_t type_clone_size(const Type* source) {
    if (!source) return sizeof(Type);
    switch (source->type_id) {
    case LMD_TYPE_INT64: return sizeof(TypeInt64);
    case LMD_TYPE_UINT64: return sizeof(TypeUint64);
    case LMD_TYPE_FLOAT:
    case LMD_TYPE_FLOAT64: return sizeof(TypeFloat);
    case LMD_TYPE_COMPLEX: return sizeof(TypeComplex);
    case LMD_TYPE_DECIMAL: return sizeof(TypeDecimal);
    case LMD_TYPE_DTIME: return sizeof(TypeDateTime);
    case LMD_TYPE_SYMBOL:
    case LMD_TYPE_STRING: return sizeof(TypeString);
    case LMD_TYPE_BINARY: return sizeof(TypeBinaryConst);
    case LMD_TYPE_NUM_SIZED: return sizeof(TypeNumSized);
    case LMD_TYPE_FUNC: return sizeof(TypeFunc);
    case LMD_TYPE_RANGE: return sizeof(TypeRange);
    default: break;
    }
    if (source->type_id == LMD_TYPE_TYPE) {
        switch (source->kind) {
        case TYPE_KIND_UNARY: return sizeof(TypeUnary);
        case TYPE_KIND_BINARY: return sizeof(TypeBinary);
        case TYPE_KIND_CONSTRAINED: return sizeof(TypeConstrained);
        case TYPE_KIND_RANGE: return sizeof(TypeRange);
        case TYPE_KIND_PATTERN: return sizeof(TypePattern);
        default: return sizeof(TypeType);
        }
    }
    if (source->kind == TYPE_KIND_PARAM) return sizeof(TypeParam);
    return sizeof(Type);
}

static Type* clone_type_without_const(Transpiler* tp, Type* source) {
    if (!source) return &TYPE_ANY;
    size_t size = type_clone_size(source);
    Type* clone = alloc_type(tp->pool, source->type_id, size);
    // Preserve the concrete type wrapper: cloning only the Type prefix leaves
    // TypeType/union metadata outside the allocation and contract inspection
    // then reads past the block. The old allocator's size classes hid this.
    memcpy(clone, source, size);
    clone->is_const = 0;
    return clone;
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
    case LMD_TYPE_FLOAT64:
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

static inline bool is_static_spread_scalar_type_id(TypeId type_id) {
    return type_id == LMD_TYPE_NULL || type_id == LMD_TYPE_BOOL ||
           is_numeric_type_id(type_id) || type_id == LMD_TYPE_DTIME ||
           is_text_type_id(type_id);
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
        StrView text = ts_node_source(tp, node->node);
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
        StrView source = ts_node_source(tp, node->node);
        char* num_str = (char*)mem_alloc(source.length + 1, MEM_CAT_AST);
        memcpy(num_str, source.str, source.length);
        num_str[source.length] = '\0';
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
    case LMD_TYPE_FLOAT: case LMD_TYPE_FLOAT64: return item.get_double() == 0.0;
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
    if (!function || !function->type || function->type->type_id != LMD_TYPE_TYPE) return NULL;
    TypeType* type_type = (TypeType*)function->type;
    return type_type->type;
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

// Record a semantic error with error code
void record_semantic_error(Transpiler* tp, TSNode node, LambdaErrorCode code, const char* format, ...) {
    // Get location from TSNode
    TSPoint start = ts_node_start_point(node);
    TSPoint end = ts_node_end_point(node);

    // Format error message
    char error_msg[512];
    va_list args;
    va_start(args, format);
    vsnprintf(error_msg, sizeof(error_msg), format, args);
    va_end(args);

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
                record_semantic_error(tp, declaration->node, ERR_UNDEFINED_FIELD,
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

static void check_compound_assignment_static_boundary(Transpiler* tp, TSNode node,
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
    record_semantic_error(tp, node, ERR_TYPE_MISMATCH,
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

static void validate_compound_mutable_root(Transpiler* tp, TSNode assign_node, AstNode* object) {
    AstIdentNode* root = compound_root_ident(object);
    if (!root || !root->entry) return;
    if (root->entry->is_mutable) return;
    // Interior writes are writes to the root binding for Lambda's mutability
    // model; allowing them through a let root makes aliases observe mutation.
    record_semantic_error(tp, assign_node, ERR_IMMUTABLE_ASSIGNMENT,
        "cannot mutate through immutable binding '%.*s'. declare it with `var` or pass it as `var`.",
        (int)root->name->len, root->name->chars);
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
                record_semantic_error(tp, fn_node->node, ERR_IMMUTABLE_ASSIGNMENT,
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

AstNode* alloc_ast_node(Transpiler* tp, AstNodeType node_type, TSNode node, size_t size) {
    AstNode* ast_node = (AstNode*)pool_alloc(tp->pool, size);
    memset(ast_node, 0, size);
    ast_node->node_type = node_type;  ast_node->node = node;
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
static StrView node_name_text(Transpiler* tp, TSNode node) {
    StrView text = ts_node_source(tp, node);
    if (ts_node_symbol(node) == SYM_SYMBOL && text.length >= 2) {
        text.str++;
        text.length -= 2;
    }
    return text;
}

static String* canonical_dotted_name(Transpiler* tp, TSNode dotted_node) {
    // S2.4.3v2 allows grammar extras around a namespace dot, but whitespace
    // is not part of the qualified identity; assemble it from segment nodes.
    StrBuf* canonical = strbuf_new();
    uint32_t count = ts_node_named_child_count(dotted_node);
    bool has_segment = false;
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(dotted_node, i);
        TSSymbol symbol = ts_node_symbol(child);
        if (symbol != SYM_IDENT && symbol != SYM_SYMBOL) continue;
        StrView segment = node_name_text(tp, child);
        if (has_segment) strbuf_append_char(canonical, '.');
        strbuf_append_str_n(canonical, segment.str, segment.length);
        has_segment = true;
    }
    String* result = name_pool_create_strview(tp->name_pool,
        (StrView){.str = canonical->str, .length = canonical->length});
    strbuf_free(canonical);
    return result;
}

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
    return strview_equal(&name, "last");
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
    if (is_reserved_identifier_keyword(name_view)) {
        int line = ts_node_start_point(node->node).row + 1;
        // c15 reserves last globally so it cannot escape its two grammar homes via declarations.
        record_type_error(tp, line, "Error: '%.*s' is a reserved keyword and cannot be used as a name",
            (int)name_view.length, name_view.str);
    }

    // check for duplicate definition in current scope
    NameEntry* existing = lookup_name_in_current_scope(tp, node->name);
    if (existing) {
        record_semantic_error(tp, node->node, ERR_DUPLICATE_DEFINITION,
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

// Parenthesized lets: (let x = a, let y = b, expr) — sequential bindings returning the last expr.
AstNode* build_let_block(Transpiler* tp, TSNode block_node) {
    log_debug("build let_block expr");
    AstListNode* ast_node = (AstListNode*)alloc_ast_node(tp, AST_NODE_LIST, block_node, sizeof(AstListNode));
    TypeList* type = (TypeList*)alloc_type(tp->pool, LMD_TYPE_ARRAY, sizeof(TypeList));
    ast_node->list_type = type;

    // push scope for let bindings
    NameScope* scope = (NameScope*)pool_calloc(tp->pool, sizeof(NameScope));
    scope->parent = tp->current_scope;
    tp->current_scope = scope;
    ast_node->vars = scope;

    // process children: let_expr nodes are declarations, last non-let child is the body
    AstNode* prev_declare = NULL;
    AstNode* body = NULL;
    TSNode child = ts_node_named_child(block_node, 0);
    while (!ts_node_is_null(child)) {
        TSSymbol sym = ts_node_symbol(child);
        TSNode next = ts_node_next_named_sibling(child);
        if (sym == sym_let_expr) {
            AstNode* decl = build_let_expr(tp, child);
            if (decl) {
                if (!prev_declare)
                    ast_node->declare = decl;
                else
                    prev_declare->next = decl;
                prev_declare = decl;
            }
        } else {
            // last expression is the body
            body = build_expr(tp, child);
        }
        child = next;
    }

    ast_node->item = body;
    type->length = body ? 1 : 0;
    ast_node->type = body ? body->type : alloc_type(tp->pool, LMD_TYPE_NULL, sizeof(Type));

    // pop scope
    tp->current_scope = scope->parent;
    return (AstNode*)ast_node;
}

AstNode* build_array(Transpiler* tp, TSNode array_node) {
    log_debug("build array expr");
    AstArrayNode* ast_node = (AstArrayNode*)alloc_ast_node(tp, AST_NODE_ARRAY, array_node, sizeof(AstArrayNode));
    ast_node->type = alloc_type(tp->pool, LMD_TYPE_ARRAY, sizeof(TypeArray));
    TypeArray* type = (TypeArray*)ast_node->type;

    // check if array has let expressions - push scope to contain their bindings
    bool has_let = false;
    {
        TSNode scan = ts_node_named_child(array_node, 0);
        while (!ts_node_is_null(scan)) {
            if (ts_node_symbol(scan) == sym_let_expr) {
                has_let = true;
                break;
            }
            scan = ts_node_next_named_sibling(scan);
        }
    }
    NameScope* saved_scope = tp->current_scope;
    if (has_let) {
        NameScope* scope = (NameScope*)pool_calloc(tp->pool, sizeof(NameScope));
        scope->parent = tp->current_scope;
        tp->current_scope = scope;
    }

    TSNode child = ts_node_named_child(array_node, 0);
    AstNode* prev_item = NULL;  Type* nested_type = NULL;
    while (!ts_node_is_null(child)) {
        AstNode* item = build_expr(tp, child);
        if (item) {
            if (!prev_item) {
                ast_node->item = item;
                // let bindings are transparent - they don't contribute array elements
                if (item->node_type != AST_NODE_ASSIGN) {
                    nested_type = item->type;
                    type->length++;
                }
                log_debug("DEBUG: First array item type_id: %d", item->type ? item->type->type_id : -1);
            }
            else {
                prev_item->next = item;
                // let bindings are transparent - skip for type/length tracking
                if (item->node_type != AST_NODE_ASSIGN) {
                    log_debug("DEBUG: Array item type_id: %d, nested_type_id: %d",
                        item->type ? item->type->type_id : -1, nested_type ? nested_type->type_id : -1);
                    if (nested_type && item->type->type_id != nested_type->type_id) {
                        log_debug("DEBUG: Type mismatch, resetting nested_type to NULL");
                        nested_type = NULL;
                    }
                    // for NUM_SIZED, also check sub-type uniformity
                    else if (nested_type && nested_type->type_id == LMD_TYPE_NUM_SIZED) {
                        NumSizedType a_num_type = type_num_sized_kind(nested_type);
                        NumSizedType b_num_type = type_num_sized_kind(item->type);
                        if (a_num_type != b_num_type) {
                            log_debug("DEBUG: NUM_SIZED sub-type mismatch (%d vs %d), resetting nested_type to NULL",
                                a_num_type, b_num_type);
                            nested_type = NULL;
                        }
                    }
                    type->length++;
                }
            }
            prev_item = item;
        }
        child = ts_node_next_named_sibling(child);
    }

    if (has_let) {
        tp->current_scope = saved_scope;
    }

    type->nested = nested_type;
    log_debug("DEBUG: Final array nested_type_id: %d", nested_type ? nested_type->type_id : -1);
    return (AstNode*)ast_node;
}

// check if an identifier is a path scheme keyword
// returns the PathScheme if it is, or -1 if not
static int get_path_scheme_from_name(StrView name) {
    if (strview_equal(&name, "file")) return PATH_SCHEME_FILE;
    if (strview_equal(&name, "http")) return PATH_SCHEME_HTTP;
    if (strview_equal(&name, "https")) return PATH_SCHEME_HTTPS;
    if (strview_equal(&name, "sys")) return PATH_SCHEME_SYS;
    return -1;  // not a path scheme
}

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
static NamespaceEntry* lookup_namespace_strview(Transpiler* tp, StrView prefix) {
    NamespaceEntry* entry = tp->namespaces;
    while (entry) {
        if (entry->prefix->len == prefix.length &&
            memcmp(entry->prefix->chars, prefix.str, prefix.length) == 0) {
            return entry;
        }
        entry = entry->next;
    }
    return NULL;
}

// check if a member_expr chain starts with a path scheme (file, http, https, sys)
// and collect all segment names if so
// returns the PathScheme if it's a path, or -1 if it's a regular member expression
static void append_path_segment(Transpiler* tp, TSNode field_node, ArrayList* segments) {
    TSSymbol field_sym = ts_node_symbol(field_node);
    if (field_sym == SYM_IDENT || field_sym == SYM_SYMBOL) {
        StrView field_name = ts_node_source(tp, field_node);
        if (field_sym == SYM_SYMBOL && field_name.length >= 2) {
            field_name.str++;
            field_name.length -= 2;
        }
        AstPathSegment* seg = (AstPathSegment*)pool_alloc(tp->pool, sizeof(AstPathSegment));
        seg->name = name_pool_create_strview(tp->name_pool, field_name);
        seg->type = LPATH_SEG_NORMAL;
        seg->int_value = 0;
        arraylist_append(segments, seg);
    }
    else if (field_sym == SYM_INT) {
        StrView source = ts_node_source(tp, field_node);
        char* text = (char*)mem_alloc(source.length + 1, MEM_CAT_AST);
        memcpy(text, source.str, source.length);
        text[source.length] = '\0';
        int64_t value = 0;
        bool valid = lambda_parse_int_literal(text, &value);
        mem_free(text);
        if (!valid || value < 0) return;
        AstPathSegment* seg = (AstPathSegment*)pool_alloc(tp->pool, sizeof(AstPathSegment));
        seg->name = NULL;
        seg->type = LPATH_SEG_INT;
        seg->int_value = value;
        arraylist_append(segments, seg);
    }
    else if (field_sym == SYM_PATH_WILDCARD) {
        StrView wc_src = ts_node_source(tp, field_node);
        AstPathSegment* seg = (AstPathSegment*)pool_alloc(tp->pool, sizeof(AstPathSegment));
        seg->name = NULL;
        seg->type = (wc_src.length == 2) ? LPATH_SEG_WILDCARD_REC : LPATH_SEG_WILDCARD;
        seg->int_value = 0;
        arraylist_append(segments, seg);
    }
    else if (field_sym == SYM_PATH_PARENT || field_sym == SYM_PATH_ROOT) {
        AstPathSegment* seg = (AstPathSegment*)pool_alloc(tp->pool, sizeof(AstPathSegment));
        seg->name = NULL;
        seg->type = field_sym == SYM_PATH_PARENT ? LPATH_SEG_PARENT : LPATH_SEG_ROOT;
        seg->int_value = 0;
        arraylist_append(segments, seg);
    }
}

static int collect_path_segments_if_path(Transpiler* tp, TSNode node, ArrayList* segments) {
    TSSymbol symbol = ts_node_symbol(node);

    // path_expr: _path_prefix optional(field)
    if (symbol == SYM_PATH_EXPR) {
        // `/` is the logical root; `.` is the active relative root.
        StrView source = ts_node_source(tp, node);
        if (source.length >= 2 && source.str[0] == '.' && source.str[1] == '.') {
            record_semantic_error(tp, node, ERR_SYNTAX_ERROR,
                "legacy '..' path syntax is not supported; use '.~~'");
        }
        int scheme;
        if (source.length >= 1 && source.str[0] == '/') {
            scheme = PATH_SCHEME_LOGICAL;
        } else {
            scheme = PATH_SCHEME_REL;
        }

        // check for optional field
        TSNode field_node = ts_node_child_by_field_id(node, FIELD_FIELD);
        if (!ts_node_is_null(field_node)) {
            append_path_segment(tp, field_node, segments);
        }
        return scheme;
    }

    if (symbol == SYM_IDENT) {
        // base case: check if this identifier is a path scheme (http, https, sys)
        StrView name = ts_node_source(tp, node);
        int scheme = get_path_scheme_from_name(name);
        if (scheme >= 0) {
            // it's a path scheme root, don't add it to segments
            return scheme;
        }
        return -1;  // regular identifier, not a path
    }
    else if (symbol == SYM_MEMBER_EXPR) {
        // recursive case: check the object (left side)
        TSNode object_node = ts_node_child_by_field_id(node, FIELD_OBJECT);
        TSNode field_node = ts_node_child_by_field_id(node, FIELD_FIELD);

        // first recurse to check if the object is a path
        int scheme = collect_path_segments_if_path(tp, object_node, segments);
        if (scheme < 0) {
            // `file./` remains an absolute provider path even though `/` is
            // now a member field rather than a separate navigation CST node.
            StrView object_source = ts_node_source(tp, object_node);
            if (ts_node_symbol(object_node) == SYM_IDENT &&
                    object_source.length == 4 &&
                    strncmp(object_source.str, "file", 4) == 0 &&
                    ts_node_symbol(field_node) == SYM_PATH_ROOT) {
                scheme = PATH_SCHEME_FILE;
            }
        }
        if (scheme >= 0) {
            StrView source = ts_node_source(tp, node);
            if (source.length >= 2 && source.str[0] == '.' && source.str[1] == '.') {
                record_semantic_error(tp, node, ERR_SYNTAX_ERROR,
                    "legacy '..' path syntax is not supported; use '.~~'");
            }
            // the object is part of a path, add the field as a segment
            // field can be identifier, symbol, or wildcard
            append_path_segment(tp, field_node, segments);
            return scheme;
        }
        return -1;  // not a path
    }
    else if (symbol == SYM_PRIMARY_EXPR) {
        // unwrap primary_expr
        TSNode child = ts_node_named_child(node, 0);
        if (!ts_node_is_null(child)) {
            return collect_path_segments_if_path(tp, child, segments);
        }
        return -1;
    }

    return -1;  // unknown node type, not a path
}

// build a path expression from a member_expr chain that starts with a path scheme
static AstNode* build_path_expr(Transpiler* tp, TSNode node, PathScheme scheme, ArrayList* segments) {
    log_debug("build_path_expr: scheme=%d, segment_count=%d", scheme, segments->length);

    StrView source = ts_node_source(tp, node);
    bool file_local = scheme == PATH_SCHEME_FILE && source.length >= 6 &&
        strncmp(source.str, "file./", 6) == 0;
    int first_segment = 0;
    String* authority = NULL;
    if (scheme == PATH_SCHEME_FILE && !file_local && segments->length > 0) {
        AstPathSegment* authority_segment = (AstPathSegment*)segments->data[0];
        if (authority_segment->type == LPATH_SEG_NORMAL && authority_segment->name) {
            authority = authority_segment->name;
            first_segment = 1;
        }
    }
    return build_static_path_ast(tp, node, scheme, authority, segments, first_segment);
}



// both index and member exprs
AstNode* build_field_expr(Transpiler* tp, TSNode array_node, AstNodeType node_type) {
    log_debug("build field expr");
    AstFieldNode* ast_node = (AstFieldNode*)alloc_ast_node(tp, node_type, array_node, sizeof(AstFieldNode));
    TSNode object_node = ts_node_child_by_field_id(array_node, FIELD_OBJECT);
    ast_node->object = build_expr(tp, object_node);

    TSNode field_node = ts_node_child_by_field_id(array_node, FIELD_FIELD);
    if (node_type == AST_NODE_MEMBER_EXPR && (ts_node_symbol(field_node) == SYM_IDENT || ts_node_symbol(field_node) == SYM_BASE_TYPE)) {
        // handle id node directly without name lookup
        AstIdentNode* id_node = (AstIdentNode*)alloc_ast_node(tp, AST_NODE_IDENT, field_node, sizeof(AstIdentNode));
        StrView var_name = ts_node_source(tp, field_node);
        id_node->name = name_pool_create_strview(tp->name_pool, var_name);
        log_debug("member expr field name: '%.*s'", (int)id_node->name->len, id_node->name->chars);
        ast_node->field = (AstNode*)id_node;

        // In v2 namespaces, e.ns.attr is chained access: (e.ns).attr
        // The sub-map desugaring stores ns attrs as ns: {attr: val}, so
        // e.ns returns the sub-map and .attr accesses the key within it.
        // No merging needed — just check for ns.value → qualified symbol.
        if (ast_node->object && ast_node->object->node_type == AST_NODE_PRIMARY) {
            AstPrimaryNode* pri = (AstPrimaryNode*)ast_node->object;
            // Check if object is just a namespace prefix identifier (for ns.value syntax)
            // e.g., ns.value where ns is a namespace prefix
            if (pri->expr && pri->expr->node_type == AST_NODE_IDENT) {
                AstIdentNode* ns_ident = (AstIdentNode*)pri->expr;
                NamespaceEntry* ns_entry = lookup_namespace(tp, ns_ident->name);
                if (ns_entry) {
                    // ns.value becomes a symbol literal with qualified name
                    // Create qualified name: ns.value
                    size_t ns_len = ns_ident->name->len;
                    size_t val_len = id_node->name->len;
                    size_t total_len = ns_len + 1 + val_len;  // ns.value
                    char* buf = (char*)pool_alloc(tp->pool, total_len + 1);
                    memcpy(buf, ns_ident->name->chars, ns_len);
                    buf[ns_len] = '.';
                    memcpy(buf + ns_len + 1, id_node->name->chars, val_len);
                    buf[total_len] = '\0';
                    log_debug("namespace symbol created: '%.*s'", (int)total_len, buf);

                    // Build a symbol type with the qualified name
                    TypeString* sym_type = (TypeString*)alloc_type(tp->pool, LMD_TYPE_SYMBOL, sizeof(TypeString));
                    sym_type->is_const = 1;
                    sym_type->is_literal = 1;
                    // Allocate Symbol struct (has ns field)
                    Symbol* sym = (Symbol*)pool_alloc(tp->pool, sizeof(Symbol) + total_len + 1);
                    sym->ns = ns_entry->target;  // set namespace target
                    sym->len = total_len;
                    memcpy(sym->chars, buf, total_len);
                    sym->chars[total_len] = '\0';
                    sym_type->string = (String*)sym;
                    arraylist_append(tp->const_list, sym);
                    sym_type->const_index = tp->const_list->length - 1;

                    // Return a primary node with symbol type
                    AstPrimaryNode* sym_node = (AstPrimaryNode*)alloc_ast_node(tp, AST_NODE_PRIMARY, array_node, sizeof(AstPrimaryNode));
                    sym_node->type = (Type*)sym_type;
                    return (AstNode*)sym_node;
                }

                // Check for math module constants: math.pi, math.e (also via alias)
                StrView ns_view = {ns_ident->name->chars, (size_t)ns_ident->name->len};
                const char* ns_resolved = resolve_imported_module(tp, &ns_view);
                if (ns_resolved && strcmp(ns_resolved, "math") == 0) {
                    if (id_node->name->len == 7 && memcmp(id_node->name->chars, "max_int", 7) == 0) {
                        TypeInt64* it = (TypeInt64*)alloc_type(tp->pool, LMD_TYPE_INT, sizeof(TypeInt64));
                        it->int64_val = INT53_MAX;
                        arraylist_append(tp->const_list, &it->int64_val);
                        it->const_index = tp->const_list->length - 1;
                        it->is_const = 1;  it->is_literal = 1;
                        AstPrimaryNode* pn = (AstPrimaryNode*)alloc_ast_node(tp, AST_NODE_PRIMARY, array_node, sizeof(AstPrimaryNode));
                        pn->type = (Type*)it;
                        return (AstNode*)pn;
                    }
                    double const_val = 0.0;
                    bool is_math_const = false;
                    if (id_node->name->len == 2 && memcmp(id_node->name->chars, "pi", 2) == 0) {
                        const_val = 3.14159265358979323846;
                        is_math_const = true;
                    } else if (id_node->name->len == 1 && id_node->name->chars[0] == 'e') {
                        const_val = 2.71828182845904523536;
                        is_math_const = true;
                    }
                    if (is_math_const) {
                        TypeFloat* ft = (TypeFloat*)alloc_type(tp->pool, LMD_TYPE_FLOAT, sizeof(TypeFloat));
                        ft->double_val = const_val;
                        arraylist_append(tp->const_list, &ft->double_val);
                        ft->const_index = tp->const_list->length - 1;
                        ft->is_const = 1;  ft->is_literal = 1;
                        AstPrimaryNode* pn = (AstPrimaryNode*)alloc_ast_node(tp, AST_NODE_PRIMARY, array_node, sizeof(AstPrimaryNode));
                        pn->type = (Type*)ft;
                        return (AstNode*)pn;
                    }
                }

                // Check if object is an aliased import prefix (e.g., helper.val)
                // by looking up the qualified name "object.field" in scope
                size_t obj_len = ns_ident->name->len;
                size_t fld_len = id_node->name->len;
                size_t q_len = obj_len + 1 + fld_len;
                char q_buf[256];
                if (q_len < sizeof(q_buf)) {
                    memcpy(q_buf, ns_ident->name->chars, obj_len);
                    q_buf[obj_len] = '.';
                    memcpy(q_buf + obj_len + 1, id_node->name->chars, fld_len);
                    q_buf[q_len] = '\0';
                    StrView q_view = {q_buf, q_len};
                    NameEntry* q_entry = lookup_name(tp, q_view);
                    if (q_entry && q_entry->import) {
                        log_debug("aliased import var resolved: %s", q_buf);
                        // build as a direct identifier reference
                        AstIdentNode* resolved = (AstIdentNode*)alloc_ast_node(
                            tp, AST_NODE_IDENT, array_node, sizeof(AstIdentNode));
                        resolved->name = q_entry->name;
                        resolved->entry = q_entry;
                        if (q_entry->import && q_entry->node->type->type_id != LMD_TYPE_FUNC) {
                            // For container types (array, list, map, element, etc.), use the
                            // original type directly to preserve nested type info (e.g.
                            // TypeArray::nested). Allocating a bare Type loses this info,
                            // causing wrong accessor functions (e.g. array_get vs array_int_get).
                            Type* orig = q_entry->node->type;
                            TypeId tid = orig->type_id;
                            if (tid >= LMD_TYPE_CONTAINER) {
                                resolved->type = orig;
                            } else {
                                resolved->type = alloc_type(tp->pool, tid, sizeof(Type));
                                resolved->type->is_const = 0;
                            }
                        } else {
                            resolved->type = q_entry->node->type ? q_entry->node->type : &TYPE_ANY;
                        }
                        // wrap in primary node to match expected AST structure
                        AstPrimaryNode* pri_node = (AstPrimaryNode*)alloc_ast_node(
                            tp, AST_NODE_PRIMARY, array_node, sizeof(AstPrimaryNode));
                        pri_node->expr = (AstNode*)resolved;
                        pri_node->type = resolved->type;
                        return (AstNode*)pri_node;
                    }
                }
            }
        }
    }
    else {
        AstNode* old_last_object = tp->last_index_object;
        if (node_type == AST_NODE_INDEX_EXPR) {
            tp->subscript_depth++;
            tp->last_index_object = ast_node->object;
        }
        ast_node->field = build_expr(tp, field_node);
        if (node_type == AST_NODE_INDEX_EXPR) {
            tp->subscript_depth--;
            tp->last_index_object = old_last_object;
        }
    }

    // For index_expr, collect additional comma-separated field children into a
    // chain via ->next, enabling multi-dim subscripts like arr[i, j, k].
    if (node_type == AST_NODE_INDEX_EXPR && ast_node->field) {
        AstNode* tail = ast_node->field;
        TSTreeCursor cur = ts_tree_cursor_new(array_node);
        bool ok = ts_tree_cursor_goto_first_child(&cur);
        bool seen_first = false;
        while (ok) {
            TSSymbol fid = ts_tree_cursor_current_field_id(&cur);
            if (fid == FIELD_FIELD) {
                if (!seen_first) {
                    seen_first = true;  // first FIELD_FIELD was already built above
                } else {
                    TSNode extra_field = ts_tree_cursor_current_node(&cur);
                    AstNode* old_last_object = tp->last_index_object;
                    tp->subscript_depth++;
                    tp->last_index_object = ast_node->object;
                    AstNode* next_idx = build_expr(tp, extra_field);
                    tp->subscript_depth--;
                    tp->last_index_object = old_last_object;
                    if (next_idx) {
                        tail->next = next_idx;
                        tail = next_idx;
                    }
                }
            }
            ok = ts_tree_cursor_goto_next_sibling(&cur);
        }
        ts_tree_cursor_delete(&cur);
    }

    // defensive check: if either object or field building failed, return error
    if (!ast_node->object || !ast_node->field) {
        record_semantic_error(tp, array_node, ERR_SYNTAX_ERROR,
            "Failed to build field expression - invalid object or field");
        ast_node->type = &TYPE_ERROR;
        return (AstNode*)ast_node;
    }
    // additional safety: check if object has valid type
    if (!ast_node->object->type) {
        record_semantic_error(tp, array_node, ERR_INTERNAL_ERROR,
            "Field expression object missing type information");
        ast_node->type = &TYPE_ERROR;
        return (AstNode*)ast_node;
    }

    Type* declared_index_type = node_type == AST_NODE_INDEX_EXPR
        ? declared_compound_destination_type(tp, (AstNode*)ast_node, NULL) : NULL;
    if (declared_index_type) {
        // Indexing is total: an absent element contributes null even when the
        // array's element contract is plain T. Keep T? in the AST rather than
        // erasing it to any; MIR can then select the matching nullable lane.
        ast_node->type = lambda_type_nullable_normalized(tp->pool,
            declared_index_type);
        return (AstNode*)ast_node;
    }

    TypeId obj_tid = ast_node->object->type->type_id;
    if (obj_tid == LMD_TYPE_ARRAY_NUM || obj_tid == LMD_TYPE_ARRAY) {
        // TIG1: publish the array's recorded element type as `T?` — the same
        // answer the declared-destination path above gives, with D2.5.3's
        // nullability for an unproven index (an OOB read is total null, S7.1).
        TypeArray* arr = (TypeArray*)ast_node->object->type;
        Type* elem = !is_global_simple_type(ast_node->object->type) ? arr->nested : NULL;
        // A `var` array's element contract is not stable: its elements can be
        // rewritten by any later store, and the literal's own element type
        // goes stale the moment they are. `var vxy = [null, null]` filled with
        // ints by a helper is the corpus case (awfy/cd2_orig) — publishing the
        // literal's `null` element made a later `var vvx: int = vxy[0]` a type
        // error against a value that is an int at runtime. This is D3.3.3's
        // rule ("container element-type narrowing dies with its binding") at
        // the AST level, and it mirrors the same guard
        // `mir_known_index_element_type` applies for mutable bindings.
        AstNode* object_base = unwrap_primary_node(ast_node->object);
        bool object_is_mutable_binding = object_base &&
            object_base->node_type == AST_NODE_IDENT &&
            ((AstIdentNode*)object_base)->entry &&
            (((AstIdentNode*)object_base)->entry->is_mutable ||
             ((AstIdentNode*)object_base)->entry->type_widened);
        AstNode* index_expr = unwrap_primary_node(ast_node->field);
        TypeId index_tid = index_expr && index_expr->type
            ? index_expr->type->type_id : LMD_TYPE_ANY;
        bool range_index = index_expr &&
            ((index_expr->node_type == AST_NODE_BINARY &&
              ((AstBinaryNode*)index_expr)->op == OPERATOR_TO) ||
             index_tid == LMD_TYPE_RANGE);
        bool query_index = index_expr &&
            (index_tid == LMD_TYPE_TYPE || index_tid == LMD_TYPE_ARRAY ||
             index_tid == LMD_TYPE_ARRAY_NUM ||
             index_expr->node_type == AST_NODE_TYPE ||
             index_expr->node_type == AST_NODE_ARRAY_TYPE ||
             index_expr->node_type == AST_NODE_LIST_TYPE ||
             index_expr->node_type == AST_NODE_MAP_TYPE ||
             index_expr->node_type == AST_NODE_ELMT_TYPE ||
             index_expr->node_type == AST_NODE_FUNC_TYPE ||
             index_expr->node_type == AST_NODE_BINARY_TYPE ||
             index_expr->node_type == AST_NODE_UNARY_TYPE ||
             index_expr->node_type == AST_NODE_CONTENT_TYPE);
        if (elem && !range_index && !query_index &&
                elem->type_id != LMD_TYPE_ANY && elem->type_id != LMD_TYPE_TYPE &&
                !object_is_mutable_binding) {
            ast_node->type = lambda_type_nullable_normalized(tp->pool, elem);
        } else {
            ast_node->type = set_type_any(tp, ANY_INDEX_ELEM);
        }
    }
    else if (obj_tid == LMD_TYPE_BINARY) {
        // Scalar binary indexes are u8 values; range indexes retain binary type.
        ast_node->type = ast_node->field->type && ast_node->field->type->type_id == LMD_TYPE_RANGE
            ? &TYPE_BINARY : &TYPE_U8;
    }
    else if ((ast_node->object->type->type_id == LMD_TYPE_MAP
           || ast_node->object->type->type_id == LMD_TYPE_OBJECT) &&
            !is_global_simple_type(ast_node->object->type)) {
        // resolve field type from map/object shape for unboxed access optimization
        // The global `map`/`object` contracts have only the compact Type prefix;
        // never read them as a shaped TypeMap while inferring a dynamic field.
        TypeMap* map_type = (TypeMap*)ast_node->object->type;
        if (map_type->struct_name && map_type->shape
            && ast_node->field && ast_node->field->node_type == AST_NODE_IDENT) {
            AstIdentNode* field_id = (AstIdentNode*)ast_node->field;
            Type* resolved_type = NULL;
            FOR_EACH_MAP_FIELD(map_type, se) {
                if (se->name && (int)se->name->length == (int)field_id->name->len
                    && strncmp(se->name->str, field_id->name->chars, se->name->length) == 0) {
                    // found — unwrap TypeType for type-defined maps
                    // TypeBinary shares LMD_TYPE_TYPE with TypeType but has
                    // no nested `type` member to unwrap.
                    Type* ft = unwrap_simple_type_type(se->type);
                    resolved_type = ft;
                    break;
                }
            }
            if (resolved_type) {
                TypeId rid = resolved_type->type_id;
                // Scalars are re-allocated as a bare Type so the field's own
                // node is not aliased into the expression graph.
                if (is_native_numeric_type_id(rid)
                    || rid == LMD_TYPE_BOOL || rid == LMD_TYPE_STRING) {
                    ast_node->type = alloc_type(tp->pool, rid, sizeof(Type));
                } else if (rid == LMD_TYPE_MAP || rid == LMD_TYPE_ELEMENT ||
                        rid == LMD_TYPE_OBJECT) {
                    // Container fields keep the shape recorded on the map, which
                    // is what makes chained access (`a.b.c`) resolve instead of
                    // dying at the first non-scalar hop [TIG2]. Representation
                    // is unaffected: a container field is a boxed pointer on
                    // both paths, so this publishes a type the emitter already
                    // produces (unlike the numeric-lane element reads of TIG1).
                    ast_node->type = resolved_type;
                } else {
                    ast_node->type = set_type_any(tp, ANY_MEMBER_SHAPE);
                }
            } else {
                ast_node->type = set_type_any(tp, ANY_MEMBER_SHAPE);  // field not in shape (e.g. method name)
            }
        } else {
            ast_node->type = set_type_any(tp, ANY_MEMBER_SHAPE);
        }
    }
    else {
        ast_node->type = set_type_any(tp, ANY_MEMBER_SHAPE);
    }
    return (AstNode*)ast_node;
}

static AstNode* build_navigation_expr(Transpiler* tp, TSNode node);

// Left-recursive member chains place member_expr directly in object; both CST
// entry paths must preserve the same path-versus-field interpretation.
static AstNode* build_member_expr(Transpiler* tp, TSNode member_node) {
    StrView source = ts_node_source(tp, member_node);
    AstNode* direct_path = try_parse_path_expr_text(tp, source.str,
        source.str + source.length, member_node);
    if (direct_path) {
        // Provider schemes begin as identifiers, so their CST is a member
        // chain; prefer the shared text parser before treating it as a field.
        return direct_path;
    }

    ArrayList* segments = arraylist_new(8);
    int scheme = collect_path_segments_if_path(tp, member_node, segments);
    TSNode field_node = ts_node_child_by_field_id(member_node, FIELD_FIELD);
    TSSymbol field_symbol = ts_node_symbol(field_node);
    AstNode* result = NULL;
    if (scheme >= 0) {
        result = build_path_expr(tp, member_node, (PathScheme)scheme, segments);
    }
    else if (field_symbol == SYM_PATH_PARENT || field_symbol == SYM_PATH_ROOT) {
        result = build_navigation_expr(tp, member_node);
    }
    else {
        result = build_field_expr(tp, member_node, AST_NODE_MEMBER_EXPR);
    }
    arraylist_free(segments);
    return result;
}

// Forward declaration: check if AST node contains ~ (current_item) reference
bool has_current_item_ref(AstNode* node);

// Helper: check if TSNode contains ~ (current_item) reference before building AST
// This is used to determine if pipe expression needs argument injection
static bool tsnode_has_current_item_ref(Transpiler* tp, TSNode node) {
    if (ts_node_is_null(node)) return false;

    TSSymbol symbol = ts_node_symbol(node);

    // Check for current_expr (~ or ~#) and its contextual parent shorthand.
    if (symbol == sym_current_expr || symbol == sym_current_parent_expr) {
        return true;
    }

    // Recursively check children
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        if (tsnode_has_current_item_ref(tp, child)) {
            return true;
        }
    }

    return false;
}

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

static bool parse_start_options(Transpiler* tp, AstNode* options,
        TSNode call_node, StartMode* mode) {
    *mode = START_MODE_TASK;
    if (!options) return true;
    AstNode* root = ast_unwrap_primary(options);
    if (!root || root->node_type != AST_NODE_MAP) {
        record_semantic_error(tp, call_node, ERR_INVALID_OPERATION,
            "`start` options must be a map literal");
        return false;
    }

    bool saw_mode = false;
    bool valid = true;
    for (AstNode* item = ((AstMapNode*)root)->item; item; item = item->next) {
        if (item->node_type != AST_NODE_KEY_EXPR) {
            record_semantic_error(tp, call_node, ERR_INVALID_OPERATION,
                "`start` options cannot use spread fields");
            valid = false;
            continue;
        }
        AstNamedNode* option = (AstNamedNode*)item;
        if (!start_option_name_is(option, "mode")) {
            record_semantic_error(tp, call_node, ERR_INVALID_OPERATION,
                "unknown `start` option '%.*s'",
                option->name ? option->name->len : 0,
                option->name ? option->name->chars : "");
            valid = false;
            continue;
        }
        if (saw_mode) {
            record_semantic_error(tp, call_node, ERR_INVALID_OPERATION,
                "duplicate `start` option 'mode'");
            valid = false;
            continue;
        }
        saw_mode = true;
        const char* value = start_literal_symbol(option->as);
        if (!value) {
            record_semantic_error(tp, call_node, ERR_INVALID_OPERATION,
                "`start` option 'mode' must be the literal symbol 'task', 'thread', or 'process'");
            valid = false;
        } else if (strcmp(value, "task") == 0) {
            *mode = START_MODE_TASK;
        } else if (strcmp(value, "thread") == 0) {
            *mode = START_MODE_THREAD;
        } else if (strcmp(value, "process") == 0) {
            *mode = START_MODE_PROCESS;
        } else {
            record_semantic_error(tp, call_node, ERR_INVALID_OPERATION,
                "invalid `start` mode '%s'; expected 'task', 'thread', or 'process'", value);
            valid = false;
        }
    }

    if (valid && *mode != START_MODE_TASK) {
        const char* value = *mode == START_MODE_THREAD ? "thread" : "process";
        record_semantic_error(tp, call_node, ERR_NOT_IMPLEMENTED,
            "`start` mode '%s' is not implemented yet; use 'task'", value);
        valid = false;
    }
    return valid;
}

static AstNode* build_start_call(Transpiler* tp, AstCallNode* source_call,
        TSNode call_node, int arg_count) {
    AstStartNode* start = (AstStartNode*)alloc_ast_node(
        tp, AST_NODE_START, call_node, sizeof(AstStartNode));
    start->type = set_type_any(tp, ANY_LEGACY_UNCLASSIFIED);
    start->owner_scope = tp->current_scope;
    start->mode = START_MODE_TASK;

    if (arg_count < 1 || arg_count > 3) {
        record_semantic_error(tp, call_node, ERR_ARGUMENT_COUNT_MISMATCH,
            "`start` expects 1 to 3 arguments, got %d", arg_count);
        start->type = &TYPE_ERROR;
        return (AstNode*)start;
    }

    AstNode* target = source_call->argument;
    AstNode* args = target ? target->next : NULL;
    AstNode* options = args ? args->next : NULL;
    for (AstNode* arg = target; arg; arg = arg->next) {
        if (arg->node_type == AST_NODE_NAMED_ARG) {
            record_semantic_error(tp, call_node, ERR_INVALID_OPERATION,
                "`start` uses positional arguments: start(pn, args, options)");
            start->type = &TYPE_ERROR;
            return (AstNode*)start;
        }
    }
    if (target) target->next = NULL;
    if (args) args->next = NULL;
    if (options) options->next = NULL;

    TypeFunc* fn_type = target && target->type && target->type->type_id == LMD_TYPE_FUNC
        ? (TypeFunc*)target->type : NULL;
    if (!fn_type || !fn_type->is_proc) {
        record_semantic_error(tp, call_node, ERR_INVALID_CALL,
            "`start` first argument must resolve to a procedure (pn)");
        start->type = &TYPE_ERROR;
    }

    AstNode* args_root = args ? ast_unwrap_primary(args) : NULL;
    if (args && (!args->type || args->type->type_id != LMD_TYPE_ARRAY)) {
        record_semantic_error(tp, call_node, ERR_ARGUMENT_TYPE_MISMATCH,
            "`start` second argument must be an argument array");
        start->type = &TYPE_ERROR;
    }
    if (!parse_start_options(tp, options, call_node, &start->mode)) {
        start->type = &TYPE_ERROR;
    }

    AstCallNode* target_call = (AstCallNode*)alloc_ast_node(
        tp, AST_NODE_CALL_EXPR, call_node, sizeof(AstCallNode));
    target_call->function = target;
    target_call->argument = args;
    target_call->type = fn_type ? function_call_result_type(tp, fn_type) : &TYPE_ERROR;
    target_call->can_raise = fn_type ? fn_type->can_raise : false;
    start->call = target_call;

    if (fn_type && args_root && args_root->node_type == AST_NODE_ARRAY) {
        AstArrayNode* literal_args = (AstArrayNode*)args_root;
        for (AstNode* item = literal_args->item; item; item = item->next) {
            if (item->node_type == AST_NODE_ASSIGN) {
                record_semantic_error(tp, call_node, ERR_INVALID_OPERATION,
                    "`start` argument arrays cannot contain declarations");
                start->type = &TYPE_ERROR;
                return (AstNode*)start;
            }
        }
        AstCallNode validation_call = *target_call;
        validation_call.argument = literal_args->item;
        if (!validate_lambda_call_arguments(tp, &validation_call, call_node,
                (int)((TypeArray*)literal_args->type)->length)) {
            start->type = &TYPE_ERROR;
        }
    }
    return (AstNode*)start;
}

static bool validate_lambda_argument_limit(Transpiler* tp, TSNode node,
        int count, const char* subject) {
    if (count <= LAMBDA_MAX_FUNCTION_ARGS) return true;
    record_semantic_error(tp, node, ERR_FUNCTION_ARGUMENT_LIMIT,
        "%s count %d exceeds Core Lambda limit %d; use a rest parameter or an array/map",
        subject, count, LAMBDA_MAX_FUNCTION_ARGS);
    return false;
}

static bool validate_lambda_call_arguments(Transpiler* tp, AstCallNode* call,
        TSNode diagnostic_node, int arg_count) {
    if (!call || !call->function || !call->function->type ||
            call->function->type->type_id != LMD_TYPE_FUNC) return true;

    TypeFunc* func_type = (TypeFunc*)call->function->type;
    TypeParam* expected_param = func_type->param;
    AstNode* arg = call->argument;
    int arg_index = 0;
    int line = ts_node_start_point(diagnostic_node).row + 1;
    String* var_arg_roots[64];
    int var_arg_root_count = 0;
    bool parameter_short_circuits_error = false;

    // A statically resolved function cannot manufacture missing values. The
    // validator is shared by direct calls and start(pn, literal_args), keeping
    // launch syntax from weakening the ordinary call contract.
    if (ast_called_function_signature_ready(call->function) &&
            (arg_count < func_type->required_param_count ||
             (!func_type->is_variadic && arg_count > func_type->param_count))) {
        record_type_error_code(tp, line, ERR_ARGUMENT_COUNT_MISMATCH,
            "function expects %d%s argument%s, got %d",
            func_type->required_param_count,
            func_type->is_variadic ? " or more" : "",
            func_type->required_param_count == 1 && !func_type->is_variadic ? "" : "s",
            arg_count);
        if (!should_continue_transpiling(tp)) {
            call->type = &TYPE_ERROR;
            return false;
        }
    }

    while (arg && expected_param) {
        if (expected_param->is_var_param) {
            AstIdentNode* root = compound_root_ident(arg);
            if (!root || !root->entry || !root->entry->is_mutable) {
                record_semantic_error(tp, diagnostic_node, ERR_IMMUTABLE_ASSIGNMENT,
                    "argument %d for `var` parameter must be a mutable `var` binding",
                    arg_index + 1);
                if (!should_continue_transpiling(tp)) {
                    call->type = &TYPE_ERROR;
                    return false;
                }
            } else {
                for (int i = 0; i < var_arg_root_count; i++) {
                    if (same_name_string(var_arg_roots[i], root->name)) {
                        record_semantic_error(tp, diagnostic_node, ERR_IMMUTABLE_ASSIGNMENT,
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

static bool resolve_imported_module_method(Transpiler* tp, TSNode object_node,
        StrView method_name, StrView* module_name, const char** resolved_module) {
    TSNode name_node = object_node;
    if (ts_node_symbol(object_node) == SYM_PRIMARY_EXPR) {
        TSNode inner = ts_node_child(object_node, 0);
        if (ts_node_is_null(inner) || ts_node_symbol(inner) != sym_identifier) return false;
        name_node = inner;
    } else if (ts_node_symbol(object_node) != sym_identifier) {
        return false;
    }

    *module_name = ts_node_source(tp, name_node);
    *resolved_module = resolve_imported_module(tp, module_name);
    if (!*resolved_module) return false;

    char qualified_buf[256];
    snprintf(qualified_buf, sizeof(qualified_buf), "%.*s.%.*s",
        (int)module_name->length, module_name->str,
        (int)method_name.length, method_name.str);
    StrView qualified_view = strview_from_cstr(qualified_buf);
    NameEntry* qualified = lookup_name(tp, qualified_view);
    if (qualified) {
        log_debug("qualified name '%s' found in scope, skipping builtin detection", qualified_buf);
        *resolved_module = NULL;
        return false;
    }
    log_debug("module call detected: %.*s.%.*s() -> %s",
        (int)module_name->length, module_name->str,
        (int)method_name.length, method_name.str, *resolved_module);
    return true;
}

AstNode* build_call_expr(Transpiler* tp, TSNode call_node, TSSymbol symbol) {
    log_debug("build call expr: %d", symbol);
    AstCallNode* ast_node = (AstCallNode*)alloc_ast_node(tp,
        AST_NODE_CALL_EXPR, call_node, sizeof(AstCallNode));

    // count no. of arguments
    int arg_count = 0;
    TSTreeCursor cursor = ts_tree_cursor_new(call_node);
    bool has_node = ts_tree_cursor_goto_first_child(&cursor);
    while (has_node) {
        TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
        if (field_id == FIELD_ARGUMENT) { arg_count++; }
        has_node = ts_tree_cursor_goto_next_sibling(&cursor);
    }
    ts_tree_cursor_delete(&cursor);
    log_debug("arg count: %d", arg_count);
    (void)validate_lambda_argument_limit(tp, call_node, arg_count, "call argument");

    // build function name
    TSNode function_node = ts_node_child_by_field_id(call_node, FIELD_FUNCTION);
    TSSymbol fn_symbol = ts_node_symbol(function_node);

    // Check if this is a method-style call: obj.method(args)
    // In this case, function_node is a primary_expr containing a member_expr
    bool is_method_call = false;
    TSNode member_node = {0};
    TSNode object_node = {0};
    TSNode method_name_node = {0};
    StrView method_name = {0};

    if (fn_symbol == SYM_PRIMARY_EXPR) {
        // Look for member_expr inside primary_expr
        TSNode inner = ts_node_child(function_node, 0);
        if (!ts_node_is_null(inner) && ts_node_symbol(inner) == SYM_MEMBER_EXPR) {
            member_node = inner;
            is_method_call = true;
        }
    } else if (fn_symbol == SYM_MEMBER_EXPR) {
        member_node = function_node;
        is_method_call = true;
    }

    if (is_method_call) {
        object_node = ts_node_child_by_field_id(member_node, FIELD_OBJECT);
        method_name_node = ts_node_child_by_field_id(member_node, FIELD_FIELD);
        method_name = ts_node_source(tp, method_name_node);
        log_debug("method_call detected: obj.%.*s() with %d args",
            (int)method_name.length, method_name.str, arg_count);
    }

    // For method calls, first build the object to get its type for validation
    AstNode* method_object = NULL;
    TypeId obj_type_id = LMD_TYPE_ANY;
    bool user_method_is_proc = false;

    // Check if this is a module call (e.g., io.copy() or hostobj_demo.answer()).
    // Modules are identified by name or alias and don't require building the object.
    bool is_imported_module_call = false;
    StrView module_name = {0};
    const char* resolved_module = NULL;  // real module name (e.g., "math" even when alias is "m")
    if (is_method_call && !ts_node_is_null(object_node)) {
        is_imported_module_call = resolve_imported_module_method(tp, object_node,
            method_name, &module_name, &resolved_module);
    }

    bool is_aliased_import_call = false;
    if (is_method_call && !is_imported_module_call && !ts_node_is_null(object_node)) {
        // Check if object.method is an aliased import call (e.g., helper.add())
        // by looking up the qualified name "object.method" in scope
        if (module_name.length > 0 && method_name.length > 0) {
            char qualified_buf[256];
            snprintf(qualified_buf, sizeof(qualified_buf), "%.*s.%.*s",
                (int)module_name.length, module_name.str,
                (int)method_name.length, method_name.str);
            StrView qualified_view = strview_from_cstr(qualified_buf);
            NameEntry* qualified_entry = lookup_name(tp, qualified_view);
            if (qualified_entry && qualified_entry->import) {
                // resolved as an aliased import function call
                log_debug("aliased import call resolved: %s", qualified_buf);
                is_aliased_import_call = true;
                AstIdentNode* ident_node = (AstIdentNode*)alloc_ast_node(
                    tp, AST_NODE_IDENT, function_node, sizeof(AstIdentNode));
                ident_node->name = qualified_entry->name;
                ident_node->entry = qualified_entry;
                ident_node->type = qualified_entry->node->type;
                // Wrap in a primary_expr for consistency with transpiler expectations
                AstPrimaryNode* primary_node = (AstPrimaryNode*)alloc_ast_node(
                    tp, AST_NODE_PRIMARY, function_node, sizeof(AstPrimaryNode));
                primary_node->expr = (AstNode*)ident_node;
                primary_node->type = ident_node->type;
                ast_node->function = (AstNode*)primary_node;
                if (ident_node->type && ident_node->type->type_id == LMD_TYPE_FUNC) {
                    TypeFunc* func_type = (TypeFunc*)ident_node->type;
                    if (func_type->is_proc && !tp->current_scope->is_proc) {
                        record_semantic_error(tp, call_node, ERR_PROC_IN_FN,
                            "procedure '%s' cannot be called in a function", qualified_buf);
                        ast_node->type = &TYPE_ERROR;
                        return (AstNode*)ast_node;
                    }
                    if (func_type->can_raise) {
                        ast_node->can_raise = true;
                    }
                    ast_node->type = function_call_result_type(tp, func_type);
                } else {
                    ast_node->type = set_type_any(tp, ANY_CALL_RESULT);
                }
            }
        }
        if (!is_aliased_import_call) {
            method_object = build_expr(tp, object_node);
            if (method_object && method_object->type) {
                obj_type_id = method_object->type->type_id;
            }
        }
    }

    // Try to resolve as sys func
    StrView func_name = ts_node_source(tp, function_node);
    SysFuncInfo* sys_func_info = NULL;

    // For module calls, construct the full function name (e.g., math_sqrt)
    // Use resolved_module (real name) instead of module_name (may be alias)
    if (is_aliased_import_call) {
        // Already resolved above - skip sys func and regular call resolution
    }
    else if (is_imported_module_call) {
        char full_name[128];
        snprintf(full_name, sizeof(full_name), "%s_%.*s",
            resolved_module,
            (int)method_name.length, method_name.str);
        StrView full_name_view = strview_from_cstr(full_name);
        sys_func_info = get_sys_func_info(&full_name_view, arg_count);
        if (sys_func_info) {
            log_debug("module call resolved to sys func: %s", sys_func_info->name);
        } else {
            // Report unknown module function
            record_semantic_error(tp, call_node, ERR_UNDEFINED_FUNCTION,
                "unknown function '%.*s.%.*s'",
                (int)module_name.length, module_name.str,
                (int)method_name.length, method_name.str);
            ast_node->type = &TYPE_ERROR;
            return (AstNode*)ast_node;
        }
    }
    else if (is_method_call) {
        // For map, element, and object types, check user-defined fields/methods first
        // before sys func lookup. This lets member fields take precedence over built-in
        // functions (e.g., a field named "sum" on a map should shadow the built-in sum()).
        bool has_user_member = false;
        if (method_object && method_object->type) {
            TypeId tid = obj_type_id;
            // Check shape entries (fields) for map, vmap, element, and object types
            if (is_map_family_type_id(tid) && !is_global_simple_type(method_object->type)) {
                TypeMap* map_type = (TypeMap*)method_object->type;
                FOR_EACH_MAP_FIELD(map_type, se) {
                    if (se->name && se->name->length == method_name.length &&
                        strncmp(se->name->str, method_name.str, method_name.length) == 0) {
                        has_user_member = true;
                        log_debug("method_call: member field '%.*s' takes precedence over sys func",
                            (int)method_name.length, method_name.str);
                        break;
                    }
                }
            }
            // Also check object method table
            if (!has_user_member && tid == LMD_TYPE_OBJECT &&
                    !is_global_simple_type(method_object->type)) {
                TypeObject* obj_type = (TypeObject*)method_object->type;
                for (TypeObject* owner = obj_type; owner && !has_user_member;
                        owner = owner->base) {
                    for (TypeMethod* m = owner->methods; m; m = m->next) {
                        if (m->name && m->name->length == method_name.length &&
                            strncmp(m->name->str, method_name.str, method_name.length) == 0) {
                            has_user_member = true;
                            user_method_is_proc = m->is_proc;
                            log_debug("method_call: user-defined method '%.*s' takes precedence over sys func",
                                (int)method_name.length, method_name.str);
                            break;
                        }
                    }
                }
            }
        }

        if (!has_user_member) {
            // Try method-style lookup: obj.method(args) -> method(obj, args)
            sys_func_info = get_sys_func_for_method(&method_name, arg_count, obj_type_id);
            if (sys_func_info) {
                log_debug("method_call resolved to sys func: %s", sys_func_info->name);
            }
        }
    }

    if (!sys_func_info && !is_method_call) {
        // Traditional function call lookup
        // If in pipe context without ~ reference, lookup with extra arg
        int lookup_arg_count = arg_count + tp->pipe_inject_args;

        // Check if the function name resolves in user scope first;
        // user-defined functions shadow system functions with the same name.
        // AST_NODE_PROC counts too: a `pn` is as much a user definition as a
        // `fn`. Omitting it meant a user `pn` sharing a builtin's name AND arity
        // was silently discarded in favour of the builtin — `pn emit(a, b)` (the
        // arity-2 `emit` sysproc) compiled and ran but produced nothing, with no
        // diagnostic. A one-arg `pn emit(v)` appeared to work only because the
        // arity-keyed lookup missed the builtin.
        NameEntry* user_name = lookup_name(tp, func_name);
        bool user_shadows = (user_name != NULL && user_name->node != NULL &&
            (user_name->node->node_type == AST_NODE_FUNC ||
             user_name->node->node_type == AST_NODE_PROC));

        if (!user_shadows) {
            sys_func_info = get_sys_func_info(&func_name, lookup_arg_count);
            if (sys_func_info && tp->pipe_inject_args > 0) {
                log_debug("pipe inject: lookup %.*s with %d args (was %d)",
                    (int)func_name.length, func_name.str, lookup_arg_count, arg_count);
                ast_node->pipe_inject = true;
            }
        }
        // Global import fallback: if `import math;`, `import io;`, or a
        // descriptor-backed native module import was used,
        // try prefixing the function name with the module name (e.g., sqrt -> math_sqrt)
        if (!sys_func_info) {
            sys_func_info = lookup_global_imported_sys_func(tp, &func_name, lookup_arg_count);
            if (sys_func_info) {
                log_debug("global import resolved: %.*s -> %s",
                    (int)func_name.length, func_name.str, sys_func_info->name);
                if (tp->pipe_inject_args > 0) {
                    ast_node->pipe_inject = true;
                }
            }
        }
        if (!sys_func_info) {
            // The complex-number surface exposes these principal functions
            // directly while reusing the established math-module entries.
            sys_func_info = lookup_complex_math_builtin(&func_name, lookup_arg_count);
        }
    }

    if (sys_func_info) {
        log_debug("build sys call");
        if (sys_func_info->is_proc) {
            if (!tp->current_scope->is_proc) {
                if (sys_func_info->fn == SYSPROC_START) {
                    record_semantic_error(tp, call_node, ERR_PROC_IN_FN,
                        "`start` is only allowed inside a procedure (pn)");
                    ast_node->type = &TYPE_ERROR;
                    return (AstNode*)ast_node;
                }
                const char* fn_name = is_method_call ? method_name.str : func_name.str;
                int fn_name_len = is_method_call ? (int)method_name.length : (int)func_name.length;
                record_semantic_error(tp, call_node, ERR_PROC_IN_FN,
                    "procedure '%.*s' cannot be called in a function",
                    fn_name_len, fn_name);
                ast_node->type = &TYPE_ERROR;
                return (AstNode*)ast_node;
            }
        }
        AstSysFuncNode* fn_node = (AstSysFuncNode*)alloc_ast_node(tp,
            AST_NODE_SYS_FUNC, function_node, sizeof(AstSysFuncNode));
        fn_node->fn_info = sys_func_info;
        fn_node->type = sys_func_info->return_type;
        ast_node->function = (AstNode*)fn_node;
        // The registry distinguishes T^ propagation from ordinary ItemError
        // results. Both retain their full semantic result type for `or` and
        // checked boundaries; only T^ sets can_raise.
        if (sys_func_info->can_raise) {
            ast_node->can_raise = true;
        }
        ast_node->type = sys_func_call_result_type(tp, sys_func_info,
            sys_func_info->may_return_error, NULL);

        // For method calls, prepend the object as the first argument
        if (is_method_call && method_object) {
            ast_node->argument = method_object;
            log_debug("method_call prepended object as first arg, type=%d", obj_type_id);
        }
    }
    else if (!is_aliased_import_call) {
        if (is_method_call) {
            // Not a sys func method call - could be a user-defined method on the object
            // For now, fall back to building as a regular member expression call
            log_debug("method_call not resolved as sys func, building as regular call");
        }
        ast_node->function = build_expr(tp, function_node);
        if (ast_node->function->type->type_id == LMD_TYPE_FUNC) {
            TypeFunc* func_type = (TypeFunc*)ast_node->function->type;
            if (func_type->is_proc && !tp->current_scope->is_proc) {
                record_semantic_error(tp, call_node, ERR_PROC_IN_FN,
                    "procedure '%.*s' cannot be called in a function",
                    (int)func_name.length, func_name.str);
                ast_node->type = &TYPE_ERROR;
                return (AstNode*)ast_node;
            }
            // Retain the complete semantic result even though MIR still uses
            // one boxed Item lane when an error constituent is possible.
            if (func_type->can_raise) {
                ast_node->can_raise = true;
            }
            ast_node->type = function_call_result_type(tp, func_type);
            if (!ast_node->type) { // e.g. recursive fn
                ast_node->type = set_type_any(tp, ANY_CALL_RESULT);
            }
            if (ast_node->type && ast_node->type->is_const) {
                ast_node->type = clone_type_without_const(tp, ast_node->type);
            }
        }
        else {
            ast_node->type = set_type_any(tp, ANY_CALL_RESULT);
        }
    }

    if (is_method_call && user_method_is_proc) {
        if (!tp->current_scope || !tp->current_scope->is_proc) {
            record_semantic_error(tp, call_node, ERR_PROC_IN_FN,
                "procedure method '%.*s' cannot be called in a function",
                (int)method_name.length, method_name.str);
            ast_node->type = &TYPE_ERROR;
            return (AstNode*)ast_node;
        }
        AstIdentNode* receiver_root = compound_root_ident(method_object);
        if (!receiver_root || !receiver_root->entry || !receiver_root->entry->is_mutable) {
            // A pn method writes its implicit self parameter back on return, so a
            // let-rooted receiver would otherwise appear to succeed and lose the write.
            record_semantic_error(tp, call_node, ERR_IMMUTABLE_ASSIGNMENT,
                "mutating method '%.*s' needs a `var` binding receiver",
                (int)method_name.length, method_name.str);
            ast_node->type = &TYPE_ERROR;
            return (AstNode*)ast_node;
        }
        ast_node->is_proc_method = true;
    }

    // build arguments
    cursor = ts_tree_cursor_new(call_node);
    has_node = ts_tree_cursor_goto_first_child(&cursor);
    // For method calls with sys func, arguments are appended after the object
    AstNode* prev_argument = is_method_call && sys_func_info ? method_object : NULL;
    while (has_node) {
        // check if the current node's field ID matches the target field ID
        TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
        if (field_id == FIELD_ARGUMENT) {
            TSNode child = ts_tree_cursor_current_node(&cursor);
            AstNode* argument = build_expr(tp, child);
            log_debug("got argument: %p, &t: %p, node_type %d, type: %d", argument, argument->type, argument->node_type, argument->type->type_id);
            if (prev_argument == NULL) {
                ast_node->argument = argument;
            }
            else {
                prev_argument->next = argument;
            }
            prev_argument = argument;
        }
        has_node = ts_tree_cursor_goto_next_sibling(&cursor);
    }
    ts_tree_cursor_delete(&cursor);

    if (sys_func_info && sys_func_info->fn == SYSPROC_START) {
        // The ordinary call surface must still become a distinct semantic node:
        // scope-exit joins, return escape, and mutable-capture rejection all
        // depend on recognizing task creation after grammar has forgotten it.
        return build_start_call(tp, ast_node, call_node, arg_count);
    }

    bool has_named_arguments = false;
    for (AstNode* call_arg = ast_node->argument; call_arg; call_arg = call_arg->next) {
        if (call_arg->node_type == AST_NODE_NAMED_ARG) {
            has_named_arguments = true;
            break;
        }
    }
    if (has_named_arguments && !ast_called_function_signature_ready(ast_node->function)) {
        // Runtime dispatch has only boxed positional operands.  Preserve names
        // for statically resolved calls; fail before lowering an opaque call
        // that would otherwise silently discard them.
        record_semantic_error(tp, call_node, ERR_UNSUPPORTED_DYNAMIC_ABI,
            "named arguments require a statically resolved Lambda callee");
        if (!should_continue_transpiling(tp)) {
            ast_node->type = &TYPE_ERROR;
            return (AstNode*)ast_node;
        }
    }

    if (sys_func_info) {
        // Arguments are built after the registry call is resolved. Refine only
        // now, when a concrete source type can prove a converter's ordinary
        // ItemError branch unreachable; the initial registry effect remains
        // the conservative answer for opaque and text inputs.
        ast_node->type = sys_func_call_result_type(tp, sys_func_info,
            sys_func_call_may_return_error(tp, sys_func_info, ast_node->argument),
            ast_node->argument);
    }

    if (!sys_func_info && ast_node->function && arg_count == 1) {
        Type* target_type = ast_called_type_target(ast_node->function);
        if (target_type && (target_type->type_id == LMD_TYPE_NUM_SIZED ||
                target_type->type_id == LMD_TYPE_UINT64 || target_type->type_id == LMD_TYPE_FLOAT64)) {
            ast_node->type = target_type;
            if (target_type->type_id == LMD_TYPE_NUM_SIZED) {
                NumSizedType num_type = type_num_sized_kind(target_type);
                if (num_type != NUM_FLOAT16 && num_type != NUM_FLOAT32) {
                    int64_t const_value = 0;
                    if (ast_constant_integer_value(tp, ast_node->argument, &const_value) &&
                            !constant_fits_sized_integer(num_type, const_value)) {
                        // Constant conversions follow Go: invalid constants are rejected before truncating.
                        record_semantic_error(tp, call_node, ERR_INVALID_NUMBER,
                            "constant conversion to %s overflows", get_num_sized_type_name(num_type));
                        ast_node->type = &TYPE_ERROR;
                        return (AstNode*)ast_node;
                    }
                }
            }
        }
    }

    if (sys_func_info) {
        AstNode* first_arg = ast_node->argument;
        AstNode* second_arg = first_arg ? first_arg->next : NULL;
        Type* bitwise_type = infer_bitwise_call_type(sys_func_info->fn, first_arg, second_arg);
        if (bitwise_type) {
            ast_node->type = bitwise_type;
            if (ast_node->function && ast_node->function->node_type == AST_NODE_SYS_FUNC) {
                ast_node->function->type = bitwise_type;
            }
        }
    }

    if (!validate_lambda_call_arguments(tp, ast_node, call_node, arg_count)) {
        return (AstNode*)ast_node;
    }

    log_debug("end building call expr type: %p, %d, is_const:%d, can_raise:%d, propagate:%d",
        ast_node->type, ast_node->type->type_id, ast_node->type->is_const, ast_node->can_raise, ast_node->propagate);
    return (AstNode*)ast_node;
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

AstNode* build_identifier(Transpiler* tp, TSNode id_node) {
    log_debug("building identifier");
    AstIdentNode* ast_node = (AstIdentNode*)alloc_ast_node(tp, AST_NODE_IDENT, id_node, sizeof(AstIdentNode));

    // get the identifier name from source and create pooled string
    StrView var_name = ts_node_source(tp, id_node);
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
            AstFieldNode* field_node = (AstFieldNode*)alloc_ast_node(tp,
                AST_NODE_MEMBER_EXPR, id_node, sizeof(AstFieldNode));
            // create ~ (current item) as the object
            AstNode* current_item = alloc_ast_node(tp, AST_NODE_CURRENT_ITEM, id_node, sizeof(AstNode));
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
                AstPrimaryNode* pn = (AstPrimaryNode*)alloc_ast_node(tp, AST_NODE_PRIMARY, id_node, sizeof(AstPrimaryNode));
                pn->type = (Type*)ft;
                log_debug("global import math constant resolved: %.*s", (int)var_name.length, var_name.str);
                return (AstNode*)pn;
            }
        }
        SysFuncInfo* sys_value = get_unambiguous_sys_func_value(&var_name);
        if (sys_value) {
            AstSysFuncNode* sys_node = (AstSysFuncNode*)alloc_ast_node(tp,
                AST_NODE_SYS_FUNC, id_node, sizeof(AstSysFuncNode));
            TypeFunc* fn_type = (TypeFunc*)alloc_type(tp->pool, LMD_TYPE_FUNC, sizeof(TypeFunc));
            fn_type->param_count = sys_value->arg_count;
            fn_type->required_param_count = sys_value->arg_count;
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

Type* build_lit_string(Transpiler* tp, TSNode node, TSSymbol symbol) {
    // Phase 3: empty strings are values; empty symbol/binary values remain absent.
    // With single-token strings/symbols, we parse the raw token text directly
    // Binary is now a single token: b'content'
    String* str;
    log_debug("build lit string with symbol: %d", symbol);

    // Handle binary separately — extract content between b' and '
    if (symbol == SYM_BINARY) {
        TypeBinaryConst* str_type = (TypeBinaryConst*)alloc_type(tp->pool, LMD_TYPE_BINARY, sizeof(TypeBinaryConst));
        str_type->is_const = 1;  str_type->is_literal = 1;

        int start = ts_node_start_byte(node), end = ts_node_end_byte(node);
        const char* raw = tp->source + start;
        int raw_len = end - start;

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
            record_semantic_error(tp, node, ERR_SYNTAX_ERROR,
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

    int start = ts_node_start_byte(node), end = ts_node_end_byte(node);
    int raw_len = end - start;
    const char* raw = tp->source + start;

    // Determine quote character and content boundaries
    char quote_char = (symbol == SYM_STRING) ? '"' : '\'';  // symbols use single quotes

    // Skip opening quote
    if (raw_len < 2 || raw[0] != quote_char || raw[raw_len - 1] != quote_char) {
        log_error("Invalid string literal format: %.*s", raw_len, raw);
        return &LIT_NULL;
    }

    const char* content_start = raw + 1;
    int content_len = raw_len - 2;  // exclude both quotes

    // Empty symbol literals are rejected by grammar; keep a null fallback for generated/stale parsers.
    if (content_len == 0) {
        if (symbol == SYM_SYMBOL) {
            log_debug("build_lit_string: empty symbol literal, returning null type");
            return &LIT_NULL;
        }
    }

    TypeString* str_type = (TypeString*)alloc_type(tp->pool,
        symbol == SYM_STRING ? LMD_TYPE_STRING : LMD_TYPE_SYMBOL, sizeof(TypeString));
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
        if (symbol == SYM_SYMBOL) {
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
                            int hex_len = hex_end - hex_start;
                            char* hex_str = (char*)mem_alloc(hex_len + 1, MEM_CAT_AST);
                            memcpy(hex_str, hex_start, hex_len);
                            hex_str[hex_len] = '\0';
                            char* endptr;
                            uint32_t code_point = strtoul(hex_str, &endptr, 16);
                            if (endptr == hex_str + hex_len && code_point <= 0x10FFFF) {
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
        if (str->len == 0 && symbol == SYM_SYMBOL) {
            log_debug("build_lit_string: empty symbol after escape processing, returning null type");
            return &LIT_NULL;
        }

        // For symbols, re-allocate as Symbol struct (different layout from String)
        if (symbol == SYM_SYMBOL) {
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

Type* build_lit_datetime(Transpiler* tp, TSNode node, TSSymbol symbol) {
    int start = ts_node_start_byte(node), end = ts_node_end_byte(node);

    TypeDateTime* dt_type = (TypeDateTime*)alloc_type(tp->pool, LMD_TYPE_DTIME, sizeof(TypeDateTime));
    dt_type->is_const = 1;  dt_type->is_literal = 1;

    // Token is t'...' — skip the t' prefix and trailing '
    const char* raw = tp->source + start;
    int raw_len = end - start;
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

Type* build_lit_int64(Transpiler* tp, TSNode node) {
    TypeInt64* item_type = (TypeInt64*)alloc_type(tp->pool, LMD_TYPE_INT64, sizeof(TypeInt64));
    StrView source = ts_node_source(tp, node);
    char* endptr;
    int64_t value = strtoll(source.str, &endptr, 0);
    item_type->int64_val = value;
    arraylist_append(tp->const_list, &item_type->int64_val);
    item_type->const_index = tp->const_list->length - 1;
    item_type->is_const = 1;  item_type->is_literal = 1;
    return (Type*)item_type;
}

static int decimal_literal_significant_digits(const char* str);
static bool n_literal_is_integer(const char* str);

Type* build_lit_float(Transpiler* tp, TSNode node) {
    TypeFloat* item_type = (TypeFloat*)alloc_type(tp->pool, LMD_TYPE_FLOAT, sizeof(TypeFloat));
    // C supports inf and nan
    log_debug("build lit float");
    const char* num_str = tp->source + ts_node_start_byte(node);
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
    return (Type*)item_type;
}

static Type* build_lit_decimal_poison(Transpiler* tp, TSNode node) {
    StrView source = ts_node_source(tp, node);
    const char* spelling = strview_equal(&source, "decimal.inf") ? "Infinity" : "NaN";
    TypeDecimal* item_type = (TypeDecimal*)alloc_type(tp->pool, LMD_TYPE_DECIMAL,
        sizeof(TypeDecimal));
    Decimal* decimal = (Decimal*)pool_alloc(tp->pool, sizeof(Decimal));
    if (!decimal) return &TYPE_ERROR;
    decimal->unlimited = 0;
    decimal->dec_val = decimal_parse_str(spelling, decimal_fixed_context());
    if (!decimal->dec_val) return &TYPE_ERROR;
    item_type->decimal = decimal;
    arraylist_append(tp->const_list, decimal);
    item_type->const_index = tp->const_list->length - 1;
    item_type->is_const = 1;
    item_type->is_literal = 1;
    return (Type*)item_type;
}

static Type* build_lit_named_value(Transpiler* tp, TSNode node) {
    StrView text = ts_node_source(tp, node);
    if (strview_equal(&text, "true") || strview_equal(&text, "false")) return &LIT_BOOL;
    if (strview_equal(&text, "decimal.inf") || strview_equal(&text, "decimal.nan")) {
        return build_lit_decimal_poison(tp, node);
    }
    return build_lit_float(tp, node);
}

Type* build_lit_imaginary(Transpiler* tp, TSNode node) {
    StrView source = ts_node_source(tp, node);
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

Type* build_lit_decimal(Transpiler* tp, TSNode node) {
    TypeDecimal* item_type = (TypeDecimal*)alloc_type(tp->pool, LMD_TYPE_DECIMAL, sizeof(TypeDecimal));
    StrView num_sv = ts_node_source(tp, node);
    char* num_str = strview_to_cstr(&num_sv);
    char suffix_char = num_sv.str[num_sv.length - 1];
    if (suffix_char == 'N') {
        record_semantic_error(tp, node, ERR_INVALID_NUMBER,
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
        record_semantic_error(tp, node, ERR_INVALID_NUMBER,
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
    decimal->dec_val = decimal_parse_str(num_str,
        decimal->unlimited ? decimal_unlimited_context() : decimal_fixed_context());
    if (!decimal->dec_val) {
        log_error("Error: Failed to parse decimal: %s", num_str);
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
Type* build_lit_sized_integer(Transpiler* tp, TSNode node) {
    StrView source = ts_node_source(tp, node);
    char* num_str = (char*)mem_alloc(source.length + 1, MEM_CAT_AST);
    memcpy(num_str, source.str, source.length);
    num_str[source.length] = '\0';

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
        record_semantic_error(tp, node, ERR_INVALID_NUMBER,
            "invalid sized integer literal '%s'", num_str);
        mem_free(num_str);
        return &TYPE_ERROR;
    }
    bool decimal_literal = sized_literal_is_decimal(num_str);
    uint64_t limit = sized_literal_limit(num_type, decimal_literal);
    if (raw_value > limit) {
        // sized constants are checked before packing so invalid source cannot silently truncate.
        record_semantic_error(tp, node, ERR_INVALID_NUMBER,
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
Type* build_lit_sized_float(Transpiler* tp, TSNode node) {
    StrView source = ts_node_source(tp, node);
    char* num_str = (char*)mem_alloc(source.length + 1, MEM_CAT_AST);
    memcpy(num_str, source.str, source.length);
    num_str[source.length] = '\0';

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

// unknown type-name diagnostic with a conceptual-alias suggestion. Names like
// `int64` appear in docs/prose as concept names but are NOT Lambda annotation
// syntax (the defined names are the sized forms, e.g. `i64`); without this,
// the annotation silently became TYPE_ERROR and surfaced later as a confusing
// E201 "cannot initialize ... of type error".
// returns the defined syntax for a conceptual alias, or NULL when the name is
// not a known concept spelling.
static const char* base_type_alias_suggestion(StrView type_name) {
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

void record_unknown_base_type(Transpiler* tp, TSNode type_node, StrView type_name) {
    const char* suggestion = base_type_alias_suggestion(type_name);
    if (suggestion) {
        record_semantic_error(tp, type_node, ERR_UNDEFINED_TYPE,
            "unknown type '%.*s'; did you mean '%s'?",
            (int)type_name.length, type_name.str, suggestion);
        return;
    }
    record_semantic_error(tp, type_node, ERR_UNDEFINED_TYPE,
        "unknown type '%.*s'", (int)type_name.length, type_name.str);
}

// helper: returns Type* for base_type node (used in primary_expr context)
Type* build_base_type_inline(Transpiler* tp, TSNode type_node) {
    StrView type_name = ts_node_source(tp, type_node);
    if (strview_equal(&type_name, "null")) {
        return (Type*)&LIT_TYPE_NULL;
    }
    else if (strview_equal(&type_name, "any")) {
        return set_lit_type_any(tp, ANY_EXPLICIT);
    }
    else if (strview_equal(&type_name, "bool")) {
        return (Type*)&LIT_TYPE_BOOL;
    }
    else if (strview_equal(&type_name, "int") || strview_equal(&type_name, "int64")) {
        return (Type*)&LIT_TYPE_INT;
    }
    else if (strview_equal(&type_name, "float")) {
        return (Type*)&LIT_TYPE_FLOAT;
    }
    else if (strview_equal(&type_name, "complex")) {
        return (Type*)&LIT_TYPE_COMPLEX;
    }
    else if (strview_equal(&type_name, "f64")) {
        // f64 is accepted on input but canonicalizes to float.
        return (Type*)&LIT_TYPE_FLOAT;
    }
    else if (strview_equal(&type_name, "decimal")) {
        return (Type*)&LIT_TYPE_DECIMAL;
    }
    else if (strview_equal(&type_name, "integer")) {
        return (Type*)&LIT_TYPE_INTEGER;
    }
    else if (strview_equal(&type_name, "number")) {
        return (Type*)&LIT_TYPE_NUMBER;
    }
    else if (strview_equal(&type_name, "string")) {
        return (Type*)&LIT_TYPE_STRING;
    }
    else if (strview_equal(&type_name, "symbol")) {
        return (Type*)&LIT_TYPE_SYMBOL;
    }
    else if (strview_equal(&type_name, "datetime")) {
        return (Type*)&LIT_TYPE_DTIME;
    }
    else if (strview_equal(&type_name, "time")) {
        return (Type*)&LIT_TYPE_TIME;
    }
    else if (strview_equal(&type_name, "date")) {
        return (Type*)&LIT_TYPE_DATE;
    }
    else if (strview_equal(&type_name, "binary")) {
        return (Type*)&LIT_TYPE_BINARY;
    }
    else if (strview_equal(&type_name, "list")) {
        return (Type*)&LIT_TYPE_LIST;
    }
    else if (strview_equal(&type_name, "range")) {
        return (Type*)&LIT_TYPE_RANGE;
    }
    else if (strview_equal(&type_name, "array")) {
        return (Type*)&LIT_TYPE_ARRAY;
    }
    else if (strview_equal(&type_name, "map")) {
        return (Type*)&LIT_TYPE_MAP;
    }
    else if (strview_equal(&type_name, "element") || strview_equal(&type_name, "entity")) {
        return (Type*)&LIT_TYPE_ELMT;
    }
    else if (strview_equal(&type_name, "object")) {
        return (Type*)&LIT_TYPE_OBJECT;
    }
    else if (strview_equal(&type_name, "function")) {
        return (Type*)&LIT_TYPE_FUNC;
    }
    else if (strview_equal(&type_name, "type")) {
        return (Type*)&LIT_TYPE_TYPE;
    }
    else if (strview_equal(&type_name, "error")) {
        return (Type*)&LIT_TYPE_ERROR;
    }
    // sized numeric type names
    else if (strview_equal(&type_name, "i8")) {
        return (Type*)&LIT_TYPE_I8;
    }
    else if (strview_equal(&type_name, "i16")) {
        return (Type*)&LIT_TYPE_I16;
    }
    else if (strview_equal(&type_name, "i32")) {
        return (Type*)&LIT_TYPE_I32;
    }
    else if (strview_equal(&type_name, "i64")) {
        return (Type*)&LIT_TYPE_INT64;
    }
    else if (strview_equal(&type_name, "u8")) {
        return (Type*)&LIT_TYPE_U8;
    }
    else if (strview_equal(&type_name, "u16")) {
        return (Type*)&LIT_TYPE_U16;
    }
    else if (strview_equal(&type_name, "u32")) {
        return (Type*)&LIT_TYPE_U32;
    }
    else if (strview_equal(&type_name, "u64")) {
        return (Type*)&LIT_TYPE_U64;
    }
    else if (strview_equal(&type_name, "f16")) {
        return (Type*)&LIT_TYPE_F16;
    }
    else if (strview_equal(&type_name, "f32")) {
        return (Type*)&LIT_TYPE_F32;
    }
    else if (strview_equal(&type_name, "f64")) {
        // f64 is accepted on input but canonicalizes to float.
        return (Type*)&LIT_TYPE_FLOAT;
    }
    else {
        // report at the annotation site instead of leaking TYPE_ERROR into a
        // later E201 boundary message (root cause: name not defined syntax).
        record_unknown_base_type(tp, type_node, type_name);
        return &TYPE_ERROR;
    }
}

static AstNode* build_navigation_expr(Transpiler* tp, TSNode node) {
    TSNode object_node = ts_node_child_by_field_id(node, FIELD_OBJECT);
    TSNode field_node = ts_node_child_by_field_id(node, FIELD_FIELD);
    AstNavigationNode* nav = (AstNavigationNode*)alloc_ast_node(tp,
        AST_NODE_NAVIGATION_EXPR, node, sizeof(AstNavigationNode));
    nav->object = build_expr(tp, object_node);
    nav->root = ts_node_symbol(field_node) == SYM_PATH_ROOT;
    nav->type = nav->object ? nav->object->type : &TYPE_ANY;
    return (AstNode*)nav;
}

AstNode* build_primary_expr(Transpiler* tp, TSNode pri_node) {
    log_debug("*** DEBUG: build_primary_expr called ***");
    AstPrimaryNode* ast_node = (AstPrimaryNode*)alloc_ast_node(tp, AST_NODE_PRIMARY, pri_node, sizeof(AstPrimaryNode));
    TSNode child = ts_node_named_child(pri_node, 0);
    if (ts_node_is_null(child)) { return (AstNode*)ast_node; }

    if (ts_node_symbol(child) == sym_let_expr &&
            !ts_node_is_null(ts_node_next_named_sibling(child))) {
        // `_parenthesized_expr` is inline, so its leading lets now live directly
        // under primary_expr; preserve their shared scope instead of treating only
        // the first binding as the expression result.
        return build_let_block(tp, pri_node);
    }

    // infer data type
    TSSymbol symbol = ts_node_symbol(child);

    // unwrap expr wrapper node (no longer inlined in grammar)
    if (symbol == SYM_EXPR) {
        child = ts_node_named_child(child, 0);
        if (ts_node_is_null(child)) { return (AstNode*)ast_node; }
        symbol = ts_node_symbol(child);
    }
    log_debug("*** DEBUG: symbol=%d ***", symbol);
    if (symbol == SYM_BASE_TYPE) {
        // base_type in primary_expr context - check if it's "null" (value) vs other built-in types (type reference)
        StrView type_name = ts_node_source(tp, child);
        if (strview_equal(&type_name, "null")) {
            // "null" as value literal
            ast_node->type = &LIT_NULL;
        }
        else {
            // other built-in types are type expressions - delegate to build_expr which calls build_base_type
            ast_node->expr = build_expr(tp, child);
            ast_node->type = ast_node->expr->type;
        }
    }
    else if (symbol == SYM_NAMED_VALUE) {
        ast_node->type = build_lit_named_value(tp, child);
    }
    else if (symbol == SYM_INT) {
        // Parse the integer value to determine if it fits in 32-bit or needs 64-bit
        StrView source = ts_node_source(tp, child);
        // Create a null-terminated string for strtoll
        char* num_str = (char*)mem_alloc(source.length + 1, MEM_CAT_AST);
        memcpy(num_str, source.str, source.length);
        num_str[source.length] = '\0';

        int64_t value = 0;
        bool in_band = lambda_parse_int_literal(num_str, &value);
        mem_free(num_str);

        log_debug("build_primary_expr SYM_INT: parsed value %lld", value);
        if (!in_band) {
            record_semantic_error(tp, child, ERR_INVALID_NUMBER,
                "integer literal is outside compact int range; use an explicit suffix or decimal literal");
            ast_node->type = &TYPE_ERROR;
        }
        else {
            ast_node->type = &LIT_INT;
        }
    }
    else if (symbol == SYM_DECIMAL) {
        ast_node->type = build_lit_decimal(tp, child);
    }
    else if (symbol == SYM_FLOAT) {
        ast_node->type = build_lit_float(tp, child);
    }
    else if (symbol == SYM_IMAGINARY) {
        ast_node->type = build_lit_imaginary(tp, child);
    }
    else if (symbol == SYM_SIZED_INT) {
        ast_node->type = build_lit_sized_integer(tp, child);
    }
    else if (symbol == SYM_SIZED_FLOAT) {
        ast_node->type = build_lit_sized_float(tp, child);
    }
    else if (symbol == SYM_STRING || symbol == SYM_SYMBOL || symbol == SYM_BINARY) {
        ast_node->type = build_lit_string(tp, child, symbol);
    }
    else if (symbol == SYM_DATETIME) {
        ast_node->type = build_lit_datetime(tp, child, symbol);
    }
    else if (symbol == SYM_IDENT) {
        ast_node->expr = build_identifier(tp, child);
        if (ast_node->expr->type->is_const) {
            ast_node->type = clone_type_without_const(tp, ast_node->expr->type);
        }
        else {
            ast_node->type = ast_node->expr->type;
        }
    }
    else if (symbol == SYM_LAST_INDEX) {
        if (tp->subscript_depth <= 0) {
            record_semantic_error(tp, child, ERR_SYNTAX_ERROR,
                "`last` is only valid inside subscripts and `limit last` clauses");
        }
        AstNode* last_node = alloc_ast_node(tp, AST_NODE_LAST_INDEX, child, sizeof(AstNode));
        last_node->type = &TYPE_INT;
        ast_node->expr = last_node;
        ast_node->type = &TYPE_INT;
    }
    else if (symbol == SYM_ARRAY) {
        ast_node->expr = build_array(tp, child);
        ast_node->type = ast_node->expr->type;
    }
    else if (symbol == SYM_MAP) {
        // check for {TypeName} pattern: map with single identifier resolving to object type
        uint32_t named_count = ts_node_named_child_count(child);
        if (named_count == 1) {
            TSNode only = ts_node_named_child(child, 0);
            TSSymbol os = ts_node_symbol(only);
            if (os == SYM_PRIMARY_EXPR) {
                only = ts_node_named_child(only, 0);
                if (!ts_node_is_null(only)) os = ts_node_symbol(only);
            }
            if (os == SYM_IDENT) {
                StrView name = ts_node_source(tp, only);
                NameEntry* entry = lookup_name(tp, name);
                if (entry && entry->node && entry->node->type) {
                    Type* resolved = entry->node->type;
                    if (resolved->type_id == LMD_TYPE_TYPE) {
                        Type* inner = ((TypeType*)resolved)->type;
                        if (inner && inner->type_id == LMD_TYPE_OBJECT) {
                            log_debug("build_primary_expr: detected {%.*s} as empty object literal", (int)name.length, name.str);
                            TypeObject* obj_type = (TypeObject*)inner;
                            AstObjectLiteralNode* obj_node = (AstObjectLiteralNode*)alloc_ast_node(tp,
                                AST_NODE_OBJECT_LITERAL, child, sizeof(AstObjectLiteralNode));
                            obj_node->type_name = name_pool_create_strview(tp->name_pool, name);
                            obj_node->type = (Type*)obj_type;
                            obj_node->item = NULL;
                            ast_node->expr = (AstNode*)obj_node;
                            ast_node->type = (Type*)obj_type;
                            return (AstNode*)ast_node;
                        }
                    }
                }
            }
        }
        ast_node->expr = build_map(tp, child);
        ast_node->type = ast_node->expr->type;
    }
    else if (symbol == SYM_ELEMENT) {
        ast_node->expr = build_elmt(tp, child);
        ast_node->type = ast_node->expr->type;
    }
    else if (symbol == SYM_PATH_EXPR) {
        // The production grammar keeps only the ambiguous logical-root '/'
        // structural; the direct parser owns the complete static path span.
        StrView source = ts_node_source(tp, child);
        ast_node->expr = parse_path_expr_text(tp, source.str,
            source.str + source.length, child);
        if (!ast_node->expr) {
            ast_node->expr = alloc_ast_node(tp, AST_NODE_PATH_EXPR, child,
                sizeof(AstNode));
            ast_node->expr->type = &TYPE_PATH;
        }
        ast_node->type = ast_node->expr->type;
    }
    else if (symbol == SYM_MEMBER_EXPR) {
        ast_node->expr = build_member_expr(tp, child);
        ast_node->type = ast_node->expr->type;
    }
    else if (symbol == SYM_CURRENT_PARENT_EXPR) {
        AstNavigationNode* nav = (AstNavigationNode*)alloc_ast_node(tp,
            AST_NODE_NAVIGATION_EXPR, child, sizeof(AstNavigationNode));
        AstNode* current = alloc_ast_node(tp, AST_NODE_CURRENT_ITEM, child, sizeof(AstNode));
        current->type = alloc_type(tp->pool, LMD_TYPE_ANY, sizeof(Type));
        nav->object = current;
        nav->root = false;
        nav->type = nav->object->type;
        ast_node->expr = (AstNode*)nav;
        ast_node->type = nav->type;
    }
    else if (symbol == SYM_INDEX_EXPR) {
        // Check if this is a path index expression (path[expr])
        // Path subscripts add dynamic segments, unlike regular index expressions
        TSNode object_node = ts_node_child_by_field_id(child, FIELD_OBJECT);
        TSNode field_node = ts_node_child_by_field_id(child, FIELD_FIELD);

        // A static path is now opaque to Tree-sitter, so reconstruct its base
        // directly from the object's source before using the legacy CST walk.
        // This preserves every static segment before the dynamic bracket.
        StrView object_source = ts_node_source(tp, object_node);
        AstNode* direct_path = try_parse_path_expr_text(tp, object_source.str,
            object_source.str + object_source.length, object_node);

        // Check if the object is a path expression in the full grammar.
        ArrayList* segments = arraylist_new(8);
        int scheme = collect_path_segments_if_path(tp, object_node, segments);

        if (direct_path || scheme >= 0) {
            // It's a path subscript expression: path[expr]
            // Build it as a special AST_NODE_PATH_INDEX_EXPR
            AstPathIndexNode* path_idx = (AstPathIndexNode*)alloc_ast_node(tp, AST_NODE_PATH_INDEX_EXPR, child, sizeof(AstPathIndexNode));
            path_idx->base_path = direct_path ? direct_path :
                build_path_expr(tp, object_node, (PathScheme)scheme, segments);
            path_idx->segment_expr = build_expr(tp, field_node);
            path_idx->type = &TYPE_PATH;  // result is still a path
            ast_node->expr = (AstNode*)path_idx;
            ast_node->type = path_idx->type;
        } else {
            // Regular index expression
            ast_node->expr = build_field_expr(tp, child, AST_NODE_INDEX_EXPR);
            ast_node->type = ast_node->expr->type;
        }
        arraylist_free(segments);
    }
    else if (symbol == SYM_CALL_EXPR) { // || symbol == SYM_SYS_FUNC
        ast_node->expr = build_call_expr(tp, child, symbol);
        ast_node->type = ast_node->expr->type;
    }
    else if (symbol == SYM_QUERY_EXPR) {
        // query expression: expr?T (recursive) or expr.?T (direct)
        AstQueryNode* query_node = (AstQueryNode*)alloc_ast_node(tp, AST_NODE_QUERY_EXPR, child, sizeof(AstQueryNode));
        TSNode object_node = ts_node_child_by_field_id(child, FIELD_OBJECT);
        query_node->object = build_expr(tp, object_node);
        TSNode query_type_node = ts_node_child_by_field_id(child, FIELD_QUERY);
        // the query operand is a primary-type token now (see the trimmed grammar)
        query_node->query = build_expr(tp, query_type_node);
        // determine direct vs recursive from the operator field
        TSNode op_node = ts_node_child_by_field_id(child, FIELD_OP);
        StrView op = ts_node_source(tp, op_node);
        query_node->direct = (op.length == 2);  // ".?" is direct, "?" is recursive
        // query always returns a list of matches
        query_node->type = alloc_type(tp->pool, LMD_TYPE_ARRAY, sizeof(TypeList));
        log_debug("build query_expr: direct=%d", query_node->direct);
        ast_node->expr = (AstNode*)query_node;
        ast_node->type = query_node->type;
    }
    else if (symbol == SYM_CURRENT_EXPR) {
        ast_node->expr = build_current_expr(tp, child);
        ast_node->type = ast_node->expr->type;
    }
    else { // from _parenthesized_expr
        ast_node->expr = build_expr(tp, child);
        if (ast_node->expr) {
            ast_node->type = ast_node->expr->type;
        } else {
            // build_expr returned NULL (e.g., comment-only parenthesized expr or parse error)
            record_semantic_error(tp, child, ERR_INVALID_LITERAL, "Empty or invalid parenthesized expression");
            return (AstNode*)ast_node;
        }
    }
    log_debug("end build primary expr");
    return (AstNode*)ast_node;
}

// Build type negation expression: !T → any ! T (exclude type)
// Creates a TypeBinary(OPERATOR_EXCLUDE, any, T) so that `x is !string` works
AstNode* build_type_negation_expr(Transpiler* tp, TSNode node) {
    log_debug("build type negation expr (!T)");
    AstBinaryNode* ast_node = (AstBinaryNode*)alloc_ast_node(tp,
        AST_NODE_BINARY_TYPE, node, sizeof(AstBinaryNode));
    ast_node->type = alloc_type(tp->pool, LMD_TYPE_TYPE, sizeof(TypeType));
    TypeBinary* type = (TypeBinary*)alloc_type_kind(tp->pool, TYPE_KIND_BINARY, sizeof(TypeBinary));
    ((TypeType*)ast_node->type)->type = (Type*)type;

    // Build the operand (the type to negate)
    TSNode operand_node = ts_node_child_by_field_id(node, FIELD_OPERAND);
    ast_node->right = build_expr(tp, operand_node);
    if (!ast_node->right) {
        log_error("Error: build_type_negation_expr failed to build operand");
        ast_node->type = &TYPE_ERROR;
        return (AstNode*)ast_node;
    }

    // Create synthetic "any" node for the left side
    AstPrimaryNode* any_node = (AstPrimaryNode*)alloc_ast_node(tp,
        AST_NODE_PRIMARY, node, sizeof(AstPrimaryNode));
    any_node->type = alloc_type(tp->pool, LMD_TYPE_TYPE, sizeof(TypeType));
    ((TypeType*)any_node->type)->type = set_type_any(tp, ANY_EXPLICIT);
    ast_node->left = (AstNode*)any_node;

    ast_node->op = OPERATOR_EXCLUDE;
    ast_node->op_str = {.str = "!", .length = 1};

    type->left = ast_node->left->type;
    type->right = ast_node->right->type;
    type->op = OPERATOR_EXCLUDE;
    arraylist_append(tp->type_list, ast_node->type);
    type->type_index = tp->type_list->length - 1;

    log_debug("type negation expr created: any ! T, index: %d", type->type_index);
    return (AstNode*)ast_node;
}

AstNode* build_spread_expr(Transpiler* tp, TSNode sp_node);

AstNode* build_unary_expr(Transpiler* tp, TSNode bi_node) {
    log_debug("build unary expr");

    // Check for ! operator early — it means type negation, not logical NOT
    TSNode op_node = ts_node_child_by_field_id(bi_node, FIELD_OPERATOR);
    StrView op = ts_node_source(tp, op_node);
    if (strview_equal(&op, "!")) {
        return build_type_negation_expr(tp, bi_node);
    }
    // * operator is spread — route to build_spread_expr
    if (strview_equal(&op, "*")) {
        return build_spread_expr(tp, bi_node);
    }

    AstUnaryNode* ast_node = (AstUnaryNode*)alloc_ast_node(tp, AST_NODE_UNARY, bi_node, sizeof(AstUnaryNode));
    ast_node->op_str = op;
    if (strview_equal(&op, "not")) { ast_node->op = OPERATOR_NOT; }
    else if (strview_equal(&op, "-")) { ast_node->op = OPERATOR_NEG; }
    else if (strview_equal(&op, "+")) { ast_node->op = OPERATOR_POS; }

    TSNode operand_node = ts_node_child_by_field_id(bi_node, FIELD_OPERAND);
    ast_node->operand = build_expr(tp, operand_node);

    // Defensive validation: ensure operand was built successfully
    if (!ast_node->operand) {
        log_error("Error: build_unary_expr failed to build operand");
        ast_node->type = &TYPE_ERROR;
        return (AstNode*)ast_node;
    }

    // Additional validation: ensure operand has valid type
    if (!ast_node->operand->type) {
        log_error("Error: build_unary_expr operand missing type information");
        ast_node->type = &TYPE_ERROR;
        return (AstNode*)ast_node;
    }

    // More robust type inference based on operator and operand type
    TypeId operand_type = ast_node->operand->type->type_id;
    TypeId type_id;

    if (ast_node->op == OPERATOR_NOT) {
        type_id = LMD_TYPE_BOOL;
    }
    else if (ast_node->op == OPERATOR_POS || ast_node->op == OPERATOR_NEG) {
        // For numeric unary operators (+/-), preserve the operand type if numeric
        if (operand_type == LMD_TYPE_NUM_SIZED || operand_type == LMD_TYPE_UINT64) {
            ast_node->type = ast_node->operand->type;
            log_debug("end build unary expr");
            return (AstNode*)ast_node;
        }
        if (IS_NUMERIC_ID(operand_type)) {
            type_id = operand_type;  // Preserve the exact numeric type
        }
        else if (operand_type != LMD_TYPE_ANY) {
            // A non-numeric operand of known type cannot silently become `any`:
            // negation either produces a number or raises, so the honest type
            // is `number | error` [TIG12, S7.2.1]. An open (`any`) operand
            // stays open — nothing has been proven about it.
            ast_node->type = lambda_type_union_normalized(tp->pool,
                &TYPE_NUMBER, &TYPE_ERROR);
            log_debug("end build unary expr");
            return (AstNode*)ast_node;
        }
        else {
            type_id = census_any_type_id(tp, ANY_UNARY);  // open operand stays open
        }
    }
    else {
        log_error("Error: build_unary_expr unknown operator");
        type_id = census_any_type_id(tp, ANY_ERROR_RECOVERY);  // Default fallback
    }

    ast_node->type = alloc_type(tp->pool, type_id, sizeof(Type));

    log_debug("end build unary expr");
    return (AstNode*)ast_node;
}

// build spread expression: *expr
AstNode* build_spread_expr(Transpiler* tp, TSNode sp_node) {
    log_debug("build spread expr");
    AstUnaryNode* ast_node = (AstUnaryNode*)alloc_ast_node(tp, AST_NODE_SPREAD, sp_node, sizeof(AstUnaryNode));
    ast_node->op = OPERATOR_SPREAD;
    ast_node->op_str = StrView{"*", 1};

    TSNode operand_node = ts_node_child_by_field_id(sp_node, FIELD_OPERAND);
    ast_node->operand = build_expr(tp, operand_node);

    if (!ast_node->operand) {
        log_error("Error: build_spread_expr failed to build operand");
        ast_node->type = &TYPE_ERROR;
        return (AstNode*)ast_node;
    }

    if (!ast_node->operand->type) {
        log_error("Error: build_spread_expr operand missing type information");
        ast_node->type = &TYPE_ERROR;
        return (AstNode*)ast_node;
    }

    // spread expression has the same type as its operand (the items will be spread)
    ast_node->type = ast_node->operand->type;

    // Check: spread on a static scalar expression is redundant and likely a mistake.
    // e.g., *5, *"hello", *true, *null — these have no elements to spread.
    {
        Type* op_type = ast_node->operand->type;
        if (op_type && op_type->is_literal) {
            TypeId tid = op_type->type_id;
            if (is_static_spread_scalar_type_id(tid)) {
                record_semantic_error(tp, sp_node, ERR_SEMANTIC_ERROR,
                    "Spread operator '*' is redundant on scalar value");
            }
        }
    }

    log_debug("end build spread expr");
    return (AstNode*)ast_node;
}

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

static void lint_condition_expr(Transpiler* tp, TSNode cond_node, AstNode* cond, const char* context) {
    if (!cond) return;
    TSPoint point = ts_node_start_point(cond_node);
    int line = (int)point.row + 1;

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

static bool known_numeric_array_type(Type* type) {
    if (!type) return false;
    if (type->type_id == LMD_TYPE_ARRAY_NUM) return true;
    if (type->type_id != LMD_TYPE_ARRAY) return false;
    TypeArray* array_type = (TypeArray*)type;
    return array_type->nested &&
        lambda_numeric_kind_from_type(array_type->nested) != LAMBDA_NUM_INVALID;
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

static void normalize_is_array_type_rhs(Transpiler* tp, AstBinaryNode* ast_node) {
    if (!ast_node || ast_node->op != OPERATOR_IS || !ast_node->right) return;
    AstNode* rhs = ast_node->right;
    if (rhs->node_type == AST_NODE_PRIMARY) {
        AstPrimaryNode* primary = (AstPrimaryNode*)rhs;
        rhs = primary->expr;
    }
    if (!rhs || rhs->node_type != AST_NODE_ARRAY) return;

    AstArrayNode* arr = (AstArrayNode*)rhs;
    Type* nested_type = NULL;
    int64_t length = 0;
    for (AstNode* item = arr->item; item; item = item->next) {
        if (!item->type) return;
        Type* actual_type = item->type;
        if (item->type->type_id == LMD_TYPE_TYPE) actual_type = ((TypeType*)item->type)->type;
        else if (!item->type->is_literal) return;
        if (!actual_type) return;
        if (!nested_type) nested_type = actual_type;
        else if (nested_type->type_id != actual_type->type_id) nested_type = NULL;
        length++;
    }
    if (length == 0) return;

    // `is [T]` parses as an array literal; reinterpret literal/type-valued items as tuple patterns.
    TypeType* node_type = (TypeType*)alloc_type(tp->pool, LMD_TYPE_TYPE, sizeof(TypeType));
    TypeArray* type = (TypeArray*)alloc_type(tp->pool, LMD_TYPE_ARRAY, sizeof(TypeArray));
    node_type->type = (Type*)type;
    type->nested = nested_type;
    type->length = length;
    type->item_patterns = (Item*)pool_calloc(tp->pool, sizeof(Item) * (size_t)length);
    type->item_is_type_pattern = (uint8_t*)pool_calloc(tp->pool, sizeof(uint8_t) * (size_t)length);
    int64_t index = 0;
    for (AstNode* item = arr->item; item; item = item->next) {
        if (item->type->type_id == LMD_TYPE_TYPE) {
            type->item_patterns[index].type = item->type;
            type->item_is_type_pattern[index] = 1;
        }
        else {
            Item literal = ItemNull;
            if (ast_static_literal_item(tp, item, &literal)) type->item_patterns[index] = literal;
        }
        index++;
    }
    if (type->length == 1 && type->item_is_type_pattern[0]) {
        log_warn("lambda_array_pattern_hint: bare [T] is an exact one-item pattern; use T[] for homogeneous arrays");
    }
    arraylist_append(tp->type_list, node_type);
    type->type_index = tp->type_list->length - 1;
    rhs->node_type = AST_NODE_ARRAY_TYPE;
    rhs->type = (Type*)node_type;
}

static bool pipe_rhs_is_legacy_file_target(TSNode node) {
    TSSymbol sym = ts_node_symbol(node);
    if (sym == SYM_EXPR || sym == SYM_PRIMARY_EXPR) {
        TSNode child = ts_node_named_child(node, 0);
        if (!ts_node_is_null(child)) sym = ts_node_symbol(child);
    }
    return sym == SYM_STRING || sym == SYM_PATH_EXPR;
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

static bool promote_type_union_expr(Transpiler* tp, AstBinaryNode* ast_node) {
    if (!ast_node || ast_node->op != OPERATOR_UNION ||
        !ast_is_explicit_type_value(ast_node->left) ||
        !ast_is_explicit_type_value(ast_node->right)) {
        return false;
    }

    // Phase 6 makes `|` union in expression position too; type-valued operands
    // must build a first-class binary type instead of falling into runtime ops.
    ast_node->node_type = AST_NODE_BINARY_TYPE;
    TypeType* node_type = (TypeType*)alloc_type(tp->pool, LMD_TYPE_TYPE, sizeof(TypeType));
    TypeBinary* type = (TypeBinary*)alloc_type_kind(tp->pool, TYPE_KIND_BINARY, sizeof(TypeBinary));
    node_type->type = (Type*)type;
    type->left = ((TypeType*)ast_node->left->type)->type;
    type->right = ((TypeType*)ast_node->right->type)->type;
    type->op = OPERATOR_UNION;
    ast_node->type = (Type*)node_type;
    arraylist_append(tp->type_list, ast_node->type);
    type->type_index = tp->type_list->length - 1;
    return true;
}

static AstNode* promote_bare_pipe_sysfunc(Transpiler* tp, TSNode right_node, AstNode* built_right) {
    AstNode* target = built_right;
    if (target && target->node_type == AST_NODE_PRIMARY) {
        AstPrimaryNode* primary = (AstPrimaryNode*)target;
        if (primary->expr) target = primary->expr;
    }

    StrView func_name = {0};
    if (target && target->node_type == AST_NODE_IDENT) {
        AstIdentNode* ident = (AstIdentNode*)target;
        if (ident->entry) return NULL;
        func_name.str = ident->name->chars;
        func_name.length = ident->name->len;
    } else if (target && target->node_type == AST_NODE_SYS_FUNC) {
        AstSysFuncNode* sys = (AstSysFuncNode*)target;
        if (!sys->fn_info) return NULL;
        func_name = strview_from_cstr(sys->fn_info->name);
    } else {
        return NULL;
    }

    NameEntry* user_name = lookup_name(tp, func_name);
    if (user_name && user_name->node && user_name->node->node_type == AST_NODE_FUNC) {
        return NULL;
    }

    SysFuncInfo* sys_func_info = get_sys_func_info(&func_name, 1);
    if (!sys_func_info) {
        sys_func_info = lookup_global_imported_sys_func(tp, &func_name, 1);
    }
    if (!sys_func_info) return NULL;

    if (sys_func_info->is_proc && (!tp->current_scope || !tp->current_scope->is_proc)) {
        record_semantic_error(tp, right_node, ERR_PROC_IN_FN,
            "procedure '%.*s' cannot be called in a function",
            (int)func_name.length, func_name.str);
    }

    AstCallNode* call = (AstCallNode*)alloc_ast_node(tp, AST_NODE_CALL_EXPR, right_node, sizeof(AstCallNode));
    AstSysFuncNode* fn_node = (AstSysFuncNode*)alloc_ast_node(tp, AST_NODE_SYS_FUNC, right_node, sizeof(AstSysFuncNode));
    fn_node->fn_info = sys_func_info;
    fn_node->type = sys_func_info->return_type;
    call->function = (AstNode*)fn_node;
    call->argument = NULL;
    // Bare sysfunc pipe RHS has no call node, so inject before lowering sees an undefined identifier.
    call->pipe_inject = true;
    call->propagate = false;
    call->can_raise = sys_func_info->can_raise;
    call->type = sys_func_call_result_type(tp, sys_func_info,
        sys_func_info->may_return_error, NULL);
    return (AstNode*)call;
}

AstNode* build_binary_expr(Transpiler* tp, TSNode bi_node) {
    log_debug("build binary expr");
    AstBinaryNode* ast_node = (AstBinaryNode*)alloc_ast_node(tp, AST_NODE_BINARY, bi_node, sizeof(AstBinaryNode));
    TSNode left_node = ts_node_child_by_field_id(bi_node, FIELD_LEFT);
    ast_node->left = build_expr(tp, left_node);

    // Defensive validation: ensure left operand was built successfully
    if (!ast_node->left) {
        log_error("Error: build_binary_expr failed to build left operand");
        ast_node->type = &TYPE_ERROR;
        return (AstNode*)ast_node;
    }

    TSNode op_node = ts_node_child_by_field_id(bi_node, FIELD_OPERATOR);
    TSNode right_node = ts_node_child_by_field_id(bi_node, FIELD_RIGHT);
    StrView op;
    if (!ts_node_is_null(op_node)) {
        op = ts_node_source(tp, op_node);
    } else if (!ts_node_is_null(right_node)) {
        // Grouped operator tokens are hidden from the CST; their exact spelling
        // is the source span between the already-fielded operand boundaries.
        uint32_t left_end = ts_node_end_byte(left_node);
        uint32_t right_start = ts_node_start_byte(right_node);
        op.str = tp->source + left_end;
        op.length = right_start - left_end;
    } else {
        op = {nullptr, 0};
    }
    strview_trim(&op);
    ast_node->op_str = op;
    if (strview_equal(&op, "and")) { ast_node->op = OPERATOR_AND; }
    else if (strview_equal(&op, "or")) { ast_node->op = OPERATOR_OR; }
    else if (strview_equal(&op, "+")) { ast_node->op = OPERATOR_ADD; }
    else if (strview_equal(&op, "++")) { ast_node->op = OPERATOR_JOIN; }
    else if (strview_equal(&op, "-")) { ast_node->op = OPERATOR_SUB; }
    else if (strview_equal(&op, "*")) { ast_node->op = OPERATOR_MUL; }
    else if (strview_equal(&op, "**")) { ast_node->op = OPERATOR_POW; }
    else if (strview_equal(&op, "/")) { ast_node->op = OPERATOR_DIV; }
    else if (strview_equal(&op, "div")) { ast_node->op = OPERATOR_IDIV; }
    else if (strview_equal(&op, "%")) { ast_node->op = OPERATOR_MOD; }
    else if (strview_equal(&op, "==")) { ast_node->op = OPERATOR_EQ; }
    else if (strview_equal(&op, "!=")) { ast_node->op = OPERATOR_NE; }
    else if (strview_equal(&op, "<")) { ast_node->op = OPERATOR_LT; }
    else if (strview_equal(&op, "<=")) { ast_node->op = OPERATOR_LE; }
    else if (strview_equal(&op, ">")) { ast_node->op = OPERATOR_GT; }
    else if (strview_equal(&op, ">=")) { ast_node->op = OPERATOR_GE; }
    else if (strview_equal(&op, "eq")) { ast_node->op = OPERATOR_ELEM_EQ; }
    else if (strview_equal(&op, "ne")) { ast_node->op = OPERATOR_ELEM_NE; }
    else if (strview_equal(&op, "lt")) { ast_node->op = OPERATOR_ELEM_LT; }
    else if (strview_equal(&op, "le")) { ast_node->op = OPERATOR_ELEM_LE; }
    else if (strview_equal(&op, "gt")) { ast_node->op = OPERATOR_ELEM_GT; }
    else if (strview_equal(&op, "ge")) { ast_node->op = OPERATOR_ELEM_GE; }
    else if (strview_equal(&op, "to")) { ast_node->op = OPERATOR_TO; }
    else if (strview_equal(&op, "|")) { ast_node->op = OPERATOR_UNION; }
    else if (strview_equal(&op, "|>")) { ast_node->op = OPERATOR_PIPE; }
    else if (strview_equal(&op, "where")) { ast_node->op = OPERATOR_WHERE; }
    else if (strview_equal(&op, "that")) { ast_node->op = OPERATOR_WHERE; }  // 'that' is filter like 'where'
    else if (strview_equal(&op, "&")) { ast_node->op = OPERATOR_INTERSECT; }
    else if (strview_equal(&op, "!")) { ast_node->op = OPERATOR_EXCLUDE; }
    else if (strview_equal(&op, "is")) { ast_node->op = OPERATOR_IS; }
    else if (strview_equal(&op, "in")) { ast_node->op = OPERATOR_IN; }
    else if (strview_equal(&op, "at")) { ast_node->op = OPERATOR_AT; }
    else {
        log_error("Error: build_binary_expr unknown operator: %.*s", (int)op.length, op.str);
        ast_node->op = OPERATOR_ADD; // Default fallback to prevent crashes
    }

    // For pipe operator: check if RHS uses ~ (current_item)
    // If not, inject the left side as first argument at call lookup time
    bool pipe_inject = false;
    if (ast_node->op == OPERATOR_PIPE) {
        if (!tsnode_has_current_item_ref(tp, right_node)) {
            if (pipe_rhs_is_legacy_file_target(right_node)) {
                // `|>` is now the pipe operator; file output is explicit.
                record_semantic_error(tp, right_node, ERR_INVALID_OPERATION,
                    "file output has moved; use output(data, file)");
            }
            tp->pipe_inject_args = 1;
            pipe_inject = true;
            log_debug("pipe without ~: will inject first arg");
        }
    }

    // For 'that' operator: enable implicit ~.name resolution for bare identifiers
    bool is_that = strview_equal(&op, "that");
    bool old_in_that = tp->in_that_clause;
    if (is_that) tp->in_that_clause = true;

    ast_node->right = build_expr(tp, right_node);

    // Reset pipe_inject_args after building right side
    if (pipe_inject) {
        tp->pipe_inject_args = 0;
    }
    // Reset that clause flag
    tp->in_that_clause = old_in_that;

    // Defensive validation: ensure right operand was built successfully
    if (!ast_node->right) {
        log_error("Error: build_binary_expr failed to build right operand");
        ast_node->type = &TYPE_ERROR;
        return (AstNode*)ast_node;
    }

    if (pipe_inject && ast_node->op == OPERATOR_PIPE) {
        AstNode* pipe_call = promote_bare_pipe_sysfunc(tp, right_node, ast_node->right);
        if (pipe_call) ast_node->right = pipe_call;
    }

    normalize_is_array_type_rhs(tp, ast_node);
    if (promote_type_union_expr(tp, ast_node)) {
        return (AstNode*)ast_node;
    }

    // Special case: 'expr is nan' — IEEE NaN check
    // nan is a float value, not a type, so 'is' type-check doesn't work.
    // Detect NaN literal on RHS and rewrite to OPERATOR_IS_NAN (unary-like check).
    if (ast_node->op == OPERATOR_IS) {
        AstNode* rhs = ast_node->right;
        if (rhs->node_type == AST_NODE_PRIMARY) {
            AstPrimaryNode* pri = (AstPrimaryNode*)rhs;
            if (pri->type && pri->type->type_id == LMD_TYPE_FLOAT) {
                TypeFloat* ft = (TypeFloat*)pri->type;
                if (__builtin_isnan(ft->double_val)) {
                    log_debug("build_binary_expr: detected 'expr is nan', rewriting to OPERATOR_IS_NAN");
                    ast_node->op = OPERATOR_IS_NAN;
                    ast_node->type = &TYPE_BOOL;
                    return (AstNode*)ast_node;
                }
            }
        }
    }

    // Chained comparison transformation: a < b < c => (a < b) and (b < c)
    // Detect when left operand is a comparison and current op is also a comparison
    if (is_relational_op(ast_node->op) && ast_node->left->node_type == AST_NODE_BINARY) {
        AstBinaryNode* left_cmp = (AstBinaryNode*)ast_node->left;
        if (is_relational_op(left_cmp->op)) {
            log_debug("chained comparison detected: transforming to AND");
            // Transform: (a op1 b) op2 c  =>  (a op1 b) and (b op2 c)
            // Create a new comparison node for: b op2 c
            AstBinaryNode* right_cmp = (AstBinaryNode*)pool_calloc(tp->pool, sizeof(AstBinaryNode));
            right_cmp->node_type = AST_NODE_BINARY;
            right_cmp->node = bi_node;
            right_cmp->left = left_cmp->right;  // reuse 'b' from left comparison
            right_cmp->op = ast_node->op;       // op2
            right_cmp->op_str = ast_node->op_str;
            right_cmp->right = ast_node->right; // c
            right_cmp->type = &TYPE_BOOL;

            // Transform current node into AND node
            ast_node->left = (AstNode*)left_cmp;  // left stays the same (a op1 b)
            ast_node->op = OPERATOR_AND;
            ast_node->op_str = {.str = "and", .length = 3};
            ast_node->right = (AstNode*)right_cmp;
            ast_node->type = &TYPE_BOOL;  // AND produces bool

            return (AstNode*)ast_node;
        }
    }

    // Additional validation: ensure both operands have valid types
    if (!ast_node->left->type || !ast_node->right->type) {
        log_error("Error: build_binary_expr operands missing type information");
        ast_node->type = &TYPE_ERROR;
        return (AstNode*)ast_node;
    }

    if (ast_node->op == OPERATOR_OR && ast_is_explicit_type_value(ast_node->left) &&
            ast_is_explicit_type_value(ast_node->right)) {
        // `or` recovers a runtime value from error/null. Type values are
        // neither. TypeId alone also labels ordinary unresolved call results
        // as `type`, so the lint must inspect source shape before rejecting it.
        record_semantic_error(tp, bi_node, ERR_INVALID_OPERATION,
            "operator `or` cannot combine type values; use `|` to form a union type");
        ast_node->type = &TYPE_ERROR;
        return (AstNode*)ast_node;
    }

    if ((ast_node->op == OPERATOR_IDIV || ast_node->op == OPERATOR_MOD) &&
        ast_static_numeric_literal_is_zero(tp, ast_node->right)) {
        // A literal integral zero is knowable before lowering; rejecting it
        // here keeps static diagnostics aligned with the runtime error contract.
        record_semantic_error(tp, right_node, ERR_INVALID_OPERATION,
            "integral division or remainder by literal zero");
        ast_node->type = &TYPE_ERROR;
        return (AstNode*)ast_node;
    }

    TypeId left_type = ast_node->left->type->type_id, right_type = ast_node->right->type->type_id;
    log_debug("left type: %d, right type: %d", left_type, right_type);
    Type* comparison_left = lambda_type_remove_error_and_null(tp->pool,
        ast_node->left->type);
    Type* comparison_right = lambda_type_remove_error_and_null(tp->pool,
        ast_node->right->type);
    if (is_relational_op(ast_node->op) && comparison_left && comparison_right &&
            !known_magnitude_comparable_type_set(comparison_left, comparison_right)) {
        // A converter's ordinary ItemError propagates through the boxed
        // comparison at runtime, and null absorbs the comparison. Validate
        // only the remaining success constituents so `int? > int` stays
        // valid while `string? > int` is rejected.
        record_semantic_error(tp, bi_node, ERR_INVALID_OPERATION,
            "ordered comparison has no magnitude for these types; use sort() for total ordering");
        ast_node->type = &TYPE_ERROR;
        return (AstNode*)ast_node;
    }
    TypeId type_id;
    Type* complete_numeric_type = NULL;
    Type* inferred_binary_type = NULL;
    bool arithmetic_op = ast_node->op == OPERATOR_ADD || ast_node->op == OPERATOR_SUB ||
        ast_node->op == OPERATOR_MUL || ast_node->op == OPERATOR_DIV ||
        ast_node->op == OPERATOR_IDIV || ast_node->op == OPERATOR_MOD;
    bool complex_arithmetic_op = arithmetic_op || ast_node->op == OPERATOR_POW;
    bool left_numeric_array = known_numeric_array_type(ast_node->left->type);
    bool right_numeric_array = known_numeric_array_type(ast_node->right->type);
    bool left_numeric_scalar = lambda_numeric_kind_from_type(ast_node->left->type) != LAMBDA_NUM_INVALID;
    bool right_numeric_scalar = lambda_numeric_kind_from_type(ast_node->right->type) != LAMBDA_NUM_INVALID;
    if (arithmetic_op &&
        ((left_numeric_array && (right_numeric_array || right_numeric_scalar)) ||
         (right_numeric_array && left_numeric_scalar))) {
        // Numeric vector operations finalize to ArrayNum. Describing their
        // result as the source TypeArray made the old call path use array_get
        // on an ArrayNum pointer after a vector expression.
        type_id = LMD_TYPE_ARRAY_NUM;
    }
    else if (arithmetic_op && ast_node->op == OPERATOR_ADD &&
        left_type == LMD_TYPE_ARRAY && right_type == LMD_TYPE_ARRAY) {
        type_id = LMD_TYPE_ARRAY;
    }
    else if (complex_arithmetic_op && (left_type == LMD_TYPE_COMPLEX || right_type == LMD_TYPE_COMPLEX)) {
        bool supported = ast_node->op == OPERATOR_ADD || ast_node->op == OPERATOR_SUB ||
            ast_node->op == OPERATOR_MUL || ast_node->op == OPERATOR_DIV ||
            ast_node->op == OPERATOR_POW;
        bool left_valid = left_type == LMD_TYPE_COMPLEX || is_complex_component_type(left_type);
        bool right_valid = right_type == LMD_TYPE_COMPLEX || is_complex_component_type(right_type);
        if (!supported || !left_valid || !right_valid) {
            record_semantic_error(tp, bi_node, ERR_INVALID_OPERATION,
                "operator '%.*s' is not defined for %s and %s",
                (int)op.length, op.str, get_type_name(left_type), get_type_name(right_type));
            ast_node->type = &TYPE_ERROR;
            return (AstNode*)ast_node;
        }
        type_id = LMD_TYPE_COMPLEX;
    }
    else if (arithmetic_op) {
        LambdaNumericOpFamily family = ast_node->op == OPERATOR_ADD ? LAMBDA_NUM_OP_ADD :
            ast_node->op == OPERATOR_SUB ? LAMBDA_NUM_OP_SUB :
            ast_node->op == OPERATOR_MUL ? LAMBDA_NUM_OP_MUL :
            ast_node->op == OPERATOR_DIV ? LAMBDA_NUM_OP_TRUE_DIV :
            ast_node->op == OPERATOR_IDIV ? LAMBDA_NUM_OP_IDIV : LAMBDA_NUM_OP_MOD;
        LambdaNumericDecision decision = lambda_numeric_classify(family,
            lambda_numeric_kind_from_type(ast_node->left->type),
            lambda_numeric_kind_from_type(ast_node->right->type));
        if (decision.valid) {
            complete_numeric_type = lambda_numeric_type_from_kind(decision.result);
            type_id = complete_numeric_type->type_id;
        } else {
            LambdaNumericKind left_kind = lambda_numeric_kind_from_type(ast_node->left->type);
            LambdaNumericKind right_kind = lambda_numeric_kind_from_type(ast_node->right->type);
            if (left_kind != LAMBDA_NUM_INVALID && right_kind != LAMBDA_NUM_INVALID) {
                // A fully known scalar pair that has no numeric-domain rule is
                // an invalid program, not a request for dynamic dispatch.
                record_semantic_error(tp, bi_node, ERR_INVALID_OPERATION,
                    "operator '%.*s' is not defined for %s and %s",
                    (int)op.length, op.str,
                    get_type_name(left_type), get_type_name(right_type));
                ast_node->type = &TYPE_ERROR;
                return (AstNode*)ast_node;
            }
            type_id = census_any_type_id(tp, ANY_ARITH_OPERAND);
        }
    }
    else if (ast_node->op == OPERATOR_AND) {
        // `and` yields the LEFT value when it is falsy, else the right value —
        // both constituents can reach the result, so the sound type is their
        // normalized union [TIG5, S5.5.2]. Unlike `or` (which only reaches its
        // right side after an error/null left), nothing may be removed here:
        // the falsy set includes `false` and `""`, which are ordinary values.
        inferred_binary_type = lambda_type_union_normalized(tp->pool,
            ast_node->left->type, ast_node->right->type);
        type_id = inferred_binary_type ? inferred_binary_type->type_id
            : census_any_type_id(tp, ANY_LOGICAL_AND);
    }
    else if (ast_node->op == OPERATOR_OR) {
        // `or` evaluates its right side only after an error/null left value;
        // retaining either constituent here would falsely leak it through the
        // next checked boundary even though this expression cannot return it.
        Type* left_clean = lambda_type_remove_error_and_null(tp->pool,
            ast_node->left->type);
        inferred_binary_type = lambda_type_union_normalized(tp->pool, left_clean,
            ast_node->right->type);
        type_id = inferred_binary_type ? inferred_binary_type->type_id : LMD_TYPE_ANY;
    }
    else if (ast_node->op == OPERATOR_EQ || ast_node->op == OPERATOR_NE ||
        ast_node->op == OPERATOR_LT || ast_node->op == OPERATOR_LE ||
        ast_node->op == OPERATOR_GT || ast_node->op == OPERATOR_GE ||
        is_elementwise_comparison_op(ast_node->op) ||
        ast_node->op == OPERATOR_IS || ast_node->op == OPERATOR_IN ||
        ast_node->op == OPERATOR_AT) {
        type_id = LMD_TYPE_BOOL;

        // Keyword comparisons are the explicit element-wise family.  Symbolic
        // < <= > >= stay scalar so array masks cannot leak into control flow.
        if (ast_node->op >= OPERATOR_LT && ast_node->op <= OPERATOR_GE) {
            // A magnitude comparison over a statically comparable pair always
            // yields bool — numeric/numeric, string/string and dtime/dtime all
            // qualify, not just the native-numeric pair the old rule admitted
            // [TIG6]. `known_magnitude_comparable` deliberately answers true
            // for ANY/NULL operands (it gates a diagnostic, not this typing),
            // so those cases are excluded here and stay open.
            bool l_open = left_type == LMD_TYPE_ANY || left_type == LMD_TYPE_NULL;
            bool r_open = right_type == LMD_TYPE_ANY || right_type == LMD_TYPE_NULL;
            if (!l_open && !r_open &&
                    known_magnitude_comparable(left_type, right_type)) {
                type_id = LMD_TYPE_BOOL;
            } else {
                type_id = census_any_type_id(tp, ANY_COMPARE);
            }
        }
        else if (is_elementwise_comparison_op(ast_node->op)) {
            bool l_arr = is_array_family_type_id(left_type);
            bool r_arr = is_array_family_type_id(right_type);
            type_id = (l_arr || r_arr) ? LMD_TYPE_ARRAY_NUM
                : census_any_type_id(tp, ANY_COMPARE);
        }

        // equality is total: incompatible concrete families compile and evaluate
        // to false/true instead of becoming a static type error.
    }
    else if (ast_node->op == OPERATOR_TO) {
        type_id = LMD_TYPE_RANGE;
    }
    else if (ast_node->op == OPERATOR_PIPE || ast_node->op == OPERATOR_WHERE) {
        // pipe operators: change node type to AST_NODE_PIPE
        ast_node->node_type = AST_NODE_PIPE;
        // determine result type based on operation
        if (ast_node->op == OPERATOR_WHERE) {
            // where preserves input collection type
            type_id = left_type;
        } else {
            // pipe: if using ~ (current item), always produces Array;
            // otherwise it's "inject first arg" and result type follows the right side
            if (has_current_item_ref(ast_node->right)) {
                // The mapped expression's type IS the element type; keeping a
                // bare ARRAY discarded it and forced every consumer back to a
                // dynamic read [TIG16, D2.6.2].
                TypeArray* mapped = (TypeArray*)alloc_type(tp->pool,
                    LMD_TYPE_ARRAY, sizeof(TypeArray));
                mapped->nested = ast_node->right->type;
                mapped->type_index = -1;
                inferred_binary_type = (Type*)mapped;
                type_id = LMD_TYPE_ARRAY;
            } else {
                type_id = right_type;
            }
        }
    }
    else {  // OPERATOR_JOIN, etc.
        type_id = census_any_type_id(tp, ANY_JOIN_OP);
    }
    if (inferred_binary_type) {
        ast_node->type = inferred_binary_type;
    } else if (complete_numeric_type) {
        ast_node->type = complete_numeric_type;
    } else if (type_id == LMD_TYPE_ARRAY_NUM) {
        ast_node->type = alloc_array_num_result_type(tp, ast_node);
    } else if (type_id >= LMD_TYPE_CONTAINER) {
        // reuse existing type from the branch that determined the type_id
        // to preserve full struct layout (TypeMap, TypeArray, etc.)
        Type* reuse_type = (type_id == right_type && ast_node->right->type) ?
            ast_node->right->type : (type_id == left_type && ast_node->left->type) ?
            ast_node->left->type : NULL;
        ast_node->type = reuse_type ? reuse_type : &TYPE_ANY;
    } else {
        ast_node->type = alloc_type(tp->pool, type_id, sizeof(Type));
    }
    log_debug("end build binary expr");
    return (AstNode*)ast_node;
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
        AstMatchArm* arm = match_node->first_arm;
        while (arm) {
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

// build current item reference (~)
AstNode* build_current_expr(Transpiler* tp, TSNode node) {
    uint32_t len = ts_node_end_byte(node) - ts_node_start_byte(node);
    bool is_index = (len == 2); // ~# is 2 chars, ~ is 1
    if (is_index) {
        log_debug("build current index (~#)");
        AstNode* ast_node = alloc_ast_node(tp, AST_NODE_CURRENT_INDEX, node, sizeof(AstNode));
        ast_node->type = alloc_type(tp->pool, LMD_TYPE_ANY, sizeof(Type));
        return ast_node;
    } else {
        log_debug("build current item (~)");
        AstNode* ast_node = alloc_ast_node(tp, AST_NODE_CURRENT_ITEM, node, sizeof(AstNode));
        ast_node->type = alloc_type(tp->pool, LMD_TYPE_ANY, sizeof(Type));
        return ast_node;
    }
}

// Build the handler-local current error reference (`^`).  The grammar admits
// the token as a primary so ordinary member/index builders can compose with it;
// semantic scope is enforced here rather than letting `^` become a global value.
static AstNode* build_current_error_expr(Transpiler* tp, TSNode node) {
    AstNode* ast_node = alloc_ast_node(tp, AST_NODE_CURRENT_ERROR, node, sizeof(AstNode));
    ast_node->type = &TYPE_ERROR;
    if (!tp->building_handler_body) {
        record_semantic_error(tp, node, ERR_INVALID_EXPR_CONTEXT,
            "current error `^` is only valid inside an error-handler body");
    }
    log_debug("build current handler error (^)");
    return ast_node;
}

static AstNode* build_null_noop(Transpiler* tp, TSNode source_node) {
    AstPrimaryNode* null_node = (AstPrimaryNode*)alloc_ast_node(tp, AST_NODE_PRIMARY,
        source_node, sizeof(AstPrimaryNode));
    null_node->type = &LIT_NULL;
    null_node->expr = NULL;
    return (AstNode*)null_node;
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

// Unified build_if_expr: handles both expression and block forms
// When a branch is a content block, creates a new scope for variable shadowing
AstNode* build_if_expr(Transpiler* tp, TSNode if_node) {
    log_debug("build if expr");
    AstIfNode* ast_node = (AstIfNode*)alloc_ast_node(tp, AST_NODE_IF_EXPR, if_node, sizeof(AstIfNode));
    TSNode cond_node = ts_node_child_by_field_id(if_node, FIELD_COND);
    ast_node->cond = build_expr(tp, cond_node);
    lint_condition_expr(tp, cond_node, ast_node->cond, "if");

    // Defensive validation: ensure condition was built successfully
    if (!ast_node->cond) {
        log_error("Error: build_if_expr failed to build condition expression");
        ast_node->type = &TYPE_ERROR;
        return (AstNode*)ast_node;
    }

    // Build 'then' branch — create scope if it's a content block
    TSNode then_node = ts_node_child_by_field_id(if_node, FIELD_THEN);
    bool then_is_block = (!ts_node_is_null(then_node) && ts_node_symbol(then_node) == SYM_CONTENT);
    if (then_is_block) {
        NameScope* then_scope = (NameScope*)pool_calloc(tp->pool, sizeof(NameScope));
        then_scope->parent = tp->current_scope;
        then_scope->is_proc = tp->current_scope->is_proc;
        tp->current_scope = then_scope;
    }
    if (ts_node_is_null(then_node)) {
        // empty branch blocks are legal no-ops; represent them as null so pn control flow continues.
        ast_node->then = build_null_noop(tp, if_node);
    } else {
        ast_node->then = build_expr(tp, then_node);
    }
    if (then_is_block) {
        // Attach the branch scope to the content node it scopes. Lowering
        // resolves these names through its own hashmaps, so this field was
        // left unset and the scope became unreachable from the AST — which any
        // later pass walking scopes (the T0 frame plan) needs it to be.
        if (ast_node->then && ast_node->then->node_type == AST_NODE_CONTENT) {
            ((AstListNode*)ast_node->then)->vars = tp->current_scope;
        }
        tp->current_scope = tp->current_scope->parent;  // restore scope
    }

    // Defensive validation: ensure then clause was built successfully
    if (!ast_node->then) {
        log_error("Error: build_if_expr failed to build then expression");
        ast_node->type = &TYPE_ERROR;
        return (AstNode*)ast_node;
    }

    // Build 'else' branch — create scope if it's a content block
    TSNode else_node = ts_node_child_by_field_id(if_node, FIELD_ELSE);
    if (ts_node_is_null(else_node)) {
        // empty else blocks have no content field; missing else and no-op else both evaluate to null.
        ast_node->otherwise = NULL;
    }
    else {
        bool else_is_block = (ts_node_symbol(else_node) == SYM_CONTENT);
        if (else_is_block) {
            NameScope* else_scope = (NameScope*)pool_calloc(tp->pool, sizeof(NameScope));
            else_scope->parent = tp->current_scope;
            else_scope->is_proc = tp->current_scope->is_proc;
            tp->current_scope = else_scope;
        }
        ast_node->otherwise = build_expr(tp, else_node);
        if (else_is_block) {
        // Attach the branch scope to the content node it scopes. Lowering
            // resolves these names through its own hashmaps, so this field was
            // left unset and the scope became unreachable from the AST — which any
            // later pass walking scopes (the T0 frame plan) needs it to be.
            if (ast_node->otherwise && ast_node->otherwise->node_type == AST_NODE_CONTENT) {
                ((AstListNode*)ast_node->otherwise)->vars = tp->current_scope;
            }
            tp->current_scope = tp->current_scope->parent;  // restore scope
        }
        // Defensive validation: if else node exists, ensure it was built successfully
        if (!ast_node->otherwise) {
            log_error("Error: build_if_expr failed to build else expression");
            ast_node->type = &TYPE_ERROR;
            return (AstNode*)ast_node;
        }
    }

    // Additional validation: ensure expressions have valid types
    if (!ast_node->cond->type || !ast_node->then->type ||
        (ast_node->otherwise && !ast_node->otherwise->type)) {
        log_error("Error: build_if_expr expressions missing type information");
        ast_node->type = &TYPE_ERROR;
        return (AstNode*)ast_node;
    }

    // Determine the type of the if expression.
    //
    // Each arm CONTRIBUTES a type to the join. A `raise` arm never yields a
    // value — control unwinds carrying an error — so its contribution is the
    // error type, not the raised expression's own type. This is what lets a
    // one-armed-raise body type as `T | error` below instead of collapsing.
    Type* then_contrib = ast_branch_diverges(ast_node->then)
        ? &TYPE_ERROR : ast_node->then->type;
    Type* else_contrib = !ast_node->otherwise ? &TYPE_NULL
        : ast_branch_diverges(ast_node->otherwise)
            ? &TYPE_ERROR : ast_node->otherwise->type;
    TypeId then_type_id = then_contrib->type_id;
    TypeId else_type_id = else_contrib->type_id;

    LambdaNumericDecision numeric_join = lambda_numeric_classify(LAMBDA_NUM_OP_ADD,
        lambda_numeric_kind_from_type(then_contrib),
        lambda_numeric_kind_from_type(ast_node->otherwise ? else_contrib : NULL));
    if (numeric_join.valid) {
        ast_node->type = lambda_numeric_type_from_kind(numeric_join.result);
    } else if (then_type_id != else_type_id) {
        // When one arm DIVERGES by raising, the join is the union of the
        // contributions — `T | error` — not ANY: the union records both that
        // the value lane is exactly T and that error-ness is present, where
        // ANY erased the first and my earlier bare-T narrowing erased the
        // second. Representation is unaffected: a union's type_id is
        // LMD_TYPE_TYPE, which consumers already treat as boxed/dynamic
        // ("unions retain their runtime Item tag" — the shape-storage rule),
        // and the hardened native-return admission treats it as unprovable,
        // so the boxed error join that propagation/handler consumers use stays intact.
        //
        // TIG7: plain differing arms now join as the normalized union too.
        // An open arm keeps the result open — `any` already contains the other
        // constituent, so unioning with it would only re-derive `any`.
        //
        // Acceptance tightening is expected and sanctioned here: the union
        // makes error-capable branches visible to the E208 containment checker
        // that ANY was hiding. Under SI3v2/D3.3.1v2 that is the point, and
        // TI6 forbids staging it — the scripts it rejects are genuinely
        // mis-typed and are repaired in the same change.
        // TIG7 stays open, with a two-sided blocker measured in Impl §13.
        //
        // Unioning plain differing arms is right in principle and carrier-safe
        // in isolation, but two independent failures block it:
        //
        // 1. A function's forward-reference placeholder return is `any`
        //    (error-capable) while its body is still being built, so a
        //    RECURSIVE arm contributes error-ness the finished function does
        //    not have. The full union produced 109 SPURIOUS E208s across ~14
        //    library files — `last_caret_pos_in` provably returns a map on
        //    every path, yet callers were told it may return an error.
        // 2. Restricting the union to error-free arms removed those, but left
        //    21 failures including a segfault in the math package: the union
        //    type reaches consumers that decode it as a lane.
        //
        // Closing it needs resolved recursive return types (a fixpoint before
        // joins observe them) AND the carrier sweep of §10.3 — not another
        // local rule here.
        bool has_divergence = then_contrib == &TYPE_ERROR ||
            else_contrib == &TYPE_ERROR;
        if (!has_divergence ||
                then_type_id == LMD_TYPE_ANY || else_type_id == LMD_TYPE_ANY) {
            ast_node->type = set_type_any(tp, ANY_JOIN);
        } else {
            // A diverging arm still unions with TYPE_ERROR: that error comes
            // from a `raise` the source actually wrote, not from a placeholder.
            TypeBinary* join = (TypeBinary*)alloc_type_kind(tp->pool,
                TYPE_KIND_BINARY, sizeof(TypeBinary));
            join->left = then_contrib;
            join->right = else_contrib;
            join->op = OPERATOR_UNION;
            ast_node->type = (Type*)join;
        }
    } else if (then_type_id >= LMD_TYPE_CONTAINER) {
        // reuse branch type to preserve full struct (TypeMap, TypeArray, etc.)
        ast_node->type = then_contrib;
    } else {
        ast_node->type = then_contrib;
    }
    log_debug("end build if expr");
    return (AstNode*)ast_node;
}

// build a match expression from Tree-sitter CST node
AstNode* build_match(Transpiler* tp, TSNode match_node) {
    log_debug("build match expr");
    AstMatchNode* ast_node = (AstMatchNode*)alloc_ast_node(tp, AST_NODE_MATCH_EXPR, match_node, sizeof(AstMatchNode));

    // build scrutinee expression
    TSNode scrutinee_node = ts_node_child_by_field_id(match_node, FIELD_SCRUTINEE);
    ast_node->scrutinee = build_expr(tp, scrutinee_node);
    if (!ast_node->scrutinee) {
        log_error("build_match: failed to build scrutinee expression");
        ast_node->type = &TYPE_ERROR;
        return (AstNode*)ast_node;
    }

    // iterate over children to find match arms and default arms
    AstMatchArm* first_arm = NULL;
    AstMatchArm* last_arm = NULL;
    int arm_count = 0;
    TypeId result_type_id = LMD_TYPE_NULL;
    bool need_any_type = false;

    uint32_t child_count = ts_node_child_count(match_node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(match_node, i);
        if (!ts_node_is_named(child)) continue;
        TSSymbol sym = ts_node_symbol(child);

        bool is_arm = (sym == SYM_MATCH_ARM);
        bool is_default = (sym == SYM_MATCH_DEFAULT);
        if (!is_arm && !is_default) continue;

        AstMatchArm* arm = (AstMatchArm*)alloc_ast_node(tp, AST_NODE_MATCH_ARM, child, sizeof(AstMatchArm));

        // build pattern (type expression) — NULL for default arm
        if (is_arm) {
            TSNode pattern_node = ts_node_child_by_field_id(child, FIELD_PATTERN);
            arm->pattern = build_expr(tp, pattern_node);
        } else {
            arm->pattern = NULL;
        }

        // create a new scope for the arm body (like if_stam branches)
        NameScope* arm_scope = (NameScope*)pool_calloc(tp->pool, sizeof(NameScope));
        arm_scope->parent = tp->current_scope;
        arm_scope->is_proc = tp->current_scope->is_proc;
        tp->current_scope = arm_scope;

        // build arm body
        TSNode body_node = ts_node_child_by_field_id(child, FIELD_BODY);
        arm->body = build_expr(tp, body_node);

        tp->current_scope = arm_scope->parent; // restore scope

        // type inference: track union of all arm body types
        if (arm->body && arm->body->type) {
            TypeId body_type_id = arm->body->type->type_id;
            if (arm_count == 0) {
                result_type_id = body_type_id;
            } else if (body_type_id != result_type_id) {
                need_any_type = true;
            }
        }

        // link arm into list
        if (!first_arm) first_arm = arm;
        else last_arm->next = (AstNode*)arm;
        last_arm = arm;
        arm_count++;
    }

    ast_node->first_arm = first_arm;
    ast_node->arm_count = arm_count;

    AstNode* scrutinee = boundary_unwrap_primary(ast_node->scrutinee);
    if (scrutinee && scrutinee->node_type == AST_NODE_IDENT) {
        AstIdentNode* ident = (AstIdentNode*)scrutinee;
        NameEntry* entry = ident->entry;
        AstNode* binding = entry ? entry->node : NULL;
        TypeParam* parameter = binding && binding->node_type == AST_NODE_PARAM &&
                binding->type && binding->type->kind == TYPE_KIND_PARAM
            ? (TypeParam*)binding->type : NULL;
        if (parameter && !parameter->has_explicit_contract && parameter->contract_type &&
                !lambda_type_accepts_error(parameter->contract_type) &&
                match_has_error_handler(ast_node)) {
            TSPoint point = ts_node_start_point(match_node);
            // Implicit parameters short-circuit error values before the body,
            // so this arm cannot run unless the source explicitly admits error.
            log_warn("lambda_match_lint: line %u: `case error:` is unreachable for implicit parameter '%.*s'; declare '%.*s: any' to accept error values",
                point.row + 1, ident->name ? (int)ident->name->len : 9,
                ident->name ? ident->name->chars : "parameter",
                ident->name ? (int)ident->name->len : 9,
                ident->name ? ident->name->chars : "parameter");
        }
    }

    // result type: if all arms have same type, use it; otherwise ANY
    if (need_any_type) {
        ast_node->type = set_type_any(tp, ANY_JOIN);
    } else if (result_type_id >= LMD_TYPE_CONTAINER && first_arm && first_arm->type) {
        // reuse arm type to preserve full struct (TypeMap, TypeArray, etc.)
        ast_node->type = first_arm->type;
    } else {
        ast_node->type = alloc_type(tp->pool, result_type_id, sizeof(Type));
    }

    log_debug("end build match expr: %d arms", arm_count);
    return (AstNode*)ast_node;
}

AstNode* build_list(Transpiler* tp, TSNode list_node) {
    log_debug("build list");
    AstListNode* ast_node = (AstListNode*)alloc_ast_node(tp, AST_NODE_LIST, list_node, sizeof(AstListNode));
    TypeList* type = (TypeList*)alloc_type(tp->pool, LMD_TYPE_ARRAY, sizeof(TypeList));
    ast_node->list_type = type;
    // Provisional: the real answer is chosen at the exits below (a one-value
    // declaration block carries its value's type). Counting here would
    // double-report every list whose type is later refined.
    ast_node->type = NULL;

    ast_node->vars = (NameScope*)pool_calloc(tp->pool, sizeof(NameScope));
    ast_node->vars->parent = tp->current_scope;
    tp->current_scope = ast_node->vars;

    TSNode child = ts_node_named_child(list_node, 0);
    AstNode* prev_declare = NULL, * prev_item = NULL;
    while (!ts_node_is_null(child)) {
        log_debug("build_list: processing child");
        AstNode* item = build_expr(tp, child);
        if (item) {
            log_debug("build_list: got item with node_type %d", item->node_type);
            if (item->node_type == AST_NODE_ASSIGN ||
                    item->node_type == AST_NODE_DECOMPOSE) {
                // A decomposition binds names in this lexical list scope just
                // like an assignment; it must stay out of value items.
                AstNode* declare = item;
                log_debug("got declare type %d", declare->node_type);
                if (prev_declare == NULL) {
                    ast_node->declare = declare;
                }
                else {
                    prev_declare->next = declare;
                }
                prev_declare = declare;
            }
            else { // normal list item
                log_debug("build_list: adding item as list item, incrementing length from %ld to %ld", type->length, type->length + 1);
                if (!prev_item) {
                    ast_node->item = item;
                }
                else {
                    prev_item->next = item;
                }
                prev_item = item;
                type->length++;
            }
        }
        else {
            log_debug("build_list: got null item");
        }
        child = ts_node_next_named_sibling(child);
    }
    if (!ast_node->declare && type->length == 1) {
        tp->current_scope = ast_node->vars->parent;  // Fix scope restoration
        log_debug("build_list: returning single item with type %d", ast_node->item->type->type_id);
        return ast_node->item;
    }
    tp->current_scope = ast_node->vars->parent;

    // A list node's runtime value is NOT always a list: `transpile_list`
    // collapses a declaration block with exactly one value item to that value
    // (`(let x = 10, x)` evaluates to `x`). Typing follows the same split, so
    // the static type never claims a container the emitter will not build
    // [TIG11, SI3v2 — the type must describe what is actually produced].
    if (ast_node->declare && type->length == 1 && ast_node->item) {
        ast_node->type = ast_node->item->type;
    } else if (type->length > 0) {
        // Genuine multi-item list: publish the container type. Element typing
        // stays open until the items are proven homogeneous (TIG11 residue).
        ast_node->type = (Type*)type;
    } else {
        ast_node->type = set_type_any(tp, ANY_LIST);
    }
    return (AstNode*)ast_node;
}

AstNode* build_decompose_expr(Transpiler* tp, TSNode asn_node, bool is_named);

AstNode* build_assign_expr(Transpiler* tp, TSNode asn_node, bool is_type_definition) {
    // Check if this is a decomposition (multiple names)
    // Count name fields to detect decomposition pattern
    TSTreeCursor cursor = ts_tree_cursor_new(asn_node);
    bool has_child = ts_tree_cursor_goto_first_child(&cursor);
    int name_count = 0;
    bool has_decompose_field = false;
    bool is_named_decompose = false;

    while (has_child) {
        TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
        if (field_id == FIELD_NAME) {
            name_count++;
        }
        if (field_id == FIELD_DECOMPOSE) {
            has_decompose_field = true;
            // Check if it's 'at' or '='
            TSNode decompose_node = ts_tree_cursor_current_node(&cursor);
            TSSymbol sym = ts_node_symbol(decompose_node);
            if (sym == anon_sym_at) {
                is_named_decompose = true;
            }
        }
        has_child = ts_tree_cursor_goto_next_sibling(&cursor);
    }
    ts_tree_cursor_delete(&cursor);

    // If multiple names or explicit decompose field, use decomposition
    if (name_count > 1 || has_decompose_field) {
        return build_decompose_expr(tp, asn_node, is_named_decompose);
    }

    // Single variable assignment (existing logic)
    AstNamedNode* ast_node = (AstNamedNode*)alloc_ast_node(tp, AST_NODE_ASSIGN, asn_node, sizeof(AstNamedNode));

    TSNode name = ts_node_child_by_field_id(asn_node, FIELD_NAME);
    StrView name_view = node_name_text(tp, name);
    ast_node->name = name_pool_create_strview(tp->name_pool, name_view);

    // check if the variable name is a reserved type keyword
    if (!is_type_definition && is_type_keyword(name_view)) {
        int line = ts_node_start_point(name).row + 1;
        record_type_error(tp, line, "Error: '%.*s' is a reserved type keyword and cannot be used as a variable name",
            (int)name_view.length, name_view.str);
    }

    TSNode type_node = ts_node_child_by_field_id(asn_node, FIELD_TYPE);
    TSNode val_node = ts_node_child_by_field_id(asn_node, FIELD_AS);

    // Resolve an annotation before building the initializer and retain it on
    // the declaration.  The RHS keeps its own inferred type in `as->type`.
    // This ordering is the static-boundary invariant: no later pass has to
    // reconstruct whether a source annotation existed from a TypeId.
    Type* annotation_type = NULL;
    // an annotation that failed to resolve (e.g. unknown type name) already
    // reported at its own site; suppress the follow-on boundary E201, which
    // would only restate the failure as "of type error" against the same line.
    bool annotation_diagnosed = false;
    if (!is_type_definition && !ts_node_is_null(type_node)) {
        // count warnings too: in --static-warning mode the unknown-type
        // diagnostic lands as a warning, and the E201 cascade must still be
        // suppressed.
        int diags_before_annotation = tp->error_count + tp->warning_count;
        AstNode* type_expr = build_expr(tp, type_node);
        annotation_diagnosed =
            tp->error_count + tp->warning_count > diags_before_annotation;
        if (annotation_diagnosed && tp->static_warning) {
            // relaxed mode: a diagnosed annotation is no contract at all —
            // keep the initializer's inferred type instead of emitting against
            // a TYPE_ERROR contract (which the MIR boundary cannot lower).
            annotation_type = NULL;
        }
        else if (type_expr && type_expr->type && type_expr->type->type_id == LMD_TYPE_TYPE) {
            annotation_type = ((TypeType*)type_expr->type)->type;
            ast_node->declared_type = annotation_type;
        } else if (type_expr && type_expr->type && type_expr->type->type_id == LMD_TYPE_RANGE) {
            // Range annotations are value-level membership predicates, so their AST carries the range directly instead of a TypeType wrapper.
            annotation_type = type_expr->type;
            ast_node->declared_type = annotation_type;
        } else {
            StrView type_str = ts_node_source(tp, type_node);
            // conceptual spellings (e.g. float32) can reach this identifier
            // path instead of build_base_type; suggest the defined name here too.
            const char* suggestion = base_type_alias_suggestion(type_str);
            if (suggestion) {
                record_semantic_error(tp, asn_node, ERR_UNDEFINED_TYPE,
                    "invalid type annotation '%.*s'; did you mean '%s'?",
                    (int)type_str.length, type_str.str, suggestion);
            } else {
                record_semantic_error(tp, asn_node, ERR_UNDEFINED_TYPE,
                    "invalid type annotation '%.*s'", (int)type_str.length, type_str.str);
            }
        }
    }

    // handle type definitions vs variable assignments differently
    if (is_type_definition) {
        // for type statements: type Name = TypeExpr
        // build the type expression - the result's type field is a TypeType* wrapper
        // Keep the source distinction after the name is registered as an ASSIGN node;
        // MIR must not confuse a value whose inferred type is `T^` with a type alias.
        ast_node->is_type_definition = true;

        // Pre-register the type name before building the body to support self-referencing
        // types (e.g., type Node = {left: Node, right: Node}). Without this, the self-reference
        // resolves to TYPE_ANY, breaking direct field access optimization.
        TypeType* pre_type = (TypeType*)alloc_type(tp->pool, LMD_TYPE_TYPE, sizeof(TypeType));
        TypeMap* pre_map = (TypeMap*)alloc_type(tp->pool, LMD_TYPE_MAP, sizeof(TypeMap));
                pre_map->struct_name = ast_node->name->chars;
                pre_map->is_trusted_contract = true;
        pre_type->type = (Type*)pre_map;
        ast_node->type = (Type*)pre_type;
        push_name(tp, ast_node, NULL);

        if (ts_node_is_null(val_node)) {
            log_error("type definition: missing type expression");
            ast_node->type = set_type_any(tp, ANY_ERROR_RECOVERY);
            ast_node->as = nullptr;
        } else {
            AstNode* type_expr = build_expr(tp, val_node);
            ast_node->as = type_expr;
            if (type_expr && type_expr->type) {
                Type* definition_type = type_expr->type;
                bool literal_alias = (type_expr->type->type_id == LMD_TYPE_STRING ||
                        type_expr->type->type_id == LMD_TYPE_SYMBOL) &&
                    type_expr->type->is_literal;
                bool range_alias = type_expr->type->type_id == LMD_TYPE_RANGE &&
                    type_expr->type->kind == TYPE_KIND_RANGE;
                if (literal_alias || range_alias) {
                    // Literal-only and range aliases used to bypass the first-class type carrier; keep their payload under a TypeType contract so named type values remain distinguishable from data values.
                    TypeType* literal_type = (TypeType*)alloc_type(
                        tp->pool, LMD_TYPE_TYPE, sizeof(TypeType));
                    literal_type->type = type_expr->type;
                    definition_type = (Type*)literal_type;
                    arraylist_append(tp->type_list, definition_type);
                }
                ast_node->type = definition_type;  // Keep the declaration contract separate from the value expression.
                // propagate declaration name to TypeMap for direct field access optimization
                Type* inner = type_expr->type;
                if (inner && inner->type_id == LMD_TYPE_TYPE) {
                    Type* actual = ((TypeType*)inner)->type;
                    if (actual && actual->type_id == LMD_TYPE_MAP && actual != &TYPE_MAP && ast_node->name) {
                        TypeMap* actual_map = (TypeMap*)actual;
                        // Recursive fields were built against the pre-registered
                        // map. Publish the completed shape through that same
                        // identity so function contracts and self-references
                        // cannot split into placeholder and final maps.
                        *pre_map = *actual_map;
                        pre_map->struct_name = ast_node->name->chars;
                        pre_map->is_trusted_contract = true;
                        pre_type->type = (Type*)pre_map;
                        ((TypeType*)inner)->type = (Type*)pre_map;
                        if (pre_map->type_index >= 0 &&
                                pre_map->type_index < tp->type_list->length &&
                                tp->type_list->data[pre_map->type_index] == actual_map) {
                            tp->type_list->data[pre_map->type_index] = pre_map;
                        }
                        ast_node->type = (Type*)pre_type;
                    }
                }
            } else {
                log_warn("type definition: failed to build type expression");
                ast_node->type = set_type_any(tp, ANY_ERROR_RECOVERY);
            }
        }
    } else {
        // for variable assignments: let name = value or let name: type = value
        if (ts_node_is_null(val_node)) {
            log_error("assignment: missing value expression");
            ast_node->as = nullptr;
            ast_node->type = set_type_any(tp, ANY_ERROR_RECOVERY);
        } else {
            ast_node->as = build_expr(tp, val_node);

            // determine the type of the variable
            if (ts_node_is_null(type_node)) {
                ast_node->type = ast_node->as->type;
            }
            else {
                if (annotation_type) {
                    bool range_contract = annotation_type->type_id == LMD_TYPE_RANGE &&
                        annotation_type->kind == TYPE_KIND_RANGE;
                    // A range annotation constrains the value without changing its storage domain.
                    ast_node->type = range_contract && ast_node->as
                        ? ast_node->as->type : annotation_type;
                    int declaration_line = ts_node_start_point(asn_node).row + 1;
                    if (!annotation_diagnosed) {
                        bool rejected = check_declaration_static_boundary(tp,
                            ast_node, annotation_type, declaration_line);
                        if (rejected && tp->static_warning) {
                            // relaxed mode: a rejected contract must not drive
                            // representation — keep the inferred type so the
                            // binding never reinterprets the value's bits
                            // (SI14; the warning above already reported it).
                            ast_node->type = ast_node->as->type;
                            ast_node->declared_type = NULL;
                        }
                    }

                    // A declared map type is a semantic root contract, not a
                    // request to overwrite the literal's physical shape. Keep
                    // the literal's exact field carriers (especially for
                    // `int | string` fields); later legal writes rebuild and
                    // repack that runtime shape while the root stays checked
                    // against its declared map contract.

                    // compile-time type check: annotation vs RHS type
                    // when the annotation is an occurrence type (e.g., int[], float+)
                    // and the RHS is a literal array with known element types, verify compatibility
                    Type* ann_type = ast_node->type;
                    Type* rhs_type = ast_node->as ? ast_node->as->type : nullptr;
                    if (ann_type && ann_type->type_id == LMD_TYPE_ARRAY) {
                        TypeArray* ann_arr = (TypeArray*)ann_type;
                        if (ann_arr->item_patterns && ann_arr->length == 1) {
                            Type* ann_elem = ann_arr->nested;
                            if (ann_elem && ann_elem->type_id == LMD_TYPE_TYPE) ann_elem = ((TypeType*)ann_elem)->type;
                            if (rhs_type && rhs_type->type_id == LMD_TYPE_ARRAY) {
                                TypeArray* rhs_arr = (TypeArray*)rhs_type;
                                if (rhs_arr->length != 1) {
                                    int line = ts_node_start_point(asn_node).row + 1;
                                    record_type_error(tp, line,
                                        "array annotation [T] expects exactly one item; did you mean `%s[]`?",
                                        ann_elem ? get_type_name(ann_elem->type_id) : "T");
                                }
                            }
                        }
                    }
                    if (ann_type && !is_global_simple_type(ann_type) &&
                        ann_type->kind == TYPE_KIND_UNARY && rhs_type) {
                        TypeUnary* unary = (TypeUnary*)ann_type;
                        // unwrap the TypeType wrapper on the operand
                        Type* expected_elem = unary->operand;
                        if (expected_elem && !is_global_simple_type(expected_elem) &&
                            expected_elem->type_id == LMD_TYPE_TYPE && expected_elem->kind == TYPE_KIND_SIMPLE) {
                            expected_elem = ((TypeType*)expected_elem)->type;
                        }
                        if (expected_elem && rhs_type->type_id == LMD_TYPE_ARRAY) {
                            TypeArray* arr_type = (TypeArray*)rhs_type;
                            if (arr_type->nested) {
                                // Preserve the optional element contract here. Comparing its
                                // compact TypeId alone made decimal?[]/datetime?[] look like
                                // `type[]`, rejecting the valid T[] -> T?[] covariance path.
                                TypeId expected_tid = expected_elem->type_id;
                                TypeId actual_tid = arr_type->nested->type_id;
                                if (static_boundary_relation(arr_type->nested, expected_elem) ==
                                        STATIC_BOUNDARY_REJECTED &&
                                    !types_compatible(arr_type->nested, expected_elem) &&
                                    !typed_array_element_compatible(arr_type->nested, expected_elem)) {
                                    int line = ts_node_start_point(asn_node).row + 1;
                                    record_type_error(tp, line,
                                        "array element type mismatch: expected %s, but got %s",
                                        get_type_name(expected_tid), get_type_name(actual_tid));
                                }
                            } else if (arr_type->length > 0) {
                                // Mixed literals may still be assignment-compatible through
                                // numeric widening; nonliteral arrays are checked by runtime coercion.
                                if (!typed_array_literal_elements_compatible(ast_node->as, expected_elem)) {
                                    int line = ts_node_start_point(asn_node).row + 1;
                                    record_type_error(tp, line,
                                        "cannot assign mixed-type array to %s[]: all elements must be %s",
                                        get_type_name(expected_elem->type_id),
                                        get_type_name(expected_elem->type_id));
                                }
                            }
                        }
                    }
                } else {
                    ast_node->type = set_type_any(tp, ANY_ERROR_RECOVERY);
                }
            }
        }
    }

    // push the name to the name stack (skip for type definitions - already pushed early
    // to support self-referencing types like `type Node = {left: Node}`)
    if (!is_type_definition) {
        push_name(tp, ast_node, NULL);
    }

    return (AstNode*)ast_node;
}

// Build decomposition expression: let a, b = expr OR let a, b at expr
AstNode* build_decompose_expr(Transpiler* tp, TSNode asn_node, bool is_named) {
    log_debug("build decompose expr, is_named=%d", is_named);

    // Count names first
    TSTreeCursor cursor = ts_tree_cursor_new(asn_node);
    bool has_child = ts_tree_cursor_goto_first_child(&cursor);
    int name_count = 0;
    while (has_child) {
        TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
        if (field_id == FIELD_NAME) {
            name_count++;
        }
        has_child = ts_tree_cursor_goto_next_sibling(&cursor);
    }
    ts_tree_cursor_delete(&cursor);

    if (name_count == 0) {
        log_error("decompose: no variable names found");
        return nullptr;
    }

    AstDecomposeNode* ast_node = (AstDecomposeNode*)alloc_ast_node(tp, AST_NODE_DECOMPOSE, asn_node, sizeof(AstDecomposeNode));
    ast_node->name_count = name_count;
    ast_node->is_named = is_named;
    ast_node->names = (String**)pool_calloc(tp->pool, sizeof(String*) * name_count);
    ast_node->entries = (NameEntry**)pool_calloc(tp->pool,
        sizeof(NameEntry*) * name_count);

    // Collect all names
    cursor = ts_tree_cursor_new(asn_node);
    has_child = ts_tree_cursor_goto_first_child(&cursor);
    int idx = 0;
    while (has_child) {
        TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
        if (field_id == FIELD_NAME) {
            TSNode name_node = ts_tree_cursor_current_node(&cursor);
            StrView name_view = node_name_text(tp, name_node);
            ast_node->names[idx] = name_pool_create_strview(tp->name_pool, name_view);
            log_debug("decompose name[%d]: %.*s", idx, (int)name_view.length, name_view.str);
            idx++;
        }
        has_child = ts_tree_cursor_goto_next_sibling(&cursor);
    }
    ts_tree_cursor_delete(&cursor);

    // Build the source expression
    TSNode val_node = ts_node_child_by_field_id(asn_node, FIELD_AS);
    if (ts_node_is_null(val_node)) {
        log_error("decompose: missing source expression");
        ast_node->as = nullptr;
        ast_node->type = set_type_any(tp, ANY_DECOMPOSE);
    } else {
        ast_node->as = build_expr(tp, val_node);
        ast_node->type = set_type_any(tp, ANY_DECOMPOSE);  // decomposition itself doesn't have a single type
    }

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
        AstNamedNode* var_node = (AstNamedNode*)alloc_ast_node(tp, AST_NODE_ASSIGN, asn_node, sizeof(AstNamedNode));
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

AstNode* build_let_expr(Transpiler* tp, TSNode let_node) {
    TSNode type_node = ts_node_child_by_field_id(let_node, FIELD_DECLARE);
    return build_assign_expr(tp, type_node, false);  // let expressions are not type definitions
}

// With the trimmed grammar a type annotation is ONE scanner token, so these
// questions are answered from the token's text rather than by walking CST type
// nodes (which no longer exist).

static StrView type_token_text(Transpiler* tp, TSNode node) {
    StrView src = ts_node_source(tp, node);
    while (src.length && (*src.str == ' ' || *src.str == '\t' || *src.str == '\n' || *src.str == '\r')) {
        src.str++;  src.length--;
    }
    return src;
}

// `type X = \(...)` / `type X = \symbol(...)` declares a pattern.
static bool type_assign_is_pattern(Transpiler* tp, TSNode type_assign) {
    TSNode as_node = ts_node_child_by_field_id(type_assign, FIELD_AS);
    if (ts_node_is_null(as_node)) return false;
    StrView text = type_token_text(tp, as_node);
    return text.length > 0 && text.str[0] == '\\';
}

static bool pattern_is_symbol_tag(Transpiler* tp, TSNode pattern_node) {
    if (ts_node_is_null(pattern_node)) return false;
    StrView text = type_token_text(tp, pattern_node);
    return text.length >= 8 && memcmp(text.str, "\\symbol(", 8) == 0;
}

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

AstNode* build_let_and_type_stam(Transpiler* tp, TSNode let_node, TSSymbol symbol) {
    // A delimiter is the syntax boundary; content inspection would misclassify
    // literal-only aliases and cannot carry the tagged symbol domain.
    bool is_string_pattern = false;
    if (symbol == SYM_TYPE_DEFINE) {
        TSNode first_declare = ts_node_child_by_field_id(let_node, FIELD_DECLARE);
        is_string_pattern = !ts_node_is_null(first_declare) && type_assign_is_pattern(tp, first_declare);
    }

    // For string/symbol pattern definitions, build pattern nodes
    if (is_string_pattern) {
        // Build from declare children (type_assign aliased as assign_expr)
        TSTreeCursor cursor = ts_tree_cursor_new(let_node);
        bool has_node = ts_tree_cursor_goto_first_child(&cursor);
        AstNode* first = NULL;
        AstNode* prev = NULL;
        while (has_node) {
            TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
            if (field_id == FIELD_DECLARE) {
                TSNode child = ts_tree_cursor_current_node(&cursor);
                TSNode as_node = ts_node_child_by_field_id(child, FIELD_AS);
                bool is_symbol = !ts_node_is_null(as_node) && pattern_is_symbol_tag(tp, as_node);
                // A literal-only string island is an ordinary literal union, not a
                // compiled pattern. The hand parser says which by handing back the
                // union AST instead of an island node, so build once and branch.
                AstNode* body = ts_node_is_null(as_node) ? NULL : build_expr(tp, as_node);
                AstNode* pattern = (body && body->node_type == AST_NODE_PATTERN_ISLAND) ?
                    build_string_pattern(tp, child, is_symbol, body) :
                    build_assign_expr(tp, child, true);
                if (pattern) {
                    if (prev) prev->next = pattern;
                    else first = pattern;
                    prev = pattern;
                }
            }
            has_node = ts_tree_cursor_goto_next_sibling(&cursor);
        }
        ts_tree_cursor_delete(&cursor);
        AstLetNode* type_stam = (AstLetNode*)alloc_ast_node(tp,
            AST_NODE_TYPE_STAM, let_node, sizeof(AstLetNode));
        type_stam->declare = first;
        type_stam->type = &LIT_NULL;
        return (AstNode*)type_stam;
    }

    // detect 'pub': let_stam uses choice('let','pub') so check first child symbol;
    // type_stam uses optional field('pub','pub') so check FIELD_PUB
    bool is_pub = false;
    if (symbol == SYM_LET_STAM) {
        TSNode first = ts_node_child(let_node, 0);
        is_pub = !ts_node_is_null(first) && ts_node_symbol(first) == anon_sym_pub;
    } else if (symbol == SYM_TYPE_DEFINE) {
        TSNode pub_node = ts_node_child_by_field_id(let_node, FIELD_PUB);
        is_pub = !ts_node_is_null(pub_node);
    }

    AstNodeType node_type = is_pub ? AST_NODE_PUB_STAM :
        symbol == SYM_TYPE_DEFINE ? AST_NODE_TYPE_STAM : AST_NODE_LET_STAM;
    AstLetNode* ast_node = (AstLetNode*)alloc_ast_node(tp,
        node_type, let_node, sizeof(AstLetNode));

    // determine if this is a type definition based on the parent symbol
    bool is_type_definition = (symbol == SYM_TYPE_DEFINE);

    // 'let' can have multiple name-value declarations
    TSTreeCursor cursor = ts_tree_cursor_new(let_node);
    bool has_node = ts_tree_cursor_goto_first_child(&cursor);
    AstNode* prev_declare = NULL;
    while (has_node) {
        // Check if the current node's field ID matches the target field ID
        TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
        if (field_id == FIELD_DECLARE) {
            TSNode child = ts_tree_cursor_current_node(&cursor);
            TSSymbol child_symbol = ts_node_symbol(child);

            AstNode* declare = NULL;
            if (child_symbol == SYM_OBJECT_TYPE) {
                // pub type Counter { ... } — object type definition wrapped in pub
                declare = build_object_type(tp, child);
                if (declare) {
                    ((AstObjectTypeNode*)declare)->is_public = true;
                }
            } else if (child_symbol == SYM_ASSIGN_EXPR) {
                // pub x = expr  OR  pub type T = type_expr (aliased as assign_expr)
                declare = build_assign_expr(tp, child, is_type_definition);
            } else {
                log_error("Error: build_let_and_type_stam expected SYM_ASSIGN_EXPR or SYM_OBJECT_TYPE but got symbol %d", child_symbol);
                // skip invalid node and continue - defensive recovery
                has_node = ts_tree_cursor_goto_next_sibling(&cursor);
                continue;
            }

            // additional defensive check
            if (!declare) {
                log_error("Error: build_let_and_type_stam failed to build declaration");
                has_node = ts_tree_cursor_goto_next_sibling(&cursor);
                continue;
            }
            if (prev_declare == NULL) {
                ast_node->declare = declare;
            }
            else {
                prev_declare->next = declare;
            }
            prev_declare = declare;
        }
        has_node = ts_tree_cursor_goto_next_sibling(&cursor);
    }
    ts_tree_cursor_delete(&cursor);

    // let statement does not have 'then' clause
    ast_node->type = &LIT_NULL;  // let stam returns null
    return (AstNode*)ast_node;
}

// ==================== Namespace Attribute Desugaring ====================
// Desugar ns.attr: val → ns: {attr: val} at AST build time (v2 namespace design)

// Find dotted_name node from attr_name or direct name node
// Returns the dotted_name TSNode, or a null TSNode if not found
static TSNode find_dotted_name_in_name(TSNode name_node) {
    TSSymbol sym = ts_node_symbol(name_node);
    if (sym == sym_dotted_name) return name_node;
    if (sym == sym_attr_name) {
        // attr_name wraps the actual name node
        TSNode child = ts_node_named_child(name_node, 0);
        if (!ts_node_is_null(child) && ts_node_symbol(child) == sym_dotted_name) {
            return child;
        }
    }
    TSNode null_node = {};
    return null_node;
}

// Build a synthetic map node wrapping a single key-value pair: {attr_name: val_expr}
// Used for desugaring ns.attr: val → ns: {attr: val}
static AstNode* build_ns_attr_map(Transpiler* tp, StrView attr_name, AstNode* val_expr, TSNode source_node) {
    AstMapNode* map_node = (AstMapNode*)alloc_ast_node(tp, AST_NODE_MAP, source_node, sizeof(AstMapNode));
    TypeMap* map_type = (TypeMap*)alloc_type(tp->pool, LMD_TYPE_MAP, sizeof(TypeMap));
    map_node->type = (Type*)map_type;

    // create key_expr: attr_name: val_expr
    AstNamedNode* key_node = (AstNamedNode*)alloc_ast_node(tp, AST_NODE_KEY_EXPR, source_node, sizeof(AstNamedNode));
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

StrView build_key_string(Transpiler* tp, TSNode key_node) {
    log_debug("build key string");
    TSSymbol symbol = ts_node_symbol(key_node);
    switch (symbol) {
    case sym_attr_name: {
        // attr_name wraps the actual name node (dotted_name, symbol, or identifier)
        TSNode name_child = ts_node_child_by_field_id(key_node, field_name);
        if (!ts_node_is_null(name_child)) {
            return build_key_string(tp, name_child);
        }
        // No name field means it's a simple name - get first named child
        TSNode first_child = ts_node_named_child(key_node, 0);
        if (!ts_node_is_null(first_child)) {
            return build_key_string(tp, first_child);
        }
        // fallback: try anonymous child (e.g., '*' key)
        TSNode anon_child = ts_node_child(key_node, 0);
        if (!ts_node_is_null(anon_child)) {
            return build_key_string(tp, anon_child);
        }
        return (StrView) { .str = NULL, .length = 0 };
    }
    case sym_dotted_name: {
        String* canonical = canonical_dotted_name(tp, key_node);
        return (StrView) { .str = canonical->chars, .length = canonical->len };
    }
    case SYM_SYMBOL:  case SYM_STRING: {
        // todo: handle string and symbol escape
        int start_byte = ts_node_start_byte(key_node) + 1; // skip the first quote
        int end_byte = ts_node_end_byte(key_node) - 1; // skip the last quote
        return (StrView) { .str = tp->source + start_byte, .length = static_cast<size_t>(end_byte - start_byte) };
    }
    case SYM_IDENT:
    case SYM_BASE_TYPE:
    case SYM_LAST_INDEX: {
        return (StrView)ts_node_source(tp, key_node);
    }
    case anon_sym_STAR: {
        // spread key: *:expr
        return (StrView) { .str = "*", .length = 1 };
    }
    default:
        log_debug("unknown key type %d", symbol);
        return (StrView) { .str = NULL, .length = 0 };
    }
}

static bool is_syntactic_spread_key(TSNode key_node) {
    if (ts_node_is_null(key_node)) return false;
    TSSymbol symbol = ts_node_symbol(key_node);
    if (symbol == anon_sym_STAR) return true;
    if (symbol != sym_attr_name) return false;

    TSNode child = ts_node_named_child(key_node, 0);
    if (!ts_node_is_null(child)) return is_syntactic_spread_key(child);
    child = ts_node_child(key_node, 0);
    return is_syntactic_spread_key(child);
}

AstNamedNode* build_key_expr(Transpiler* tp, TSNode pair_node) {
    log_debug("build_key_expr");
    AstNamedNode* ast_node = (AstNamedNode*)alloc_ast_node(tp, AST_NODE_KEY_EXPR, pair_node, sizeof(AstNamedNode));

    TSNode name = ts_node_child_by_field_id(pair_node, FIELD_NAME);
    if (ts_node_is_null(name)) {
        log_error("build_key_expr: missing name field");
        ast_node->name = name_pool_create_strview(tp->name_pool, (StrView){.str = "", .length = 0});
        ast_node->type = set_type_any(tp, ANY_ERROR_RECOVERY);
        ast_node->as = nullptr;
        return ast_node;
    }

    // Check for dotted_name to desugar: ns.attr: val → ns: {attr: val}
    TSNode dotted_node = find_dotted_name_in_name(name);
    if (!ts_node_is_null(dotted_node)) {
        // first named child is the namespace prefix, second is the local attr name
        TSNode ns_node = ts_node_named_child(dotted_node, 0);
        TSNode local_node = ts_node_named_child(dotted_node, 1);
        if (!ts_node_is_null(ns_node) && !ts_node_is_null(local_node)) {
            StrView ns_prefix = node_name_text(tp, ns_node);
            StrView local_name = node_name_text(tp, local_node);
            log_debug("ns attr desugar: %.*s.%.*s → %.*s: {%.*s: val}",
                (int)ns_prefix.length, ns_prefix.str,
                (int)local_name.length, local_name.str,
                (int)ns_prefix.length, ns_prefix.str,
                (int)local_name.length, local_name.str);

            // set key to ns prefix only
            ast_node->name = name_pool_create_strview(tp->name_pool, ns_prefix);

            // build value expression
            TSNode val_node = ts_node_child_by_field_id(pair_node, FIELD_AS);
            AstNode* val_expr = ts_node_is_null(val_node) ? nullptr : build_expr(tp, val_node);
            if (!val_expr) {
                log_error("build_key_expr: missing value for ns.attr");
                ast_node->type = set_type_any(tp, ANY_ERROR_RECOVERY);
                ast_node->as = nullptr;
                return ast_node;
            }

            // wrap in map: {local_name: val_expr}
            ast_node->as = build_ns_attr_map(tp, local_name, val_expr, pair_node);
            ast_node->type = ast_node->as->type;
            return ast_node;
        }
    }

    // Normal (non-namespaced) key handling
    StrView name_view = build_key_string(tp, name);
    ast_node->name = name_pool_create_strview(tp->name_pool, name_view);

    TSNode val_node = ts_node_child_by_field_id(pair_node, FIELD_AS);
    if (ts_node_is_null(val_node)) {
        log_error("build_key_expr: missing value field");
        ast_node->type = set_type_any(tp, ANY_ERROR_RECOVERY);
        ast_node->as = nullptr;
        return ast_node;
    }

    log_debug("build key as");
    ast_node->as = build_expr(tp, val_node);

    // determine the type of the field
    ast_node->type = ast_node->as->type;
    return ast_node;
}

// One source of truth for the base-type keywords. A table beats the former
// 30-branch if/else chain, and the hand parser (parse_type_pattern.cpp) needs
// the same mapping without a CST node to hang it on.
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

AstNode* build_base_type(Transpiler* tp, TSNode type_node) {
    log_debug("build type annotation");
    AstTypeNode* ast_node = (AstTypeNode*)alloc_ast_node(tp, AST_NODE_TYPE, type_node, sizeof(AstTypeNode));
    StrView type_name = ts_node_source(tp, type_node);
    ast_node->type = lookup_base_type_name(tp, type_name);
    if (!ast_node->type) {
        // report at the annotation site instead of leaking TYPE_ERROR into a
        // later E201 boundary message (root cause: name not defined syntax).
        record_unknown_base_type(tp, type_node, type_name);
        ast_node->type = (Type*)&LIT_TYPE_ERROR;
    }
    log_debug("built base type %.*s, type_id %d", (int)type_name.length, type_name.str,
        ((TypeType*)ast_node->type)->type->type_id);
    return (AstNode*)ast_node;
}

// ============================================================================
// Resolve base type for inheritance: returns the parent TypeObject*, or NULL
// Also copies parent fields into child shape entries (parent fields first).
// ============================================================================
static TypeObject* resolve_base_type(Transpiler* tp, TSNode base_node, TypeObject* obj_type,
    ShapeEntry** prev_entry_out, int* byte_offset_out) {
    if (ts_node_is_null(base_node)) return NULL;

    // resolve the base type identifier
    StrView base_name = ts_node_source(tp, base_node);
    NameEntry* base_entry = lookup_name(tp, base_name);
    if (!base_entry || !base_entry->node || !base_entry->node->type) {
        log_error("build_object_type: unknown base type '%.*s'", (int)base_name.length, base_name.str);
        return NULL;
    }

    // unwrap TypeType to get TypeObject
    Type* resolved = base_entry->node->type;
    if (resolved->type_id == LMD_TYPE_TYPE) {
        resolved = ((TypeType*)resolved)->type;
    }
    if (!resolved || resolved->type_id != LMD_TYPE_OBJECT) {
        log_error("build_object_type: base type '%.*s' is not an object type", (int)base_name.length, base_name.str);
        return NULL;
    }

    TypeObject* base_type = (TypeObject*)resolved;
    obj_type->base = base_type;
    log_debug("build_object_type: resolved base type '%.*s' with %lld fields",
        (int)base_name.length, base_name.str, base_type->length);

    // copy parent shape entries into child (parent fields come first)
    ShapeEntry* prev_entry = NULL;
    int byte_offset = 0;
    for (ShapeEntry* parent_se = base_type->shape; parent_se; parent_se = parent_se->next) {
        ShapeEntry* se = (ShapeEntry*)pool_calloc(tp->pool, sizeof(ShapeEntry));
        se->name = parent_se->name;
        se->type = parent_se->type;
        se->byte_offset = byte_offset;
        // The child owns a fresh layout entry but inherits the parent default;
        // dropping this AST makes `<Child>` leave inherited fields uninitialized.
        se->default_value = parent_se->default_value;
        se->next = NULL;
        if (!prev_entry) { obj_type->shape = se; }
        else { prev_entry->next = se; }
        prev_entry = se;
        obj_type->length++;
        byte_offset += sizeof(void*);
    }

    *prev_entry_out = prev_entry;
    *byte_offset_out = byte_offset;
    return base_type;
}

// ============================================================================
// Push parent fields into scope so child methods can reference them (implicit this)
// ============================================================================
static void push_inherited_fields_to_scope(Transpiler* tp, TypeObject* base_type) {
    if (!base_type) return;
    FOR_EACH_MAP_FIELD(base_type, se) {
        if (!se->name) continue;
        // create a lightweight AstNamedNode to push field into scope
        AstNamedNode* field_ref = (AstNamedNode*)pool_calloc(tp->pool, sizeof(AstNamedNode));
        field_ref->node_type = AST_NODE_KEY_EXPR;
        field_ref->type = se->type;
        field_ref->name = name_pool_create_len(tp->name_pool, se->name->str, se->name->length);
        push_name(tp, field_ref, NULL);
    }
}

// Build object methods while the object's fields are visible as implicit names.
static void build_object_type_methods(Transpiler* tp, AstObjectTypeNode* ast_node,
    TypeObject* obj_type, TypeObject* base_type_obj, TSNode type_node,
    ShapeEntry* prev_entry, int byte_offset) {
    NameScope* obj_scope = (NameScope*)pool_calloc(tp->pool, sizeof(NameScope));
    obj_scope->parent = tp->current_scope;
    tp->current_scope = obj_scope;

    push_inherited_fields_to_scope(tp, base_type_obj);
    AstNode* field_node = ast_node->item;
    while (field_node) {
        if (field_node->node_type == AST_NODE_KEY_EXPR) {
            AstNamedNode* fn = (AstNamedNode*)field_node;
            Type* orig_type = fn->type;
            if (orig_type && orig_type->type_id == LMD_TYPE_TYPE) {
                fn->type = ((TypeType*)orig_type)->type;
            }
            push_name(tp, fn, NULL);
        }
        field_node = field_node->next;
    }

    AstNode* prev_method = NULL;
    TSNode child = ts_node_named_child(type_node, 0);
    while (!ts_node_is_null(child)) {
        TSSymbol symbol = ts_node_symbol(child);
        if (symbol == SYM_FUNC_STAM || symbol == SYM_FUNC_EXPR_STAM) {
            AstNode* method = build_func(tp, child, true, false);
            if (method) {
                if (!prev_method) { ast_node->methods = method; }
                else { prev_method->next = method; }
                prev_method = method;

                AstFuncNode* fn_method = (AstFuncNode*)method;
                TypeMethod* tm = (TypeMethod*)pool_calloc(tp->pool, sizeof(TypeMethod));
                StrView* method_name_view = (StrView*)pool_calloc(tp->pool, sizeof(StrView));
                method_name_view->str = fn_method->name->chars;
                method_name_view->length = fn_method->name->len;
                tm->name = method_name_view;
                tm->compiled_fn = NULL;
                // retain the source signature; wrappers cannot recover optional or variadic semantics.
                tm->fn_type = (struct TypeFunc*)fn_method->type;
                tm->ast_def = fn_method;
                tm->ast_module = tp->script_owner;
                tm->is_proc = (method->node_type == AST_NODE_PROC);
                tm->next = NULL;
                if (!obj_type->methods) { obj_type->methods = tm; }
                else { obj_type->methods_last->next = tm; }
                obj_type->methods_last = tm;
                obj_type->method_count++;
            }
        }
        child = ts_node_next_named_sibling(child);
    }

    obj_type->byte_size = byte_offset;
    obj_type->last = prev_entry;
    tp->current_scope = obj_scope->parent;
}

// ============================================================================
// Object type definition: type Point { x: float, y: float; fn magnitude() => ... }
// ============================================================================
AstNode* build_content_type(Transpiler* tp, TSNode list_node); // forward declaration

AstNode* build_object_type(Transpiler* tp, TSNode type_node) {
    log_debug("build_object_type");
    AstObjectTypeNode* ast_node = (AstObjectTypeNode*)alloc_ast_node(tp,
        AST_NODE_OBJECT_TYPE, type_node, sizeof(AstObjectTypeNode));
    // detect 'pub' field on object_type
    TSNode pub_node = ts_node_child_by_field_id(type_node, FIELD_PUB);
    ast_node->is_public = !ts_node_is_null(pub_node);
    ast_node->local_type_index = -1;

    // allocate TypeObject (extends TypeMap) for this object type definition
    TypeObject* obj_type = (TypeObject*)pool_calloc(tp->pool, sizeof(TypeObject));
    obj_type->type_id = LMD_TYPE_OBJECT;
    obj_type->kind = 0;
    obj_type->base = NULL;
    obj_type->methods = NULL;
    obj_type->methods_last = NULL;
    obj_type->method_count = 0;
    obj_type->constraint = NULL;
    obj_type->constraint_fn = NULL;

    // wrap in TypeType so it behaves as a type value
    TypeType* tt = (TypeType*)alloc_type(tp->pool, LMD_TYPE_TYPE, sizeof(TypeType));
    tt->type = (Type*)obj_type;
    ast_node->type = (Type*)tt;

    // get type name
    TSNode name_node = ts_node_child_by_field_id(type_node, FIELD_NAME);
    StrView name = node_name_text(tp, name_node);
    ast_node->name = name_pool_create_strview(tp->name_pool, name);
    obj_type->type_name.str = ast_node->name->chars;
    obj_type->type_name.length = ast_node->name->len;
    // set struct_name for direct field access optimization (Phase 5/6)
    obj_type->struct_name = ast_node->name->chars;
    obj_type->is_trusted_contract = true;
    log_debug("build_object_type: name='%.*s'", (int)name.length, name.str);

    // get optional base type (inheritance)
    TSNode base_node = ts_node_child_by_field_id(type_node, FIELD_BASE);
    if (!ts_node_is_null(base_node)) {
        ast_node->base_type = build_expr(tp, base_node);
        log_debug("build_object_type: has base type");
    } else {
        ast_node->base_type = NULL;
    }

    // iterate children to find fields (attr), methods (fn_stam/fn_expr_stam), and constraints
    // Two-pass approach: first collect fields, then build methods with fields in scope
    TSNode child = ts_node_named_child(type_node, 0);
    AstNode* prev_field = NULL;  ShapeEntry* prev_entry = NULL;  int byte_offset = 0;
    AstNode* prev_method = NULL;

    // Resolve inheritance: copy parent fields into child shape (parent fields first)
    TypeObject* base_type_obj = resolve_base_type(tp, base_node, obj_type, &prev_entry, &byte_offset);
    AstNode* prev_constraint = NULL;
    ast_node->methods = NULL;
    ast_node->constraints = NULL;
    ast_node->content = NULL;

    // Pass 1: build fields, content schema, and constraints (no methods yet)
    while (!ts_node_is_null(child)) {
        TSSymbol symbol = ts_node_symbol(child);

        if (symbol == SYM_COMMENT) {
            // skip
        }
        else if (symbol == SYM_ATTR) {
            // field declaration: name: type_expr [= default]
            AstNode* item = (AstNode*)build_key_expr(tp, child);
            if (item) {
                if (!prev_field) { ast_node->item = item; }
                else { prev_field->next = item; }
                prev_field = item;

                // build shape entry for this field
                ShapeEntry* shape_entry = (ShapeEntry*)pool_calloc(tp->pool, sizeof(ShapeEntry));
                String* pooled_name = ((AstNamedNode*)item)->name;
                StrView* name_view = (StrView*)pool_calloc(tp->pool, sizeof(StrView));
                name_view->str = pooled_name->chars;
                name_view->length = pooled_name->len;
                shape_entry->name = name_view;
                // unwrap TypeType to get the actual field data type
                Type* field_type = item->type;
                if (field_type && field_type->type_id == LMD_TYPE_TYPE) {
                    field_type = ((TypeType*)field_type)->type;
                }
                shape_entry->type = field_type;
                shape_entry->byte_offset = byte_offset;
                // read optional default value expression
                TSNode default_node = ts_node_child_by_field_id(child, FIELD_DEFAULT);
                if (!ts_node_is_null(default_node)) {
                    shape_entry->default_value = build_expr(tp, default_node);
                    log_debug("build_object_type: field '%.*s' has default value",
                        (int)name_view->length, name_view->str);
                }
                if (!prev_entry) { obj_type->shape = shape_entry; }
                else { prev_entry->next = shape_entry; }
                prev_entry = shape_entry;

                obj_type->length++;
                byte_offset += sizeof(void*);  // all fields stored as Item (8 bytes)
            }
        }
        else if (symbol == SYM_THAT_CONSTRAINT) {
            // object-level constraint: that (expr) - enable implicit ~.name resolution
            TSNode constraint_expr = ts_node_child_by_field_id(child, FIELD_CONSTRAINT);
            if (!ts_node_is_null(constraint_expr)) {
                bool old_in_that = tp->in_that_clause;
                tp->in_that_clause = true;
                AstNode* constraint = build_expr(tp, constraint_expr);
                tp->in_that_clause = old_in_that;
                if (constraint) {
                    if (!prev_constraint) { ast_node->constraints = constraint; }
                    else { prev_constraint->next = constraint; }
                    prev_constraint = constraint;
                    // store last constraint on the type (for single constraint)
                    obj_type->constraint = constraint;
                }
            }
        }
        else if (symbol == SYM_CONTENT_TYPE) {
            // content schema: when present, type acts as element type
            ast_node->content = build_content_type(tp, child);
            log_debug("build_object_type: has content schema");
        }

        child = ts_node_next_named_sibling(child);
    }

    build_object_type_methods(tp, ast_node, obj_type, base_type_obj, type_node, prev_entry, byte_offset);

    // register the type in the type list (store TypeType wrapper for consistency with other types)
    arraylist_append(tp->type_list, tt);
    obj_type->type_index = tp->type_list->length - 1;

    // register the type name in the current scope so it can be resolved
    push_name(tp, (AstNamedNode*)ast_node, NULL);

    log_debug("build_object_type: '%.*s' with %d fields, %d methods",
        (int)name.length, name.str, obj_type->length, obj_type->method_count);
    return (AstNode*)ast_node;
}

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

AstNode* build_content_type(Transpiler* tp, TSNode list_node) {
    log_debug("build content type");
    AstListNode* ast_node = (AstListNode*)alloc_ast_node(tp, AST_NODE_CONTENT_TYPE, list_node, sizeof(AstListNode));
    TypeList* type = (TypeList*)alloc_type(tp->pool, LMD_TYPE_ARRAY, sizeof(TypeList));
    ast_node->type = ast_node->list_type = type;

    TSNode child = ts_node_named_child(list_node, 0);
    AstNode* prev_item = NULL;
    while (!ts_node_is_null(child)) {
        AstNode* item = build_expr(tp, child);
        if (item) {
            if (!prev_item) ast_node->item = item;
            else prev_item->next = item;
            prev_item = item;
            type->length++;
        }
        // else comment or error
        child = ts_node_next_named_sibling(child);
    }
    log_debug("end building content type: %ld", type->length);
    return ast_node;
}

// build range type: start to end (e.g. 1 to 10, 'a' to 'z')
// constructs as a binary node with OPERATOR_TO and type LMD_TYPE_RANGE
// build constrained type: base_type where (constraint)
// e.g. int where (5 < ~ < 10), string where (len(~) > 0)
AstNode* build_constrained_type(Transpiler* tp, TSNode type_node) {
    log_debug("build constrained type");
    AstConstrainedTypeNode* ast_node = (AstConstrainedTypeNode*)alloc_ast_node(tp,
        AST_NODE_CONSTRAINED_TYPE, type_node, sizeof(AstConstrainedTypeNode));

    // Build base type
    TSNode base_node = ts_node_child_by_field_id(type_node, FIELD_BASE);
    if (!ts_node_is_null(base_node)) {
        ast_node->base = build_expr(tp, base_node);
    }

    // Build constraint expression (inside 'that' clause: enable implicit ~.name resolution)
    TSNode constraint_node = ts_node_child_by_field_id(type_node, FIELD_CONSTRAINT);
    if (!ts_node_is_null(constraint_node)) {
        bool old_in_that = tp->in_that_clause;
        tp->in_that_clause = true;
        ast_node->constraint = build_expr(tp, constraint_node);
        tp->in_that_clause = old_in_that;
    }

    // Create TypeConstrained directly (not wrapped in TypeType)
    // TypeConstrained inherits from Type with type_id = LMD_TYPE_TYPE and kind = TYPE_KIND_CONSTRAINED
    TypeConstrained* constrained = (TypeConstrained*)alloc_type_kind(tp->pool,
        TYPE_KIND_CONSTRAINED, sizeof(TypeConstrained));

    // Get the inner type from the base (unwrap if TypeType)
    if (ast_node->base && ast_node->base->type) {
        if (ast_node->base->type->type_id == LMD_TYPE_TYPE) {
            TypeType* base_type_type = (TypeType*)ast_node->base->type;
            constrained->base = base_type_type->type;
        } else {
            constrained->base = ast_node->base->type;
        }
    } else {
        constrained->base = set_type_any(tp, ANY_EXPLICIT);
    }
    constrained->constraint = ast_node->constraint;

    // Set ast_node->type to the TypeConstrained directly (no TypeType wrapper)
    ast_node->type = (Type*)constrained;

    // Add to type_list for runtime const_type() access
    arraylist_append(tp->type_list, ast_node->type);
    constrained->type_index = tp->type_list->length - 1;

    return (AstNode*)ast_node;
}

AstBinaryNode* build_registered_binary_type(Transpiler* tp, TSNode node,
        AstNode* left, AstNode* right, Type* left_type, Type* right_type,
        Operator op, StrView op_str) {
    AstBinaryNode* binary = (AstBinaryNode*)alloc_ast_node(tp,
        AST_NODE_BINARY_TYPE, node, sizeof(AstBinaryNode));
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

AstNode* build_function_return_contract_node(Transpiler* tp, TSNode node,
        Type* returned, Type* error_type, bool can_raise) {
    // Both grammar paths must carry exactly this compact TypeFunc contract;
    // build_func reads it before replacing the wrapper with the declared fn.
    AstFuncNode* wrapper_node = (AstFuncNode*)alloc_ast_node(tp, AST_NODE_FUNC_TYPE,
        node, sizeof(AstFuncNode));
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

static ShapeEntry* build_map_shape_entry(Transpiler* tp, AstNode* item,
                                         bool is_spread, bool normalize_type) {
    ShapeEntry* shape_entry = (ShapeEntry*)pool_calloc(tp->pool, sizeof(ShapeEntry));
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

AstNode* build_map(Transpiler* tp, TSNode map_node) {
    AstMapNode* ast_node = (AstMapNode*)alloc_ast_node(tp, AST_NODE_MAP, map_node, sizeof(AstMapNode));
    ast_node->type = alloc_type(tp->pool, LMD_TYPE_MAP, sizeof(TypeMap));
    TypeMap* type = (TypeMap*)ast_node->type;

    TSNode child = ts_node_named_child(map_node, 0);
    AstNode* prev_item = NULL;  ShapeEntry* prev_entry = NULL;  int byte_offset = 0;
    while (!ts_node_is_null(child)) {
        TSSymbol symbol = ts_node_symbol(child);
        if (symbol == SYM_COMMENT) {  // skip comments
            child = ts_node_next_named_sibling(child);  continue;
        }
        // named map item; *:expr is a map_item with key name "*" (spread)
        AstNode* item;
        bool is_spread = false;
        if (symbol == SYM_MAP_ITEM) {
            AstNamedNode* key_expr = build_key_expr(tp, child);
            TSNode key_node = ts_node_child_by_field_id(child, FIELD_NAME);
            // A quoted `'*'` is an ordinary map key; only the grammar's
            // anonymous `*` token denotes a spread. Confusing the two leaves
            // an array value in a nameless shape slot and crashes later map
            // construction (S2.3.1, D2.1.1).
            if (is_syntactic_spread_key(key_node)) {
                // spread: *:expr — extract the value expression
                item = key_expr->as;
                is_spread = true;
            } else {
                item = (AstNode*)key_expr;
            }
        } else {
            item = build_expr(tp, child);
        }
        if (!item) { log_error("build_map: null expr item");  break; }

        if (!prev_item) { ast_node->item = item; }
        else { prev_item->next = item; }
        prev_item = item;

        ShapeEntry* shape_entry = build_map_shape_entry(tp, item, is_spread, true);
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
        child = ts_node_next_named_sibling(child);
    }
    type->byte_size = byte_offset;

    arraylist_append(tp->type_list, type);
    type->type_index = tp->type_list->length - 1;
    return (AstNode*)ast_node;
}

AstNode* build_elmt(Transpiler* tp, TSNode elmt_node) {
    log_debug("build element expr");

    // first pass: extract the tag name to check if it's an object type
    TSNode first_child = ts_node_named_child(elmt_node, 0);
    TSSymbol first_sym = ts_node_symbol(first_child);
    StrView tag_name = {};
    String* dotted_tag_name = NULL;
    if (first_sym == SYM_IDENT) {
        tag_name = ts_node_source(tp, first_child);
    } else if (first_sym == SYM_SYMBOL) {
        tag_name = ts_node_source(tp, first_child);
        if (tag_name.length >= 2) { tag_name.str++; tag_name.length -= 2; }
    } else if (first_sym == sym_dotted_name) {
        dotted_tag_name = canonical_dotted_name(tp, first_child);
        tag_name = { .str = dotted_tag_name->chars, .length = dotted_tag_name->len };
    }

    // check if tag name resolves to an object type definition
    if (tag_name.length > 0) {
        NameEntry* entry = lookup_name(tp, tag_name);
        if (entry && entry->node && entry->node->type) {
            Type* resolved = entry->node->type;
            if (resolved->type_id == LMD_TYPE_TYPE) {
                Type* inner = ((TypeType*)resolved)->type;
                if (inner && inner->type_id == LMD_TYPE_OBJECT) {
                    // build as object literal instead of element
                    log_debug("build_elmt: detected <%.*s> as object type, building object literal", (int)tag_name.length, tag_name.str);
                    TypeObject* obj_type = (TypeObject*)inner;
                    AstObjectLiteralNode* obj_node = (AstObjectLiteralNode*)alloc_ast_node(tp,
                        AST_NODE_OBJECT_LITERAL, elmt_node, sizeof(AstObjectLiteralNode));
                    obj_node->type_name = name_pool_create_strview(tp->name_pool, tag_name);
                    obj_node->type = (Type*)obj_type;
                    obj_node->item = NULL;

                    // parse children: attrs and spread become items
                    AstNode* prev_item = NULL;
                    TSNode child = ts_node_named_child(elmt_node, 0);
                    int name_start = ts_node_start_byte(first_child);
                    while (!ts_node_is_null(child)) {
                        TSSymbol symbol = ts_node_symbol(child);
                        int child_start = (int)ts_node_start_byte(child);
                        if (symbol == SYM_COMMENT || child_start == name_start) {
                            child = ts_node_next_named_sibling(child);
                            continue;
                        }
                        AstNode* item;
                        if (symbol == SYM_ATTR) {
                            AstNamedNode* key_expr = build_key_expr(tp, child);
                            if (key_expr->name && key_expr->name->len == 1 && key_expr->name->chars[0] == '*') {
                                // spread: *:expr — extract value as source
                                AstNode* source_inner = key_expr->as;
                                if (source_inner) {
                                    if (source_inner->node_type == AST_NODE_IDENT || source_inner->node_type == AST_NODE_CURRENT_ITEM) {
                                        AstPrimaryNode* wrapper = (AstPrimaryNode*)alloc_ast_node(tp,
                                            AST_NODE_PRIMARY, child, sizeof(AstPrimaryNode));
                                        wrapper->expr = source_inner;
                                        wrapper->type = source_inner->type;
                                        item = (AstNode*)wrapper;
                                    } else {
                                        item = source_inner;
                                    }
                                } else {
                                    item = NULL;
                                }
                            } else {
                                item = (AstNode*)key_expr;
                            }
                        } else {
                            child = ts_node_next_named_sibling(child);
                            continue;
                        }
                        if (item) {
                            if (!prev_item) { obj_node->item = item; }
                            else { prev_item->next = item; }
                            prev_item = item;
                        }
                        child = ts_node_next_named_sibling(child);
                    }
                    return (AstNode*)obj_node;
                }
            }
        }
    }

    // not an object type — build as normal element
    AstElementNode* ast_node = (AstElementNode*)alloc_ast_node(tp,
        AST_NODE_ELEMENT, elmt_node, sizeof(AstElementNode));
    TypeElmt* type = (TypeElmt*)alloc_type(tp->pool, LMD_TYPE_ELEMENT, sizeof(TypeElmt));
    ast_node->type = (Type*)type;

    TSNode child = ts_node_named_child(elmt_node, 0);
    AstNode* prev_item = NULL;  ShapeEntry* prev_entry = NULL;  int byte_offset = 0;
    while (!ts_node_is_null(child)) {
        TSSymbol symbol = ts_node_symbol(child);
        if (symbol == SYM_COMMENT) {} // skip comments
        else if (symbol == SYM_IDENT) {  // element name
            StrView name = ts_node_source(tp, child);
            String* pooled_name = name_pool_create_strview(tp->name_pool, name);
            // Convert pooled String* to StrView for TypeElmt
            type->name.str = pooled_name->chars;
            type->name.length = pooled_name->len;
        }
        else if (symbol == sym_dotted_name) {  // dotted element name (a.b.'c')
            String* pooled_name = dotted_tag_name
                ? dotted_tag_name : canonical_dotted_name(tp, child);
            type->name.str = pooled_name->chars;
            type->name.length = pooled_name->len;
            // look up namespace prefix (first segment) and set TypeElmt.ns
            TSNode ns_node = ts_node_named_child(child, 0);
            if (!ts_node_is_null(ns_node)) {
                StrView ns_prefix = node_name_text(tp, ns_node);
                NamespaceEntry* ns_entry = lookup_namespace_strview(tp, ns_prefix);
                if (ns_entry) {
                    type->ns = ns_entry->target;
                    log_debug("element ns resolved: %.*s → target %p", (int)ns_prefix.length, ns_prefix.str, ns_entry->target);
                }
            }
        }
        else if (symbol == SYM_SYMBOL) {  // element name as symbol 'name'
            int start_byte = ts_node_start_byte(child) + 1; // skip leading quote
            int end_byte = ts_node_end_byte(child) - 1; // skip trailing quote
            StrView name = { .str = tp->source + start_byte, .length = static_cast<size_t>(end_byte - start_byte) };
            String* pooled_name = name_pool_create_strview(tp->name_pool, name);
            type->name.str = pooled_name->chars;
            type->name.length = pooled_name->len;
        }
        else if (symbol == SYM_CONTENT) {  // element content
            ast_node->content = build_content(tp, child, false, false);
        }
        else {  // attrs (including spread *:expr)
            AstNode* item;
            bool is_spread = false;
            if (symbol == SYM_ATTR) {
                AstNamedNode* key_expr = build_key_expr(tp, child);
                TSNode key_node = ts_node_child_by_field_id(child, FIELD_NAME);
                if (is_syntactic_spread_key(key_node)) {
                    // spread: *:expr — extract value expression
                    item = key_expr->as;
                    is_spread = true;
                } else {
                    item = (AstNode*)key_expr;
                }
            } else {
                child = ts_node_next_named_sibling(child);
                continue;
            }
            if (!item) { child = ts_node_next_named_sibling(child); continue; }

            // check for ns attr merging: if key already exists and both are maps, merge
            if (item->node_type == AST_NODE_KEY_EXPR) {
                AstNamedNode* new_key = (AstNamedNode*)item;
                AstNamedNode* existing = find_existing_named_item(ast_node->item, new_key->name);
                if (existing && existing->as && new_key->as &&
                    existing->as->type->type_id == LMD_TYPE_MAP && new_key->as->type->type_id == LMD_TYPE_MAP) {
                    merge_ns_attr_maps(tp, existing->as, new_key->as);
                    existing->type = existing->as->type;
                    log_debug("merged ns attr map for key '%.*s'", (int)new_key->name->len, new_key->name->chars);
                    child = ts_node_next_named_sibling(child);
                    continue;  // skip adding new entry
                }
            }

            if (!prev_item) { ast_node->item = item; }
            else { prev_item->next = item; }
            prev_item = item;

            ShapeEntry* shape_entry = build_map_shape_entry(tp, item, is_spread, false);
            shape_entry->byte_offset = byte_offset;
            if (!prev_entry) { type->shape = shape_entry; }
            else { prev_entry->next = shape_entry; }
            prev_entry = shape_entry;

            type->length++;
            byte_offset += (!is_spread) ?
                type_info[type_field_storage_type_id(item->type)].byte_size : sizeof(void*);
        }
        child = ts_node_next_named_sibling(child);
    }

    arraylist_append(tp->type_list, type);
    type->type_index = tp->type_list->length - 1;
    type->byte_size = byte_offset;
    type->content_length = ast_node->content ?
        (ast_node->content->node_type == AST_NODE_CONTENT ? ((AstListNode*)ast_node->content)->list_type->length : 1) : 0;
    return (AstNode*)ast_node;
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
    AstJoinKey* spec = (AstJoinKey*)alloc_ast_node(tp, AST_NODE_JOIN_KEY, loop->node, sizeof(AstJoinKey));
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

AstNode* build_loop_expr(Transpiler* tp, TSNode loop_node) {
    log_debug("build loop expr");
    AstLoopNode* ast_node = (AstLoopNode*)alloc_ast_node(tp, AST_NODE_LOOP, loop_node, sizeof(AstLoopNode));
    ast_node->index_name = NULL;  // default: no index variable
    ast_node->key_filter = LOOP_KEY_ALL;  // default: iterate all entries
    ast_node->key_only = false;
    ast_node->optional = false;

    TSNode op_node = ts_node_child_by_field_id(loop_node, FIELD_OP);
    if (!ts_node_is_null(op_node)) {
        StrView op_view = ts_node_source(tp, op_node);
        if (strview_equal(&op_view, "at")) {
            // `for k at item` is key-only iteration; values remain available via `for k, v in item`.
            ast_node->key_filter = LOOP_KEY_SYMBOL;
            ast_node->key_only = true;
        }
    }

    // Check for optional index variable (first identifier in 'for k, v in expr')
    TSNode index_node = ts_node_child_by_field_id(loop_node, FIELD_INDEX);
    if (!ts_node_is_null(index_node)) {
        int start_byte = ts_node_start_byte(index_node);
        StrView index_view = { .str = tp->source + start_byte, .length = ts_node_end_byte(index_node) - start_byte };
        ast_node->index_name = name_pool_create_strview(tp->name_pool, index_view);
        log_debug("loop has index variable: %.*s", (int)index_view.length, index_view.str);

        // Check for optional type annotation on index variable (k:int or k:symbol)
        TSNode index_type_node = ts_node_child_by_field_id(loop_node, FIELD_INDEX_TYPE);
        if (!ts_node_is_null(index_type_node)) {
            StrView type_view = ts_node_source(tp, index_type_node);
            if (strview_equal(&type_view, "int")) {
                ast_node->key_filter = LOOP_KEY_INT;
                log_debug("loop key filter: int (indexed only)");
            } else if (strview_equal(&type_view, "symbol")) {
                ast_node->key_filter = LOOP_KEY_SYMBOL;
                log_debug("loop key filter: symbol (keyed only)");
            } else {
                log_error("Error: unsupported loop key type '%.*s', expected 'int' or 'symbol'",
                    (int)type_view.length, type_view.str);
            }
        }
    }

    // Get the main variable name
    TSNode name = ts_node_child_by_field_id(loop_node, FIELD_NAME);
    int start_byte = ts_node_start_byte(name);
    StrView name_view = { .str = tp->source + start_byte, .length = ts_node_end_byte(name) - start_byte };
    ast_node->name = name_pool_create_strview(tp->name_pool, name_view);

    TSNode optional_node = ts_node_child_by_field_id(loop_node, FIELD_OPTIONAL);
    ast_node->optional = !ts_node_is_null(optional_node);

    TSNode expr_node = ts_node_child_by_field_id(loop_node, FIELD_AS);
    ast_node->as = build_expr(tp, expr_node);

    // determine the type of the loop variable
    Type* expr_type = ast_node->as->type;
    if (ast_node->key_only) {
        ast_node->type = set_type_any(tp, ANY_LOOP_SRC);
    }
    else if (expr_type->type_id == LMD_TYPE_ARRAY ||
            expr_type->type_id == LMD_TYPE_ARRAY_NUM) {
        // RETRY (§15.6): ARRAY_NUM was excluded while the emit_double_bits
        // float bug was live, so iterating a typed numeric array produced
        // `any` while the plain-array form produced its element type.
        TypeArray* array_type = !is_global_simple_type(expr_type)
            ? (TypeArray*)expr_type : NULL;
        if (array_type && array_type->nested && (uintptr_t)array_type->nested > 0x1000) {
            ast_node->type = array_type->nested;
        }
        else {
            log_debug("Warning: Invalid nested type in array during loop AST building, using TYPE_ANY");
            ast_node->type = set_type_any(tp, ANY_LOOP_SRC);
        }
    }
    else if (expr_type->type_id == LMD_TYPE_RANGE) {
        ast_node->type = &TYPE_INT;
    }
    else if (expr_type->type_id == LMD_TYPE_BINARY) {
        // Iteration exposes the same sized byte scalar as direct indexing.
        ast_node->type = &TYPE_U8;
    }
    else {
        // for maps/elements/any: value type is any
        ast_node->type = set_type_any(tp, ANY_LOOP_SRC);
    }

    // push the index name to the name stack if present
    if (ast_node->index_name) {
        AstNamedNode* index_entry = (AstNamedNode*)alloc_ast_node(tp, AST_NODE_LOOP, loop_node, sizeof(AstNamedNode));
        index_entry->name = ast_node->index_name;
        // key type depends on filter and container type:
        // LOOP_KEY_INT -> always int; LOOP_KEY_SYMBOL -> always symbol/string
        // LOOP_KEY_ALL -> could be either, use ANY
        if (ast_node->key_filter == LOOP_KEY_INT) {
            index_entry->type = &TYPE_INT;
        } else if (ast_node->key_filter == LOOP_KEY_SYMBOL) {
            // A symbol-filtered key is a symbol at runtime; saying so costs
            // nothing (symbols are boxed on every path) [TIG10].
            index_entry->type = &TYPE_SYMBOL;
        } else {
            // LOOP_KEY_ALL yields int OR symbol. The union is the honest type,
            // but it has no magnitude, so `k < n` on an unfiltered key becomes
            // a static E312 — 113 corpus scripts compare an index that is an
            // int at runtime. It stays open until TI5 narrowing can separate
            // the two per branch.
            index_entry->type = set_type_any(tp, ANY_LOOP_SRC);
        }
        push_name(tp, index_entry, NULL);
    }

    // push the main name to the name stack
    push_name(tp, (AstNamedNode*)ast_node, NULL);

    TSNode on_node = ts_node_child_by_field_id(loop_node, FIELD_ON);
    if (!ts_node_is_null(on_node)) {
        ast_node->on = build_expr(tp, on_node);
        if (ast_node->on) {
            build_join_key_specs(tp, ast_node, ast_node->on);
        }
    } else if (ast_node->optional) {
        log_error("Error: optional join marker '?' requires an 'on' condition");
    }
    return (AstNode*)ast_node;
}

// Helper: build order_spec node
AstNode* build_order_spec(Transpiler* tp, TSNode spec_node) {
    log_debug("build order spec");
    AstOrderSpec* ast_node = (AstOrderSpec*)alloc_ast_node(tp, AST_NODE_ORDER_SPEC, spec_node, sizeof(AstOrderSpec));

    TSNode expr_node = ts_node_child_by_field_id(spec_node, FIELD_EXPR);
    ast_node->expr = build_expr(tp, expr_node);

    // Check for direction (asc/desc)
    TSNode dir_node = ts_node_child_by_field_id(spec_node, FIELD_DIR);
    if (!ts_node_is_null(dir_node)) {
        StrView dir_text = ts_node_source(tp, dir_node);
        ast_node->descending = (strview_equal(&dir_text, "desc") || strview_equal(&dir_text, "descending"));
    } else {
        ast_node->descending = false;  // default ascending
    }

    ast_node->type = set_type_any(tp, ANY_STATEMENT);
    return (AstNode*)ast_node;
}

// Helper: build for_let_clause node (reuses AstNamedNode)
AstNode* build_for_let_clause(Transpiler* tp, TSNode let_node) {
    log_debug("build for let clause");
    AstNamedNode* ast_node = (AstNamedNode*)alloc_ast_node(tp, AST_NODE_ASSIGN, let_node, sizeof(AstNamedNode));

    TSNode name_node = ts_node_child_by_field_id(let_node, FIELD_NAME);
    if (ts_node_is_null(name_node)) {
        log_error("for_let_clause: name_node is null");
        return (AstNode*)ast_node;
    }
    StrView name_view = ts_node_source(tp, name_node);
    ast_node->name = name_pool_create_strview(tp->name_pool, name_view);
    log_debug("for_let_clause: name = %.*s", (int)name_view.length, name_view.str);

    TSNode value_node = ts_node_child_by_field_id(let_node, FIELD_VALUE);
    if (ts_node_is_null(value_node)) {
        log_error("for_let_clause: value_node is null");
        ast_node->as = NULL;
        ast_node->type = set_type_any(tp, ANY_ERROR_RECOVERY);
    } else {
        const char* value_type = ts_node_type(value_node);
        log_debug("for_let_clause: value_node type = %s", value_type);
        log_debug("for_let_clause: building value expression");
        ast_node->as = build_expr(tp, value_node);
        if (ast_node->as == NULL) {
            log_error("for_let_clause: build_expr returned NULL for type %s", value_type);
        }
        ast_node->type = ast_node->as ? ast_node->as->type : &TYPE_ANY;
    }

    // Register in current scope
    push_name(tp, ast_node, NULL);

    return (AstNode*)ast_node;
}

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

static bool group_alias_exists(AstGroupKey* first, String* alias) {
    for (AstGroupKey* key = first; key; key = (AstGroupKey*)key->next) {
        if (key->alias == alias) return true;
        if (key->alias && alias && key->alias->len == alias->len &&
            memcmp(key->alias->chars, alias->chars, alias->len) == 0) {
            return true;
        }
    }
    return false;
}

static void enter_for_group_scope(Transpiler* tp, AstForNode* for_node) {
    NameScope* scope = (NameScope*)pool_calloc(tp->pool, sizeof(NameScope));
    scope->parent = for_node->vars ? for_node->vars->parent : tp->current_scope;
    scope->is_proc = tp->current_scope ? tp->current_scope->is_proc : false;
    tp->current_scope = scope;
}

// Helper: build group by clause
AstNode* build_group_clause(Transpiler* tp, TSNode group_node) {
    log_debug("build group clause");
    AstGroupClause* ast_node = (AstGroupClause*)alloc_ast_node(tp, AST_NODE_GROUP_CLAUSE, group_node, sizeof(AstGroupClause));

    // Get the group name (from 'into name')
    TSNode name_node = ts_node_child_by_field_id(group_node, FIELD_NAME);
    StrView name_view = ts_node_source(tp, name_node);
    ast_node->name = name_pool_create_strview(tp->name_pool, name_view);

    // Build key specs (linked list)
    TSTreeCursor cursor = ts_tree_cursor_new(group_node);
    bool has_node = ts_tree_cursor_goto_first_child(&cursor);
    AstGroupKey* prev_key = NULL;
    while (has_node) {
        TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
        if (field_id == FIELD_SPEC) {
            TSNode spec_node = ts_tree_cursor_current_node(&cursor);
            AstGroupKey* key_spec = (AstGroupKey*)alloc_ast_node(tp, AST_NODE_GROUP_KEY, spec_node, sizeof(AstGroupKey));
            TSNode key_node = ts_node_child_by_field_id(spec_node, FIELD_KEY);
            key_spec->expr = build_expr(tp, key_node);

            TSNode alias_node = ts_node_child_by_field_id(spec_node, FIELD_ALIAS);
            if (!ts_node_is_null(alias_node)) {
                StrView alias_view = ts_node_source(tp, alias_node);
                key_spec->alias = name_pool_create_strview(tp->name_pool, alias_view);
            } else {
                key_spec->alias = infer_group_key_alias(tp, key_spec->expr);
                if (!key_spec->alias) {
                    log_error("Error: group by computed key requires an explicit 'as' alias");
                }
            }
            if (key_spec->alias && group_alias_exists(ast_node->keys, key_spec->alias)) {
                log_error("Error: duplicate group by key name '%.*s'; use an explicit unique alias",
                    (int)key_spec->alias->len, key_spec->alias->chars);
            }

            if (prev_key == NULL) {
                ast_node->keys = key_spec;
            } else {
                prev_key->next = (AstNode*)key_spec;
            }
            prev_key = key_spec;
            ast_node->key_count++;
        }
        has_node = ts_tree_cursor_goto_next_sibling(&cursor);
    }
    ts_tree_cursor_delete(&cursor);

    ast_node->type = set_type_any(tp, ANY_STATEMENT);
    return (AstNode*)ast_node;
}

// Helper function to build all for clauses (shared between for_expr and for_stam)
// Three-pass approach:
// Pass 1: Process loop declarations to register loop vars in scope
// Pass 2: Process let clauses (can reference loop vars)
// Pass 3: Process where, group, order, limit, offset (can reference both)
void build_for_clauses(Transpiler* tp, TSNode for_node, AstForNode* ast_node) {
    TSTreeCursor cursor = ts_tree_cursor_new(for_node);
    bool has_node = ts_tree_cursor_goto_first_child(&cursor);
    AstNode* prev_loop = NULL;
    AstNode* prev_let = NULL;
    AstNode* prev_order = NULL;

    // PASS 1: Process loop declarations to register loop vars in scope
    log_debug("for clauses pass 1: loop declarations");
    while (has_node) {
        TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
        TSNode child = ts_tree_cursor_current_node(&cursor);

        if (field_id == FIELD_DECLARE) {
            // Loop binding
            AstNode* loop = build_loop_expr(tp, child);
            log_debug("got loop type %d", loop->node_type);
            if (prev_loop == NULL) {
                ast_node->loop = loop;
            } else {
                prev_loop->next = loop;
            }
            prev_loop = loop;
        }

        has_node = ts_tree_cursor_goto_next_sibling(&cursor);
    }
    ts_tree_cursor_delete(&cursor);

    // PASS 2: Process let clauses (can reference loop vars)
    log_debug("for clauses pass 2: let clauses");
    cursor = ts_tree_cursor_new(for_node);
    has_node = ts_tree_cursor_goto_first_child(&cursor);

    while (has_node) {
        TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
        TSNode child = ts_tree_cursor_current_node(&cursor);

        if (field_id == FIELD_LET) {
            // Let clause - also registers name in scope
            AstNode* let_clause = build_for_let_clause(tp, child);
            if (prev_let == NULL) {
                ast_node->let_clause = let_clause;
            } else {
                prev_let->next = let_clause;
            }
            prev_let = let_clause;
        }

        has_node = ts_tree_cursor_goto_next_sibling(&cursor);
    }
    ts_tree_cursor_delete(&cursor);

    // PASS 3: Process clauses that reference variables (where, group, order, limit, offset)
    // These are now nested inside a for_clauses node
    log_debug("for clauses pass 3: where/group/order/limit/offset");

    // Find the for_clauses child node
    TSNode clauses_node = {0};
    uint32_t child_count = ts_node_child_count(for_node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(for_node, i);
        if (ts_node_symbol(child) == sym_for_clauses) {
            clauses_node = child;
            break;
        }
    }

    if (!ts_node_is_null(clauses_node)) {
        cursor = ts_tree_cursor_new(clauses_node);
        has_node = ts_tree_cursor_goto_first_child(&cursor);

        while (has_node) {
            TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
            TSNode child = ts_tree_cursor_current_node(&cursor);

            if (field_id == FIELD_WHERE || ts_node_symbol(child) == sym_for_where_clause) {
                // Where clause - get the condition expression
                TSNode cond_node = ts_node_child_by_field_id(child, FIELD_COND);
                ast_node->where = build_expr(tp, cond_node);
                lint_condition_expr(tp, cond_node, ast_node->where, "where");
            }
            else if (field_id == FIELD_GROUP) {
                // Group by clause
                ast_node->group = (AstGroupClause*)build_group_clause(tp, child);
                // grouping starts a post-group scope so row/let bindings cannot leak into aggregate clauses.
                enter_for_group_scope(tp, ast_node);
                // Register group name in scope as an element; row bindings deliberately remain inaccessible after grouping.
                if (ast_node->group && ast_node->group->name) {
                    AstNamedNode* group_var = (AstNamedNode*)alloc_ast_node(tp, AST_NODE_ASSIGN, child, sizeof(AstNamedNode));
                    group_var->name = ast_node->group->name;
                    group_var->type = &TYPE_ELMT;
                    push_name(tp, group_var, NULL);
                    // The group scope is intentionally separate from the row
                    // scope; retain its entry so non-MIR execution can bind
                    // `into` without making row names visible again.
                    ast_node->group->entry = lookup_name_in_current_scope(tp,
                        ast_node->group->name);
                }
            }
            else if (field_id == FIELD_ORDER) {
                // Order by clause - contains multiple order_spec
                TSTreeCursor order_cursor = ts_tree_cursor_new(child);
                bool has_spec = ts_tree_cursor_goto_first_child(&order_cursor);
                while (has_spec) {
                    TSSymbol spec_field = ts_tree_cursor_current_field_id(&order_cursor);
                    if (spec_field == FIELD_SPEC) {
                        TSNode spec_node = ts_tree_cursor_current_node(&order_cursor);
                        AstNode* order_spec = build_order_spec(tp, spec_node);
                        if (prev_order == NULL) {
                            ast_node->order = order_spec;
                        } else {
                            prev_order->next = order_spec;
                        }
                        prev_order = order_spec;
                    }
                    has_spec = ts_tree_cursor_goto_next_sibling(&order_cursor);
                }
                ts_tree_cursor_delete(&order_cursor);
            }
            else if (field_id == FIELD_LIMIT) {
                // Limit clause
                TSNode last_node = ts_node_child_by_field_id(child, FIELD_LAST);
                ast_node->limit_from_end = !ts_node_is_null(last_node);
                TSNode count_node = ts_node_child_by_field_id(child, FIELD_COUNT);
                ast_node->limit = build_expr(tp, count_node);
            }
            else if (field_id == FIELD_OFFSET) {
                // Offset clause
                TSNode count_node = ts_node_child_by_field_id(child, FIELD_COUNT);
                ast_node->offset = build_expr(tp, count_node);
            }

            has_node = ts_tree_cursor_goto_next_sibling(&cursor);
        }
        ts_tree_cursor_delete(&cursor);
    } // end if for_clauses node found

    if (!ast_node->loop) {
        log_error("Error: missing for loop declare");
    }
}

AstNode* build_for_expr(Transpiler* tp, TSNode for_node) {
    log_debug("build for expr");
    AstForNode* ast_node = (AstForNode*)alloc_ast_node(tp, AST_NODE_FOR_EXPR, for_node, sizeof(AstForNode));
    // Type will be determined after processing the 'then' expression

    ast_node->vars = (NameScope*)pool_calloc(tp->pool, sizeof(NameScope));
    ast_node->vars->parent = tp->current_scope;
    ast_node->vars->is_proc = tp->current_scope->is_proc;
    tp->current_scope = ast_node->vars;

    // Build all clauses (loop, let, where, group, order, limit, offset)
    build_for_clauses(tp, for_node, ast_node);

    TSNode then_node = ts_node_child_by_field_id(for_node, FIELD_THEN);
    if (ts_node_symbol(then_node) == SYM_CONTENT) {
        // Keep braced for-expression bodies lexically local to each iteration.
        NameScope* body_scope = (NameScope*)pool_calloc(tp->pool, sizeof(NameScope));
        body_scope->parent = tp->current_scope;
        body_scope->is_proc = tp->current_scope->is_proc;
        tp->current_scope = body_scope;
        ast_node->then = build_content(tp, then_node, true, false);
        // Hang the scope off the content node it belongs to. Lowering resolves
        // these names through its own per-scope hashmaps, so this was left
        // unset and the scope became unreachable from the AST — which any
        // later pass walking scopes (the T0 frame plan) needs it to be.
        if (ast_node->then && ast_node->then->node_type == AST_NODE_CONTENT) {
            ((AstListNode*)ast_node->then)->vars = body_scope;
        }
        tp->current_scope = body_scope->parent;
    }
    else {
        ast_node->then = build_expr(tp, then_node);
    }

    // determine for-expr type
    if (!ast_node->then) {
        log_debug("missing for then");
        ast_node->type = &TYPE_ERROR;  // fallback
    }
    else {
        log_debug("got for then type %d", ast_node->then->node_type);
        // For expression type should be Item | List containing the element type
        // TypeList* type_list = (TypeList*)alloc_type(tp->pool, LMD_TYPE_ARRAY, sizeof(TypeList));
        // type_list->nested = ast_node->then->type;
        ast_node->type = set_type_any(tp, ANY_LIST);
    }

    tp->current_scope = ast_node->vars->parent;
    return (AstNode*)ast_node;
}

AstNode* build_for_stam(Transpiler* tp, TSNode for_node) {
    log_debug("build for stam");
    AstForNode* ast_node = (AstForNode*)alloc_ast_node(tp, AST_NODE_FOR_STAM, for_node, sizeof(AstForNode));

    ast_node->vars = (NameScope*)pool_calloc(tp->pool, sizeof(NameScope));
    ast_node->vars->parent = tp->current_scope;
    ast_node->vars->is_proc = tp->current_scope->is_proc;  // inherit proc context
    tp->current_scope = ast_node->vars;

    // Build all clauses (loop, let, where, group, order, limit, offset)
    build_for_clauses(tp, for_node, ast_node);

    TSNode then_node = ts_node_child_by_field_id(for_node, FIELD_THEN);
    ast_node->then = build_expr(tp, then_node);
    log_debug("got for then type %d", ast_node->then->node_type);

    // for statement returns type
    ast_node->type = set_type_any(tp, ANY_STATEMENT);
    tp->current_scope = ast_node->vars->parent;
    return (AstNode*)ast_node;
}

// `apply;` (splat) statement: re-dispatch each child of the matched item (~)
// through the template registry. Equivalent to `for (c in ~) apply(c)`.
// Synthesizes the for-expr AST so existing MIR codegen handles it.
AstNode* build_apply_stam(Transpiler* tp, TSNode apply_node) {
    log_debug("build apply stam (splat)");

    // synthesize: for (c$apply in ~) apply(c$apply)
    AstForNode* for_node = (AstForNode*)alloc_ast_node(tp, AST_NODE_FOR_EXPR, apply_node, sizeof(AstForNode));
    for_node->vars = (NameScope*)pool_calloc(tp->pool, sizeof(NameScope));
    for_node->vars->parent = tp->current_scope;
    for_node->vars->is_proc = tp->current_scope->is_proc;
    tp->current_scope = for_node->vars;

    // synthesize loop binding `c$apply in ~`
    AstLoopNode* loop = (AstLoopNode*)alloc_ast_node(tp, AST_NODE_LOOP, apply_node, sizeof(AstLoopNode));
    StrView c_name = { .str = "c$apply", .length = 7 };
    loop->name = name_pool_create_strview(tp->name_pool, c_name);
    loop->index_name = NULL;
    loop->key_filter = LOOP_KEY_ALL;

    // current item ~ as the source
    AstNode* current_item = alloc_ast_node(tp, AST_NODE_CURRENT_ITEM, apply_node, sizeof(AstNode));
    current_item->type = set_type_any(tp, ANY_STATEMENT);
    loop->as = current_item;
    loop->type = set_type_any(tp, ANY_STATEMENT);

    // register the loop var in scope
    push_name(tp, (AstNamedNode*)loop, NULL);
    for_node->loop = (AstNode*)loop;
    for_node->let_clause = NULL;
    for_node->where = NULL;
    for_node->group = NULL;
    for_node->order = NULL;
    for_node->limit = NULL;
    for_node->offset = NULL;

    // synthesize body: apply(c$apply)
    StrView apply_name = { .str = "apply", .length = 5 };
    SysFuncInfo* apply_info = get_sys_func_info(&apply_name, 1);
    if (!apply_info) {
        log_error("apply_stam: 'apply' sys func (arity 1) not found");
        for_node->then = NULL;
        for_node->type = &TYPE_ERROR;
        tp->current_scope = for_node->vars->parent;
        return (AstNode*)for_node;
    }
    AstSysFuncNode* sys_node = (AstSysFuncNode*)alloc_ast_node(tp,
        AST_NODE_SYS_FUNC, apply_node, sizeof(AstSysFuncNode));
    sys_node->fn_info = apply_info;
    sys_node->type = apply_info->return_type;

    AstIdentNode* arg = (AstIdentNode*)alloc_ast_node(tp,
        AST_NODE_IDENT, apply_node, sizeof(AstIdentNode));
    arg->name = loop->name;
    arg->entry = lookup_name(tp, c_name);
    arg->type = set_type_any(tp, ANY_STATEMENT);

    AstCallNode* call = (AstCallNode*)alloc_ast_node(tp,
        AST_NODE_CALL_EXPR, apply_node, sizeof(AstCallNode));
    call->function = (AstNode*)sys_node;
    call->argument = (AstNode*)arg;
    call->pipe_inject = false;
    call->propagate = false;
    call->can_raise = false;
    call->type = set_type_any(tp, ANY_CALL_RESULT);

    for_node->then = (AstNode*)call;
    for_node->type = set_type_any(tp, ANY_STATEMENT);

    tp->current_scope = for_node->vars->parent;
    return (AstNode*)for_node;
}

// shared guard for the procedural-only statements (var/assign/while/break/continue/return).
// Recording a semantic error rather than only logging it is load-bearing: each guard returns
// NULL, leaving a hole in the AST (a rejected `var` never enters its name into the scope), and
// runner.cpp:730 only returns before MIR when error_count > 0. With a bare log_error the build
// looked clean, MIR ran against the holey AST, and the real diagnostic was buried under invented
// follow-on errors such as "mir: undefined variable 'x'".
static bool require_proc_scope(Transpiler* tp, TSNode node, const char* subject) {
    if (tp->current_scope && tp->current_scope->is_proc) return true;
    record_semantic_error(tp, node, ERR_PROC_IN_FN,
        "%s is only allowed inside a procedure (pn)", subject);
    return false;
}

// while statement (procedural only)
AstNode* build_while_stam(Transpiler* tp, TSNode while_node) {
    log_debug("build while stam");

    // Check if we're in a procedural context
    if (!require_proc_scope(tp, while_node, "`while`")) return NULL;

    AstWhileNode* ast_node = (AstWhileNode*)alloc_ast_node(tp, AST_NODE_WHILE_STAM, while_node, sizeof(AstWhileNode));

    ast_node->vars = (NameScope*)pool_calloc(tp->pool, sizeof(NameScope));
    ast_node->vars->parent = tp->current_scope;
    ast_node->vars->is_proc = true;
    tp->current_scope = ast_node->vars;

    // build condition
    TSNode cond_node = ts_node_child_by_field_id(while_node, FIELD_COND);
    ast_node->cond = build_expr(tp, cond_node);
    lint_condition_expr(tp, cond_node, ast_node->cond, "while");
    log_debug("got while cond type %d", ast_node->cond ? ast_node->cond->node_type : -1);

    // build body
    TSNode body_node = ts_node_child_by_field_id(while_node, FIELD_BODY);
    ast_node->body = build_expr(tp, body_node);
    log_debug("got while body type %d", ast_node->body ? ast_node->body->node_type : -1);

    ast_node->type = set_type_any(tp, ANY_STATEMENT);
    tp->current_scope = ast_node->vars->parent;
    return (AstNode*)ast_node;
}

// break statement (procedural only)
AstNode* build_break_stam(Transpiler* tp, TSNode break_node) {
    log_debug("build break stam");

    // Check if we're in a procedural context
    if (!require_proc_scope(tp, break_node, "`break`")) return NULL;

    AstNode* ast_node = alloc_ast_node(tp, AST_NODE_BREAK_STAM, break_node, sizeof(AstNode));
    ast_node->type = set_type_any(tp, ANY_STATEMENT);
    return ast_node;
}

// continue statement (procedural only)
AstNode* build_continue_stam(Transpiler* tp, TSNode continue_node) {
    log_debug("build continue stam");

    // Check if we're in a procedural context
    if (!require_proc_scope(tp, continue_node, "`continue`")) return NULL;

    AstNode* ast_node = alloc_ast_node(tp, AST_NODE_CONTINUE_STAM, continue_node, sizeof(AstNode));
    ast_node->type = set_type_any(tp, ANY_STATEMENT);
    return ast_node;
}

// return statement (procedural only)
AstNode* build_return_stam(Transpiler* tp, TSNode return_node) {
    log_debug("build return stam");

    // Check if we're in a procedural context
    if (!require_proc_scope(tp, return_node, "`return`")) return NULL;

    AstReturnNode* ast_node = (AstReturnNode*)alloc_ast_node(tp, AST_NODE_RETURN_STAM, return_node, sizeof(AstReturnNode));

    // build optional return value
    TSNode value_node = ts_node_child_by_field_id(return_node, FIELD_VALUE);
    if (!ts_node_is_null(value_node)) {
        ast_node->value = build_expr(tp, value_node);
        ast_node->type = ast_node->value ? ast_node->value->type : &TYPE_ANY;
    } else {
        ast_node->value = NULL;
        ast_node->type = &TYPE_NULL;
    }

    return (AstNode*)ast_node;
}

// raise statement - raises an error to the caller
// Allowed in:
// 1. Procedural functions (pn) - can always raise
// 2. Pure functions (fn) with error return type (T^E or T@)
static void build_raise_value(Transpiler* tp, AstRaiseNode* ast_node, TSNode value_node) {
    if (!ts_node_is_null(value_node)) {
        ast_node->value = build_expr(tp, value_node);
        ast_node->type = ast_node->value ? ast_node->value->type : &TYPE_ERROR;
    } else {
        log_error("Error: 'raise' requires an error expression");
        ast_node->value = NULL;
        ast_node->type = &TYPE_ERROR;
    }
}

AstNode* build_raise_stam(Transpiler* tp, TSNode raise_node) {
    log_debug("build raise stam");

    // Check if we're in a context where raise is allowed
    // For now, we allow raise in procedures (is_proc=true) and will add
    // function error type checking later for full error handling support
    // TODO: Also allow in pure functions with error return type
    if (!tp->current_scope->is_proc) {
        // For now, log warning but continue - this allows raise in fn bodies
        log_debug("'raise' in pure function - function should have error return type");
    }

    AstRaiseNode* ast_node = (AstRaiseNode*)alloc_ast_node(tp, AST_NODE_RAISE_STAM, raise_node, sizeof(AstRaiseNode));

    // raise_stam now wraps a raise_expr child — get the value from it
    TSNode raise_expr_node = ts_node_child(raise_node, 0);
    TSNode value_node = ts_node_is_null(raise_expr_node) ? raise_expr_node
                        : ts_node_child_by_field_id(raise_expr_node, FIELD_VALUE);
    build_raise_value(tp, ast_node, value_node);

    return (AstNode*)ast_node;
}

// raise expression (functional) - raises an error in expression context
AstNode* build_raise_expr(Transpiler* tp, TSNode raise_node) {
    log_debug("build raise expr");

    AstRaiseNode* ast_node = (AstRaiseNode*)alloc_ast_node(tp, AST_NODE_RAISE_EXPR, raise_node, sizeof(AstRaiseNode));

    // build required error value
    TSNode value_node = ts_node_child_by_field_id(raise_node, FIELD_VALUE);
    build_raise_value(tp, ast_node, value_node);

    return (AstNode*)ast_node;
}

// var statement for mutable variables (procedural only)
AstNode* build_var_stam(Transpiler* tp, TSNode var_node) {
    log_debug("build var stam");

    // Check if we're in a procedural context
    if (!require_proc_scope(tp, var_node, "`var`")) return NULL;

    // Reuse the let statement builder but mark as VAR_STAM
    AstLetNode* ast_node = (AstLetNode*)alloc_ast_node(tp, AST_NODE_VAR_STAM, var_node, sizeof(AstLetNode));

    // Build declarations (same logic as build_let_and_type_stam for let)
    TSTreeCursor cursor = ts_tree_cursor_new(var_node);
    bool has_node = ts_tree_cursor_goto_first_child(&cursor);
    AstNode* prev_declare = NULL;
    while (has_node) {
        TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
        if (field_id == FIELD_DECLARE) {
            TSNode child = ts_tree_cursor_current_node(&cursor);
            AstNode* assign = build_assign_expr(tp, child, false);
            if (assign && assign->node_type == AST_NODE_ASSIGN) {
                AstNamedNode* named = (AstNamedNode*)assign;
                // mark the name entry as mutable (var)
                NameEntry* entry = lookup_name_in_current_scope(tp, named->name);
                if (entry) {
                    entry->is_mutable = true;
                    named->entry = entry;
                    // check if type annotation was provided
                    TSNode type_node = ts_node_child_by_field_id(child, FIELD_TYPE);
                    entry->has_type_annotation = !ts_node_is_null(type_node);
                }
            }
            if (prev_declare == NULL) {
                ast_node->declare = assign;
            } else {
                prev_declare->next = assign;
            }
            prev_declare = assign;
        }
        has_node = ts_tree_cursor_goto_next_sibling(&cursor);
    }
    ts_tree_cursor_delete(&cursor);

    ast_node->type = set_type_any(tp, ANY_STATEMENT);
    return (AstNode*)ast_node;
}

// assignment statement for mutable variables (procedural only)
// supports: x = val, arr[i] = val, obj.field = val
AstNode* build_assign_stam(Transpiler* tp, TSNode assign_node) {
    log_debug("build assign stam");

    // Check if we're in a procedural context
    if (!require_proc_scope(tp, assign_node, "assignment")) return NULL;

    // get target node — could be identifier, index_expr, or member_expr
    TSNode target_node = ts_node_child_by_field_id(assign_node, FIELD_TARGET);
    TSSymbol target_symbol = ts_node_symbol(target_node);

    // build value expression (same for all cases)
    TSNode value_node = ts_node_child_by_field_id(assign_node, FIELD_VALUE);

    if (target_symbol == SYM_INDEX_EXPR) {
        // arr[i] = val — compound index assignment
        AstCompoundAssignNode* ast_node = (AstCompoundAssignNode*)alloc_ast_node(
            tp, AST_NODE_INDEX_ASSIGN_STAM, assign_node, sizeof(AstCompoundAssignNode));

        // build object (the array/container)
        TSNode obj_node = ts_node_child_by_field_id(target_node, FIELD_OBJECT);
        ast_node->object = build_expr(tp, obj_node);
        validate_compound_mutable_root(tp, assign_node, ast_node->object);

        // build index expression(s) — multi-dim arr[i, j, k] = v chains keys via ->next
        TSNode idx_node = ts_node_child_by_field_id(target_node, FIELD_FIELD);
        ast_node->key = build_expr(tp, idx_node);
        if (ast_node->key) {
            AstNode* tail = ast_node->key;
            TSTreeCursor cur = ts_tree_cursor_new(target_node);
            bool ok = ts_tree_cursor_goto_first_child(&cur);
            bool seen_first = false;
            while (ok) {
                TSSymbol fid = ts_tree_cursor_current_field_id(&cur);
                if (fid == FIELD_FIELD) {
                    if (!seen_first) {
                        seen_first = true;
                    } else {
                        TSNode extra = ts_tree_cursor_current_node(&cur);
                        AstNode* next_idx = build_expr(tp, extra);
                        if (next_idx) { tail->next = next_idx; tail = next_idx; }
                    }
                }
                ok = ts_tree_cursor_goto_next_sibling(&cur);
            }
            ts_tree_cursor_delete(&cur);
        }

        // build value expression
        ast_node->value = build_expr(tp, value_node);
        ast_node->type = ast_node->value ? ast_node->value->type : &TYPE_ANY;
        ast_node->op = OPERATOR_ASSIGN;
        AstFieldNode* left = (AstFieldNode*)alloc_ast_node(tp, AST_NODE_INDEX_EXPR, target_node, sizeof(AstFieldNode));
        left->object = ast_node->object;
        left->field = ast_node->key;
        left->computed = true;
        left->type = set_type_any(tp, ANY_INDEX_ELEM);
        ast_node->left = (AstNode*)left;
        ast_node->right = ast_node->value;

        check_compound_assignment_static_boundary(tp, assign_node, (AstNode*)left,
            ast_node->value, "array element");

        return (AstNode*)ast_node;
    }
    else if (target_symbol == SYM_MEMBER_EXPR) {
        // obj.field = val — compound member assignment
        AstCompoundAssignNode* ast_node = (AstCompoundAssignNode*)alloc_ast_node(
            tp, AST_NODE_MEMBER_ASSIGN_STAM, assign_node, sizeof(AstCompoundAssignNode));

        // build object (the map/element)
        TSNode obj_node = ts_node_child_by_field_id(target_node, FIELD_OBJECT);
        ast_node->object = build_expr(tp, obj_node);
        validate_compound_mutable_root(tp, assign_node, ast_node->object);

        // build field name as identifier node
        TSNode field_node = ts_node_child_by_field_id(target_node, FIELD_FIELD);
        TSSymbol field_sym = ts_node_symbol(field_node);
        if (field_sym == SYM_IDENT || field_sym == SYM_BASE_TYPE) {
            AstIdentNode* id_node = (AstIdentNode*)alloc_ast_node(tp, AST_NODE_IDENT, field_node, sizeof(AstIdentNode));
            StrView var_name = ts_node_source(tp, field_node);
            id_node->name = name_pool_create_strview(tp->name_pool, var_name);
            log_debug("member assign field name: '%.*s'", (int)id_node->name->len, id_node->name->chars);
            ast_node->key = (AstNode*)id_node;
        }
        else {
            ast_node->key = build_expr(tp, field_node);
        }

        // build value expression
        ast_node->value = build_expr(tp, value_node);
        ast_node->type = ast_node->value ? ast_node->value->type : &TYPE_ANY;
        ast_node->op = OPERATOR_ASSIGN;
        AstFieldNode* left = (AstFieldNode*)alloc_ast_node(tp, AST_NODE_MEMBER_EXPR, target_node, sizeof(AstFieldNode));
        left->object = ast_node->object;
        left->field = ast_node->key;
        left->computed = false;
        left->type = set_type_any(tp, ANY_MEMBER_SHAPE);
        ast_node->left = (AstNode*)left;
        ast_node->right = ast_node->value;

        check_compound_assignment_static_boundary(tp, assign_node, (AstNode*)left,
            ast_node->value, "map member");

        return (AstNode*)ast_node;
    }
    else {
        // simple variable assignment: x = val
        AstAssignStamNode* ast_node = (AstAssignStamNode*)alloc_ast_node(tp, AST_NODE_ASSIGN_STAM, assign_node, sizeof(AstAssignStamNode));

        // get target identifier
        StrView target_str = ts_node_source(tp, target_node);
        ast_node->target = name_pool_create_strview(tp->name_pool, target_str);

        // lookup target variable to get its type info
        NameEntry* entry = lookup_name(tp, target_str);
        ast_node->target_node = entry ? entry->node : NULL;
        ast_node->target_entry = entry;
        ast_node->op = OPERATOR_ASSIGN;
        AstIdentNode* left = (AstIdentNode*)alloc_ast_node(tp, AST_NODE_IDENT, target_node, sizeof(AstIdentNode));
        left->name = ast_node->target;
        left->entry = entry;
        left->type = (entry && entry->node && entry->node->type) ? entry->node->type : &TYPE_ANY;
        ast_node->left = (AstNode*)left;

        // check that the target is a mutable variable (declared with var)
        // Exception: in pn method bodies, object fields (AST_NODE_KEY_EXPR) are mutable
        bool is_field_in_pn = (entry && entry->node &&
                               entry->node->node_type == AST_NODE_KEY_EXPR &&
                               tp->current_scope && tp->current_scope->is_proc);
        if (entry && !entry->is_mutable && !is_field_in_pn) {
            if (entry->node->node_type == AST_NODE_PARAM) {
                record_semantic_error(tp, assign_node, ERR_IMMUTABLE_ASSIGNMENT,
                    "cannot assign to '%.*s': parameter bindings are immutable",
                    (int)target_str.length, target_str.str);
            }
            else {
                // immutable let bindings are intentional; point users at var for reassignment.
                record_semantic_error(tp, assign_node, ERR_IMMUTABLE_ASSIGNMENT,
                    "cannot assign to let binding '%.*s'. declare with `var` instead of `let`.",
                    (int)target_str.length, target_str.str);
            }
        }

        // build value expression
        ast_node->value = build_expr(tp, value_node);
        ast_node->type = ast_node->value ? ast_node->value->type : &TYPE_ANY;
        ast_node->right = ast_node->value;

        // type analysis for var assignment
        if (entry && entry->is_mutable && ast_node->value && ast_node->value->type && entry->node->type) {
            TypeId var_tid = entry->node->type->type_id;
            TypeId val_tid = ast_node->value->type->type_id;

            if (entry->has_type_annotation && entry->declared_type) {
                // An explicit var annotation is a binding contract, not a
                // conversion request.  In particular, null and float values
                // cannot enter an int lane just because the old type ids differ.
                StaticBoundaryResult relation = static_boundary_relation(
                    ast_node->value->type, entry->declared_type);
                if (relation == STATIC_BOUNDARY_REJECTED) {
                    int line = ts_node_start_point(assign_node).row + 1;
                    char value_name[128];
                    char expected_name[128];
                    lambda_type_format_name(ast_node->value->type, value_name,
                        sizeof(value_name));
                    lambda_type_format_name(entry->declared_type, expected_name,
                        sizeof(expected_name));
                    record_type_error_code(tp, line, ERR_TYPE_MISMATCH,
                        "cannot assign %s value to var '%.*s' of type %s",
                        value_name,
                        (int)target_str.length, target_str.str,
                        expected_name);
                }
            } else if (var_tid != val_tid && !entry->type_widened) {
                    // non-annotated var: widen to Item if types differ
                    // null-initialized vars must widen too; otherwise later map
                    // literals keep a null-shaped field and drop reassigned values.
                    if (var_tid != LMD_TYPE_ANY) {
                        entry->type_widened = true;
                        log_debug("var '%.*s' widened to Item (was %s, assigned %s)",
                            (int)target_str.length, target_str.str,
                            get_type_name(var_tid), get_type_name(val_tid));
                    }
            }
        }

        return (AstNode*)ast_node;
    }
}

// returns NULL for variadic marker (...)
// Fold a declared type into a TypeParam: copy the compact Type prefix, restore
// the param-only flags, then choose the retained contract and full_type. Shared
// with the type-pattern hand parser, which builds `fn(a: T)` params without a
// CST node to read.
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

    Type* parameter_contract = declared;
    if (was_optional && !default_value) {
        // An omitted `p?: T` reaches the body as null.  The compact TypeParam
        // remains T for its register class, but its full contract must be T? so
        // native lanes do not decode absence as a zero value.
        parameter_contract = lambda_type_nullable_normalized(tp->pool, parameter_contract);
    }
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

AstNamedNode* build_param_expr(Transpiler* tp, TSNode param_node, bool is_type) {
    log_debug("build param expr");

    // check for variadic marker (...)
    TSNode variadic = ts_node_child_by_field_id(param_node, FIELD_VARIADIC);
    if (!ts_node_is_null(variadic)) {
        log_debug("build param: variadic marker found");
        return NULL;  // special marker, will be handled by build_func
    }

    AstNamedNode* ast_node = (AstNamedNode*)alloc_ast_node(tp, AST_NODE_PARAM, param_node, sizeof(AstNamedNode));

    TSNode name = ts_node_child_by_field_id(param_node, FIELD_NAME);
    StrView name_str = node_name_text(tp, name);
    ast_node->name = name_pool_create_strview(tp->name_pool, name_str);

    // allocate TypeParam for this parameter
    TypeParam* param_type = (TypeParam*)alloc_type(tp->pool, LMD_TYPE_ANY, sizeof(TypeParam));
    param_type->kind = TYPE_KIND_PARAM;
    ast_node->type = (Type*)param_type;
    set_param_contract(param_type, &TYPE_ANY_NO_ERROR, false);

    TSNode var_node = ts_node_child_by_field_id(param_node, FIELD_VAR);
    param_type->is_var_param = !ts_node_is_null(var_node);

    // check optional marker (?)
    TSNode optional_node = ts_node_child_by_field_id(param_node, FIELD_OPTIONAL);
    param_type->is_optional = !ts_node_is_null(optional_node);

    // check for default value expression
    TSNode default_node = ts_node_child_by_field_id(param_node, FIELD_DEFAULT);
    if (!ts_node_is_null(default_node)) {
        param_type->default_value = build_expr(tp, default_node);
        param_type->is_optional = true;  // param with default is implicitly optional
    }

    // determine the type of the parameter
    TSNode type_node = ts_node_child_by_field_id(param_node, FIELD_TYPE);
    if (!ts_node_is_null(type_node)) {
        AstNode* type_expr = build_expr(tp, type_node);
        // validate that type_expr is actually a type (TypeType)
        if (type_expr && type_expr->type && type_expr->type->type_id == LMD_TYPE_TYPE) {
            TypeType* type_type = (TypeType*)type_expr->type;
            if (type_type->type) {
                // Keep the original full annotation for runtime boundary
                // checks; the TypeParam prefix only preserves a compact TypeId.
                ast_node->declared_type = type_type->type;
                apply_declared_param_type(tp, param_type, type_type->type);
            }
        } else {
            // invalid type annotation - record error but continue with ANY type
            StrView type_str = ts_node_source(tp, type_node);
            // structured recorder: gets --static-warning downgrade + list
            record_semantic_error(tp, type_node, ERR_UNDEFINED_TYPE,
                "invalid type annotation '%.*s' - not a valid type",
                (int)type_str.length, type_str.str);
            set_param_contract(param_type, &TYPE_ANY_NO_ERROR, false);
        }
    }
    else {
        *(Type*)param_type = ast_node->as ? *ast_node->as->type : TYPE_ANY;
        param_type->kind = TYPE_KIND_PARAM;
        param_type->full_type = NULL;
        set_param_contract(param_type, &TYPE_ANY_NO_ERROR, false);
    }

    if (!is_type) {
        push_name(tp, ast_node, NULL);
        // Legacy pn parameters remain locally mutable; explicit `var` marks
        // inout intent for Phase 5 call-site checks.
        if (tp->current_scope && tp->current_scope->is_proc) {
            NameEntry* entry = lookup_name_in_current_scope(tp, ast_node->name);
            if (entry) {
                entry->is_mutable = true;
                entry->is_var_param = param_type->is_var_param;
            }
        }
    }
    return ast_node;
}

// build named argument in function call: name: value
AstNode* build_named_argument(Transpiler* tp, TSNode arg_node) {
    log_debug("build named argument");
    AstNamedNode* ast_node = (AstNamedNode*)alloc_ast_node(tp, AST_NODE_NAMED_ARG, arg_node, sizeof(AstNamedNode));

    TSNode name = ts_node_child_by_field_id(arg_node, FIELD_NAME);
    StrView name_str = node_name_text(tp, name);
    ast_node->name = name_pool_create_strview(tp->name_pool, name_str);

    TSNode value_node = ts_node_child_by_field_id(arg_node, FIELD_VALUE);
    ast_node->as = build_expr(tp, value_node);
    ast_node->type = ast_node->as ? ast_node->as->type : &TYPE_ANY;

    log_debug("named argument: %s", ast_node->name);
    return (AstNode*)ast_node;
}

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
    TSPoint point = ts_node_start_point(site ? site->node : fn->node);
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
        TSPoint point = ts_node_start_point(node->node);
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
    record_semantic_error(tp, call->node, ERR_UNHANDLED_ERROR,
        "error from '%.*s' must be handled: use '%.*s(...)^' to propagate, "
        "handle with '%.*s(...) ^ { ... }', or recover with '%.*s(...) or default'",
        name_length, name, name_length, name, name_length, name, name_length, name);
}

static bool match_arm_is_error_handler(AstMatchArm* arm) {
    AstNode* pattern = arm ? boundary_unwrap_primary(arm->pattern) : NULL;
    if (!pattern || !pattern->type || pattern->type->type_id != LMD_TYPE_TYPE) return false;
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

static void validate_function_enforcing_calls(Transpiler* tp, AstFuncNode* fn,
        TypeFunc* signature) {
    if (!fn || !signature || !fn->body) return;
    bool return_acknowledgment = signature->has_explicit_return_contract &&
        (signature->can_raise || lambda_type_has_proven_error(signature->return_contract));
    AstNode* body = boundary_unwrap_primary(fn->body);
    if (body && body->node_type == AST_NODE_CONTENT) {
        AstNode* item = ((AstListNode*)body)->item;
        for (; item; item = item->next) {
            bool tail_acknowledgment = return_acknowledgment && !item->next;
            validate_enforcing_calls_in_expression(tp, item, tail_acknowledgment,
                return_acknowledgment);
        }
        return;
    }
    validate_enforcing_calls_in_expression(tp, body, return_acknowledgment,
        return_acknowledgment);
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
    record_semantic_error(state->tp, node->node, ERR_INVALIDATED_BINDING,
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

static bool apply_function_return_type(Transpiler* tp, TypeFunc* fn_type,
                                       AstNode* type_expr, TSNode type_node,
                                       bool second_pass) {
    if (type_expr && type_expr->type && type_expr->type->type_id == LMD_TYPE_TYPE) {
        Type* inner_type = ((TypeType*)type_expr->type)->type;
        if (inner_type && inner_type->type_id == LMD_TYPE_FUNC) {
            TypeFunc* return_type_info = (TypeFunc*)inner_type;
            fn_type->returned = return_type_info->returned;
            fn_type->inferred_return = return_type_info->inferred_return
                ? return_type_info->inferred_return : return_type_info->returned;
            fn_type->error_type = return_type_info->error_type;
            fn_type->can_raise = return_type_info->can_raise;
            set_function_return_contract(fn_type, return_type_info->return_contract, true);
            log_debug("%sfunction return type: ok=%d, error=%d, can_raise=%d",
                second_pass ? "pass 2 " : "",
                fn_type->returned ? fn_type->returned->type_id : -1,
                fn_type->error_type ? fn_type->error_type->type_id : -1,
                fn_type->can_raise);
        } else {
            fn_type->returned = inner_type;
            fn_type->inferred_return = inner_type;
            fn_type->error_type = NULL;
            fn_type->can_raise = false;
            set_function_return_contract(fn_type, inner_type, true);
        }
        return true;
    }
    StrView type_str = ts_node_source(tp, type_node);
    // structured recorder: gets --static-warning downgrade + list
    record_semantic_error(tp, type_node, ERR_UNDEFINED_TYPE,
        "invalid return type '%.*s' - not a valid type",
        (int)type_str.length, type_str.str);
    fn_type->returned = &TYPE_ANY;
    set_function_return_contract(fn_type, &TYPE_ANY, true);
    return false;
}

// for both func expr and stam
AstNode* build_func(Transpiler* tp, TSNode func_node, bool is_named, bool is_global) {
    log_debug("build function");
    bool is_proc = false;
    TSNode kind = ts_node_child_by_field_id(func_node, FIELD_KIND);
    if (!ts_node_is_null(kind)) {
        StrView kind_str = ts_node_source(tp, kind);
        is_proc = strview_equal(&kind_str, "pn");
    }
    log_debug("is proc: %d", is_proc);

    AstFuncNode* ast_node = (AstFuncNode*)alloc_ast_node(tp,
        is_proc ? AST_NODE_PROC : is_named ? AST_NODE_FUNC : AST_NODE_FUNC_EXPR,
        func_node, sizeof(AstFuncNode));
    ast_node->type = alloc_type(tp->pool, LMD_TYPE_FUNC, sizeof(TypeFunc));
    TypeFunc* fn_type = (TypeFunc*)ast_node->type;
    fn_type->is_anonymous = !is_named;  fn_type->is_proc = is_proc;
    set_function_return_contract(fn_type, is_proc ? &TYPE_ANY : &TYPE_ANY_NO_ERROR,
        false);

    // 'pub' flag
    TSNode pub = ts_node_child_by_field_id(func_node, FIELD_PUB);
    fn_type->is_public = !ts_node_is_null(pub);

    // get the function name
    if (is_named) {
        TSNode fn_name_node = ts_node_child_by_field_id(func_node, FIELD_NAME);
        StrView name = node_name_text(tp, fn_name_node);

        // check if name conflicts with a system function (only for global scope)
        if (is_global && is_sys_func_name(name.str, name.length)) {
            log_debug("user function '%.*s' shadows system function with same name",
                (int)name.length, name.str);
        }

        ast_node->name = name_pool_create_strview(tp->name_pool, name);
        // add fn name to current scope
        push_name(tp, (AstNamedNode*)ast_node, NULL);
    }

    // build the params
    ast_node->vars = (NameScope*)pool_calloc(tp->pool, sizeof(NameScope));
    ast_node->vars->parent = tp->current_scope;
    ast_node->vars->is_proc = is_proc;
    tp->current_scope = ast_node->vars;
    AstNamedNode* prev_param = NULL;  int param_count = 0;  int required_count = 0;
    bool seen_optional = false;  // track if we've seen an optional param

    // for anonymous fn_expr with untyped params: (a, b) => expr
    // the grammar parses untyped arrow functions as fn_expr → expr... => body
    // where the unnamed expr children (identifiers) before body are treated as parameters
    if (!is_named) {
        // iterate named children that have no field (untyped params are unfielded exprs)
        TSTreeCursor pre_cursor = ts_tree_cursor_new(func_node);
        bool has_pre = ts_tree_cursor_goto_first_child(&pre_cursor);
        while (has_pre) {
            TSSymbol pre_field = ts_tree_cursor_current_field_id(&pre_cursor);
            TSNode child = ts_tree_cursor_current_node(&pre_cursor);
            if (pre_field == 0 && ts_node_is_named(child)) {
                // unnamed named child — could be an untyped param identifier
                TSNode param_node = child;
                TSSymbol child_sym = ts_node_symbol(param_node);
                while (child_sym == sym_primary_expr) {
                    param_node = ts_node_named_child(param_node, 0);
                    child_sym = ts_node_symbol(param_node);
                }
                log_debug("fn_expr: unnamed child symbol=%d", child_sym);
                if (child_sym == sym_identifier) {
                    AstNamedNode* param = (AstNamedNode*)alloc_ast_node(tp,
                        AST_NODE_PARAM, child, sizeof(AstNamedNode));
                    StrView name_str = ts_node_source(tp, child);
                    param->name = name_pool_create_strview(tp->name_pool, name_str);

                    TypeParam* param_type = (TypeParam*)alloc_type(tp->pool, LMD_TYPE_ANY, sizeof(TypeParam));
                    *(Type*)param_type = TYPE_ANY;
                    param_type->kind = TYPE_KIND_PARAM;
                    param_type->full_type = NULL;
                    set_param_contract(param_type, &TYPE_ANY_NO_ERROR, false);
                    param->type = (Type*)param_type;
                    push_name(tp, param, NULL);
                    if (is_proc) {
                        NameEntry* entry = lookup_name_in_current_scope(tp, param->name);
                        if (entry) entry->is_mutable = true;
                    }

                    if (prev_param == NULL) {
                        ast_node->param = param;
                        fn_type->param = param_type;
                    } else {
                        prev_param->next = (AstNode*)param;
                        ((TypeParam*)prev_param->type)->next = param_type;
                    }
                    prev_param = param;  param_count++;  required_count++;
                    log_debug("fn_expr: added untyped param '%.*s'",
                        (int)name_str.length, name_str.str);
                }
            }
            has_pre = ts_tree_cursor_goto_next_sibling(&pre_cursor);
        }
        ts_tree_cursor_delete(&pre_cursor);
    }

    TSTreeCursor cursor = ts_tree_cursor_new(func_node);
    bool has_node = ts_tree_cursor_goto_first_child(&cursor);
    while (has_node) {
        TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
        if (field_id == FIELD_DECLARE) {  // param declaration
            TSNode child = ts_tree_cursor_current_node(&cursor);
            AstNamedNode* param = build_param_expr(tp, child, false);

            // check for variadic marker (NULL return from build_param_expr)
            if (param == NULL) {
                fn_type->is_variadic = true;
                log_debug("function is variadic");
                has_node = ts_tree_cursor_goto_next_sibling(&cursor);
                continue;
            }

            TypeParam* param_type = (TypeParam*)param->type;
            log_debug("got param: %s, optional=%d", param->name, param_type->is_optional);

            // validate param ordering: required params must come before optional
            if (param_type->is_optional) {
                seen_optional = true;
            } else {
                if (seen_optional) {
                    log_error("required parameter '%s' cannot follow optional parameter", param->name);
                }
                required_count++;
            }

            if (prev_param == NULL) {
                ast_node->param = param;
                fn_type->param = param_type;
            }
            else {
                prev_param->next = (AstNode*)param;
                ((TypeParam*)prev_param->type)->next = param_type;
            }
            prev_param = param;  param_count++;
        }
        else if (field_id == FIELD_TYPE) {  // return type
            TSNode child = ts_tree_cursor_current_node(&cursor);
            AstNode* type_expr = build_expr(tp, child);
            apply_function_return_type(tp, fn_type, type_expr, child, false);
        }
        has_node = ts_tree_cursor_goto_next_sibling(&cursor);
    }
    ts_tree_cursor_delete(&cursor);
    fn_type->param_count = param_count;
    fn_type->required_param_count = required_count;
    (void)validate_lambda_argument_limit(tp, func_node,
        param_count + (fn_type->is_variadic ? 1 : 0), "function formal");

    // build the function body
    // ast_node->locals = (NameScope*)pool_calloc(tp->pool, sizeof(NameScope));
    // ast_node->locals->parent = tp->current_scope;
    // tp->current_scope = ast_node->locals;
    TSNode fn_body_node = ts_node_child_by_field_id(func_node, FIELD_BODY);
    bool saved_handler_body = tp->building_handler_body;
    // A function literal created inside a handler is a separate callable
    // scope; its body cannot capture the handler's ephemeral current error.
    tp->building_handler_body = false;
    ast_node->body = build_expr(tp, fn_body_node);
    tp->building_handler_body = saved_handler_body;

    // determine the function return type
    fn_type->inferred_return = is_proc ? infer_procedural_return_type(tp, ast_node) :
        ast_node->body ? ast_node->body->type : &TYPE_ANY;
    if (!fn_type->returned) {
        fn_type->returned = fn_type->inferred_return;
    }
    validate_function_return_contract(tp, ast_node, fn_type);
    validate_function_enforcing_calls(tp, ast_node, fn_type);

    // restore parent namescope
    tp->current_scope = ast_node->vars->parent;

    // Analyze captures for closure support
    NameScope* global_scope = find_global_scope(ast_node->vars);
    analyze_captures(tp, ast_node, global_scope);
    validate_cross_frame_binding_reads(tp, ast_node);

    log_debug("end building fn");
    return (AstNode*)ast_node;
}

// Build a view/edit template declaration
AstNode* build_view_stam(Transpiler* tp, TSNode view_node) {
    log_debug("build view/edit template");

    AstViewNode* ast_node = (AstViewNode*)alloc_ast_node(tp,
        AST_NODE_VIEW, view_node, sizeof(AstViewNode));
    ast_node->type = set_type_any(tp, ANY_STATEMENT);

    // determine view vs edit
    TSNode kind = ts_node_child_by_field_id(view_node, FIELD_KIND);
    if (!ts_node_is_null(kind)) {
        StrView kind_str = ts_node_source(tp, kind);
        ast_node->is_edit = strview_equal(&kind_str, "edit");
    } else {
        // The compact view/edit token is hidden from the CST; the declaration
        // source starts with the keyword that selects the template behavior.
        StrView source = ts_node_source(tp, view_node);
        ast_node->is_edit = source.length >= 4 && memcmp(source.str, "edit", 4) == 0;
    }
    log_debug("is edit: %d", ast_node->is_edit);

    // optional name (identifier followed by ':')
    TSNode name_node = ts_node_child_by_field_id(view_node, FIELD_NAME);
    if (!ts_node_is_null(name_node)) {
        StrView name = node_name_text(tp, name_node);
        ast_node->name = name_pool_create_strview(tp->name_pool, name);
        log_debug("view template name: %.*s", (int)name.length, name.str);
    }

    // model pattern (required)
    TSNode pattern_node = ts_node_child_by_field_id(view_node, FIELD_PATTERN);
    if (!ts_node_is_null(pattern_node)) {
        // view_pattern is a wrapper node — unwrap to get the actual type expression
        TSSymbol pat_sym = ts_node_symbol(pattern_node);
        if (pat_sym == SYM_VIEW_PATTERN) {
            TSNode inner = ts_node_named_child(pattern_node, 0);
            if (!ts_node_is_null(inner)) {
                pattern_node = inner;
            }
        }
        ast_node->pattern = build_expr(tp, pattern_node);
    } else {
        log_error("view/edit template missing required pattern");
    }

    // create scope for params, state, and body
    ast_node->vars = (NameScope*)pool_calloc(tp->pool, sizeof(NameScope));
    ast_node->vars->parent = tp->current_scope;
    ast_node->vars->is_proc = false; // body is functional
    tp->current_scope = ast_node->vars;

    // add ~ (current item) to scope — it refers to the matched model item
    // (this is inherent to view/edit — ~ is always available)

    // build parameters (optional)
    AstNamedNode* prev_param = NULL;
    TSTreeCursor cursor = ts_tree_cursor_new(view_node);
    bool has_node = ts_tree_cursor_goto_first_child(&cursor);
    while (has_node) {
        TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
        if (field_id == FIELD_DECLARE) {
            TSNode child = ts_tree_cursor_current_node(&cursor);
            AstNamedNode* param = build_param_expr(tp, child, false);
            if (param != NULL) {
                if (prev_param == NULL) {
                    ast_node->param = param;
                } else {
                    prev_param->next = (AstNode*)param;
                }
                prev_param = param;
            }
        }
        has_node = ts_tree_cursor_goto_next_sibling(&cursor);
    }
    ts_tree_cursor_delete(&cursor);

    // build state declarations (optional)
    TSNode state_node = ts_node_child_by_field_id(view_node, FIELD_STATE);
    if (!ts_node_is_null(state_node)) {
        AstStateEntry* prev_state = NULL;
        TSNode child = ts_node_named_child(state_node, 0);
        while (!ts_node_is_null(child)) {
            TSSymbol sym = ts_node_symbol(child);
            if (sym == SYM_STATE_ENTRY) {
                AstStateEntry* entry = (AstStateEntry*)alloc_ast_node(tp,
                    AST_NODE_STATE_ENTRY, child, sizeof(AstStateEntry));
                entry->type = set_type_any(tp, ANY_STATEMENT);

                TSNode name = ts_node_child_by_field_id(child, FIELD_NAME);
                StrView name_str = node_name_text(tp, name);
                entry->name = name_pool_create_strview(tp->name_pool, name_str);

                TSNode value = ts_node_child_by_field_id(child, FIELD_VALUE);
                if (!ts_node_is_null(value)) {
                    entry->value = build_expr(tp, value);
                }

                log_debug("state entry: %.*s", (int)name_str.length, name_str.str);

                // add state variable to scope (accessible in body and handlers)
                AstNamedNode* state_named = (AstNamedNode*)alloc_ast_node(tp,
                    AST_NODE_PARAM, child, sizeof(AstNamedNode));
                state_named->name = entry->name;
                state_named->type = entry->value ? entry->value->type : &TYPE_ANY;
                push_name(tp, state_named, NULL);
                // mark state vars mutable so handlers can assign to them
                NameEntry* state_entry = lookup_name_in_current_scope(tp, entry->name);
                if (state_entry) state_entry->is_mutable = true;

                if (prev_state == NULL) {
                    ast_node->state = entry;
                } else {
                    prev_state->next_state = entry;
                }
                prev_state = entry;
            }
            child = ts_node_next_named_sibling(child);
        }
    }

    // build body (functional — fn semantics)
    TSNode body_node = ts_node_child_by_field_id(view_node, FIELD_BODY);
    if (!ts_node_is_null(body_node)) {
        ast_node->body = build_expr(tp, body_node);
    }

    // build event handlers (procedural — pn semantics)
    AstEventHandler* prev_handler = NULL;
    cursor = ts_tree_cursor_new(view_node);
    has_node = ts_tree_cursor_goto_first_child(&cursor);
    while (has_node) {
        TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
        if (field_id == FIELD_HANDLER) {
            TSNode handler_node = ts_tree_cursor_current_node(&cursor);

            AstEventHandler* handler = (AstEventHandler*)alloc_ast_node(tp,
                AST_NODE_EVENT_HANDLER, handler_node, sizeof(AstEventHandler));
            handler->type = set_type_any(tp, ANY_STATEMENT);

            // event name
            TSNode event_node = ts_node_child_by_field_id(handler_node, FIELD_EVENT);
            if (!ts_node_is_null(event_node)) {
                StrView event_str = node_name_text(tp, event_node);
                handler->event = name_pool_create_strview(tp->name_pool, event_str);
                log_debug("event handler: %.*s", (int)event_str.length, event_str.str);
            }

            // handler scope (procedural)
            handler->vars = (NameScope*)pool_calloc(tp->pool, sizeof(NameScope));
            handler->vars->parent = ast_node->vars; // inherits template scope (state vars, params)
            handler->vars->is_proc = true; // handlers are procedural
            tp->current_scope = handler->vars;

            // optional event parameter
            TSNode param_node = ts_node_child_by_field_id(handler_node, FIELD_DECLARE);
            if (!ts_node_is_null(param_node)) {
                handler->param = build_param_expr(tp, param_node, false);
                // mark param as mutable in proc scope
                if (handler->param) {
                    NameEntry* entry = lookup_name_in_current_scope(tp, handler->param->name);
                    if (entry) entry->is_mutable = true;
                }
            }

            // handler body
            TSNode handler_body = ts_node_child_by_field_id(handler_node, FIELD_BODY);
            if (!ts_node_is_null(handler_body)) {
                handler->body = build_expr(tp, handler_body);
            }

            // restore scope to template scope
            tp->current_scope = ast_node->vars;

            if (prev_handler == NULL) {
                ast_node->handler = handler;
            } else {
                prev_handler->next_handler = handler;
            }
            prev_handler = handler;
        }
        has_node = ts_tree_cursor_goto_next_sibling(&cursor);
    }
    ts_tree_cursor_delete(&cursor);

    // restore parent scope
    tp->current_scope = ast_node->vars->parent;

    // register named template in scope (update pre-registered placeholder if exists)
    if (ast_node->name) {
        NameEntry* existing = lookup_name_in_current_scope(tp, ast_node->name);
        if (existing && existing->node && existing->node->node_type == AST_NODE_VIEW) {
            // update pre-registered placeholder from pass 1
            existing->node = (AstNode*)ast_node;
        } else {
            push_name(tp, (AstNamedNode*)ast_node, NULL);
        }
    }

    log_debug("end building view/edit template");
    return (AstNode*)ast_node;
}

AstNode* build_content(Transpiler* tp, TSNode list_node, bool flattern, bool is_global) {
    log_debug("build content, is_global=%d", is_global);
    AstListNode* ast_node = (AstListNode*)alloc_ast_node(tp, AST_NODE_CONTENT, list_node, sizeof(AstListNode));
    TypeList* type = (TypeList*)alloc_type(tp->pool, LMD_TYPE_ARRAY, sizeof(TypeList));
    ast_node->list_type = type;
    // Provisional: the real answer is chosen at the exits below. Counting here
    // would double-report every block whose type is later refined.
    ast_node->type = NULL;

    // Two-pass compilation for top-level functions (only when is_global is true)
    if (is_global) {
        log_debug("pass 1: scanning for top-level function declarations");
        // Pass 1: Scan and register all top-level function names with placeholder nodes
        TSNode child = ts_node_named_child(list_node, 0);
        while (!ts_node_is_null(child)) {
            TSSymbol symbol = ts_node_symbol(child);
            if (symbol == SYM_FUNC_STAM || symbol == SYM_FUNC_EXPR_STAM) {
                // Create minimal placeholder function node
                bool is_proc = false;
                TSNode kind = ts_node_child_by_field_id(child, FIELD_KIND);
                if (!ts_node_is_null(kind)) {
                    StrView kind_str = ts_node_source(tp, kind);
                    is_proc = strview_equal(&kind_str, "pn");
                }

                AstFuncNode* fn_node = (AstFuncNode*)alloc_ast_node(tp,
                    is_proc ? AST_NODE_PROC : AST_NODE_FUNC, child, sizeof(AstFuncNode));
                fn_node->type = alloc_type(tp->pool, LMD_TYPE_FUNC, sizeof(TypeFunc));
                TypeFunc* fn_type = (TypeFunc*)fn_node->type;
                fn_type->is_anonymous = false;
                fn_type->is_proc = is_proc;
                fn_type->returned = &TYPE_ANY;  // Safe default for forward references
                set_function_return_contract(fn_type,
                    is_proc ? &TYPE_ANY : &TYPE_ANY_NO_ERROR, false);

                // Initialize required fields to prevent crashes
                fn_node->param = NULL;
                fn_node->body = NULL;
                fn_node->vars = NULL;
                fn_node->captures = NULL;

                // Get function name and register it early
                TSNode fn_name_node = ts_node_child_by_field_id(child, FIELD_NAME);
                StrView name = node_name_text(tp, fn_name_node);
                fn_node->name = name_pool_create_strview(tp->name_pool, name);

                log_debug("pass 1: registering function placeholder '%.*s'", (int)fn_node->name->len, fn_node->name->chars);
                push_name(tp, (AstNamedNode*)fn_node, NULL);
            }
            else if (symbol == SYM_OBJECT_TYPE) {
                // pre-register object type placeholder so it can be forward-referenced
                TSNode obj_name_node = ts_node_child_by_field_id(child, FIELD_NAME);
                StrView obj_name = node_name_text(tp, obj_name_node);

                AstObjectTypeNode* obj_node = (AstObjectTypeNode*)alloc_ast_node(tp,
                    AST_NODE_OBJECT_TYPE, child, sizeof(AstObjectTypeNode));
                obj_node->name = name_pool_create_strview(tp->name_pool, obj_name);
                // create placeholder TypeType wrapping a minimal TypeObject
                TypeObject* placeholder_obj = (TypeObject*)pool_calloc(tp->pool, sizeof(TypeObject));
                placeholder_obj->type_id = LMD_TYPE_OBJECT;
                placeholder_obj->type_name.str = obj_node->name->chars;
                placeholder_obj->type_name.length = obj_node->name->len;
                TypeType* placeholder_tt = (TypeType*)alloc_type(tp->pool, LMD_TYPE_TYPE, sizeof(TypeType));
                placeholder_tt->type = (Type*)placeholder_obj;
                obj_node->type = (Type*)placeholder_tt;
                obj_node->base_type = NULL;
                obj_node->methods = NULL;
                obj_node->constraints = NULL;
                obj_node->item = NULL;

                // detect 'pub' field on object_type
                TSNode pub_node = ts_node_child_by_field_id(child, FIELD_PUB);
                obj_node->is_public = !ts_node_is_null(pub_node);

                log_debug("pass 1: registering object type placeholder '%.*s'",
                    (int)obj_name.length, obj_name.str);
                push_name(tp, (AstNamedNode*)obj_node, NULL);
            }
            else if (symbol == SYM_VIEW_STAM) {
                // pre-register named view/edit templates for forward references
                TSNode tmpl_name_node = ts_node_child_by_field_id(child, FIELD_NAME);
                if (!ts_node_is_null(tmpl_name_node)) {
                    StrView tmpl_name = node_name_text(tp, tmpl_name_node);
                    AstViewNode* view_node = (AstViewNode*)alloc_ast_node(tp,
                        AST_NODE_VIEW, child, sizeof(AstViewNode));
                    view_node->name = name_pool_create_strview(tp->name_pool, tmpl_name);
                    view_node->type = set_type_any(tp, ANY_STATEMENT);
                    view_node->pattern = NULL;
                    view_node->param = NULL;
                    view_node->body = NULL;
                    view_node->state = NULL;
                    view_node->handler = NULL;
                    view_node->vars = NULL;

                    log_debug("pass 1: registering view template placeholder '%.*s'",
                        (int)tmpl_name.length, tmpl_name.str);
                    push_name(tp, (AstNamedNode*)view_node, NULL);
                }
            }
            child = ts_node_next_named_sibling(child);
        }
    }

    // Pass 2: build all content (functions and other expressions)
    log_debug("pass 2: building content bodies");
    TSNode child = ts_node_named_child(list_node, 0);
    AstNode* prev_item = NULL;
    while (!ts_node_is_null(child)) {
        TSSymbol symbol = ts_node_symbol(child);
        AstNode* item = NULL;

        if (symbol == SYM_FUNC_STAM || symbol == SYM_FUNC_EXPR_STAM) {
            if (is_global) {
                // For global functions, look up the pre-registered placeholder
                TSNode fn_name_node = ts_node_child_by_field_id(child, FIELD_NAME);
                StrView name = node_name_text(tp, fn_name_node);
                NameEntry* entry = lookup_name(tp, name);

                if (entry && entry->node &&
                    (entry->node->node_type == AST_NODE_FUNC || entry->node->node_type == AST_NODE_PROC)) {
                    AstFuncNode* fn_node = (AstFuncNode*)entry->node;
                    log_debug("pass 2: completing function '%.*s'", (int)fn_node->name->len, fn_node->name->chars);

                    // Now build the complete function body
                    TypeFunc* fn_type = (TypeFunc*)fn_node->type;

                    // 'pub' flag
                    TSNode pub = ts_node_child_by_field_id(child, FIELD_PUB);
                    fn_type->is_public = !ts_node_is_null(pub);

                    // Build parameters
                    fn_node->vars = (NameScope*)pool_calloc(tp->pool, sizeof(NameScope));
                    fn_node->vars->parent = tp->current_scope;
                    fn_node->vars->is_proc = fn_type->is_proc;
                    tp->current_scope = fn_node->vars;

                    TSTreeCursor cursor = ts_tree_cursor_new(child);
                    bool has_node = ts_tree_cursor_goto_first_child(&cursor);
                    AstNamedNode* prev_param = NULL;
                    int param_count = 0;
                    int required_count = 0;
                    bool seen_optional = false;
                    bool has_declared_return_type = false;

                    while (has_node) {
                        TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
                        if (field_id == FIELD_DECLARE) {
                            TSNode param_child = ts_tree_cursor_current_node(&cursor);
                            AstNamedNode* param = build_param_expr(tp, param_child, false);

                            if (param == NULL) {
                                fn_type->is_variadic = true;
                                log_debug("function is variadic");
                                has_node = ts_tree_cursor_goto_next_sibling(&cursor);
                                continue;
                            }

                            TypeParam* param_type = (TypeParam*)param->type;
                            log_debug("got param: %.*s, optional=%d", (int)param->name->len, param->name->chars, param_type->is_optional);

                            if (param_type->is_optional) {
                                seen_optional = true;
                            } else {
                                if (seen_optional) {
                                    log_error("required parameter '%.*s' cannot follow optional parameter", (int)param->name->len);
                                }
                                required_count++;
                            }

                            if (prev_param == NULL) {
                                fn_node->param = param;
                                fn_type->param = param_type;
                            } else {
                                prev_param->next = (AstNode*)param;
                                ((TypeParam*)prev_param->type)->next = param_type;
                            }
                            prev_param = param;
                            param_count++;
                        }
                        else if (field_id == FIELD_TYPE) {
                            TSNode type_child = ts_tree_cursor_current_node(&cursor);
                            AstNode* type_expr = build_expr(tp, type_child);
                            has_declared_return_type = apply_function_return_type(
                                tp, fn_type, type_expr, type_child, true);
                        }
                        has_node = ts_tree_cursor_goto_next_sibling(&cursor);
                    }
                    ts_tree_cursor_delete(&cursor);

                    fn_type->param_count = param_count;
                    fn_type->required_param_count = required_count;
                    (void)validate_lambda_argument_limit(tp, child,
                        param_count + (fn_type->is_variadic ? 1 : 0),
                        "function formal");

                    // Build function body
                    TSNode fn_body_node = ts_node_child_by_field_id(child, FIELD_BODY);
                    bool saved_handler_body = tp->building_handler_body;
                    // A function body is compiled independently of the
                    // surrounding handler and cannot use its current error.
                    tp->building_handler_body = false;
                    fn_node->body = build_expr(tp, fn_body_node);
                    tp->building_handler_body = saved_handler_body;

                    fn_type->inferred_return = fn_type->is_proc ?
                        infer_procedural_return_type(tp, fn_node) :
                        fn_node->body ? fn_node->body->type : &TYPE_ANY;
                    // Earlier source can already have lowered a call through this
                    // placeholder's Item ABI. Preserve that carrier and retain the
                    // precise body result separately for later type enforcement.
                    if (!has_declared_return_type) {
                        fn_type->returned = &TYPE_ANY;
                    }
                    validate_function_return_contract(tp, fn_node, fn_type);

                    // Restore parent scope
                    tp->current_scope = fn_node->vars->parent;

                    // Analyze captures
                    NameScope* global_scope = find_global_scope(fn_node->vars->parent);
                    analyze_captures(tp, fn_node, global_scope);
                    validate_cross_frame_binding_reads(tp, fn_node);

                    item = (AstNode*)fn_node;
                    log_debug("pass 2: completed function '%.*s' with body=%p",
                        (int)fn_node->name->len, fn_node->name->chars, fn_node->body);
                } else {
                    log_error("Error: failed to find pre-registered function for '%.*s'",
                        (int)name.length, name.str);
                    // Fallback: build normally
                    item = build_func(tp, child, true, is_global);
                }
            } else {
                // For non-global functions, use original single-pass behavior
                item = build_func(tp, child, true, is_global);
            }
        } else if (symbol == SYM_OBJECT_TYPE && is_global) {
            // For global object types, look up the pre-registered placeholder and complete it
            TSNode obj_name_node = ts_node_child_by_field_id(child, FIELD_NAME);
            StrView obj_name = node_name_text(tp, obj_name_node);
            NameEntry* entry = lookup_name(tp, obj_name);

            if (entry && entry->node && entry->node->node_type == AST_NODE_OBJECT_TYPE) {
                AstObjectTypeNode* obj_node = (AstObjectTypeNode*)entry->node;
                TypeType* tt = (TypeType*)obj_node->type;
                TypeObject* obj_type = (TypeObject*)tt->type;
                // set struct_name for direct field access optimization (Phase 5/6)
                obj_type->struct_name = obj_node->name->chars;
                obj_type->is_trusted_contract = true;
                log_debug("pass 2: completing object type '%.*s'", (int)obj_name.length, obj_name.str);

                // get optional base type
                TSNode base_node = ts_node_child_by_field_id(child, FIELD_BASE);
                if (!ts_node_is_null(base_node)) {
                    obj_node->base_type = build_expr(tp, base_node);
                }

                // iterate children: fields, methods, constraints
                // Two-pass: first fields/constraints, then push fields into scope, then methods
                TSNode obj_child = ts_node_named_child(child, 0);
                AstNode* prev_field = NULL;  ShapeEntry* prev_entry = NULL;  int byte_offset = 0;
                AstNode* prev_constraint = NULL;

                // Resolve inheritance: copy parent fields into child shape (parent fields first)
                TypeObject* base_type_obj = resolve_base_type(tp, base_node, obj_type, &prev_entry, &byte_offset);

                // Pass 1: fields and constraints
                while (!ts_node_is_null(obj_child)) {
                    TSSymbol child_sym = ts_node_symbol(obj_child);
                    if (child_sym == SYM_ATTR) {
                        AstNode* field_item = (AstNode*)build_key_expr(tp, obj_child);
                        if (field_item) {
                            if (!prev_field) { obj_node->item = field_item; }
                            else { prev_field->next = field_item; }
                            prev_field = field_item;

                            ShapeEntry* shape_entry = (ShapeEntry*)pool_calloc(tp->pool, sizeof(ShapeEntry));
                            String* pooled_name = ((AstNamedNode*)field_item)->name;
                            StrView* name_view = (StrView*)pool_calloc(tp->pool, sizeof(StrView));
                            name_view->str = pooled_name->chars;
                            name_view->length = pooled_name->len;
                            shape_entry->name = name_view;
                            // unwrap TypeType to get the actual field data type
                            Type* field_type = field_item->type;
                            if (field_type && field_type->type_id == LMD_TYPE_TYPE) {
                                field_type = ((TypeType*)field_type)->type;
                            }
                            shape_entry->type = field_type;
                            shape_entry->byte_offset = byte_offset;
                            // read optional default value expression
                            TSNode default_node = ts_node_child_by_field_id(obj_child, FIELD_DEFAULT);
                            if (!ts_node_is_null(default_node)) {
                                shape_entry->default_value = build_expr(tp, default_node);
                            }
                            if (!prev_entry) { obj_type->shape = shape_entry; }
                            else { prev_entry->next = shape_entry; }
                            prev_entry = shape_entry;
                            obj_type->length++;
                            byte_offset += sizeof(void*);
                        }
                    } else if (child_sym == SYM_THAT_CONSTRAINT) {
                        // object-level constraint: enable implicit ~.name resolution
                        TSNode constraint_expr = ts_node_child_by_field_id(obj_child, FIELD_CONSTRAINT);
                        if (!ts_node_is_null(constraint_expr)) {
                            bool old_in_that = tp->in_that_clause;
                            tp->in_that_clause = true;
                            AstNode* constraint = build_expr(tp, constraint_expr);
                            tp->in_that_clause = old_in_that;
                            if (constraint) {
                                if (!prev_constraint) { obj_node->constraints = constraint; }
                                else { prev_constraint->next = constraint; }
                                prev_constraint = constraint;
                                obj_type->constraint = constraint;
                            }
                        }
                    }
                    obj_child = ts_node_next_named_sibling(obj_child);
                }

                build_object_type_methods(tp, obj_node, obj_type, base_type_obj, child,
                    prev_entry, byte_offset);

                // register in type_list (store TypeType wrapper for const_type() runtime access)
                arraylist_append(tp->type_list, tt);
                obj_type->type_index = tp->type_list->length - 1;

                item = (AstNode*)obj_node;
                log_debug("pass 2: completed object type '%.*s' with %d fields, %d methods",
                    (int)obj_name.length, obj_name.str, obj_type->length, obj_type->method_count);
            } else {
                log_error("pass 2: failed to find pre-registered object type '%.*s'", (int)obj_name.length, obj_name.str);
                item = build_object_type(tp, child);
            }
        } else {
            item = build_expr(tp, child);
        }

        if (item) {
            AstNode* item_tail = item;
            while (item_tail->next) item_tail = item_tail->next;
            if (!prev_item) {
                ast_node->item = item;
            }
            else {
                prev_item->next = item;
            }
            // Pattern multi-declarations return a linked declaration chain; preserve its tail or the next content item overwrites later patterns.
            prev_item = item_tail;
            type->length++;
        }
        // else comment or error
        child = ts_node_next_named_sibling(child);
    }

    log_debug("end building content item: %p, %ld", ast_node->item, type->length);
    if (flattern && type->length == 1) { return ast_node->item; }

    // TIG11 (IP4): type the block by what `transpile_content` actually
    // yields — a proc block's LAST value item, a single-item block's sole
    // value, a list only for a multi-item functional block.
    // yields. A single value item IS the block's value on every path, so the
    // block carries that item's type. The multi-item cases stay open: a
    // functional block builds a list whose element types are unproven, and a
    // procedural block's last-value type leaks its raw lane into the
    // for-expression collector (`[inf, inf, inf]` on
    // `proc_for_expr_content_proc`) — the IP5 carrier class again.
    // Type the block by what `transpile_content` yields: a procedural block's
    // LAST value item, a single-item block's sole value, a list only for a
    // multi-item functional block (whose element types are still unproven, so
    // it stays open). Safe now that the carrier oracle has a CONTENT case —
    // the block's CARRIER is a boxed Item, so this semantic type can no longer
    // be mistaken for a raw lane [Impl §17].
    bool proc_block = tp->current_scope && tp->current_scope->is_proc;
    if (type->length == 1 && ast_node->item && ast_node->item->type) {
        ast_node->type = ast_node->item->type;
    } else if (proc_block && type->length > 1) {
        AstNode* last_value = ast_node->item;
        while (last_value && last_value->next) last_value = last_value->next;
        ast_node->type = last_value && last_value->type ? last_value->type
            : set_type_any(tp, ANY_LIST);
    } else {
        ast_node->type = set_type_any(tp, ANY_LIST);
    }
    return ast_node;
}

AstNode* build_lit_node(Transpiler* tp, TSNode lit_node, bool quoted_value, TSSymbol symbol) {
    log_debug("build lit node");
    // string, symbol, binary, datetime, time
    AstPrimaryNode* ast_node = (AstPrimaryNode*)alloc_ast_node(tp, AST_NODE_PRIMARY, lit_node, sizeof(AstPrimaryNode));
    // TSNode val_node = quoted_value ? ts_node_named_child(lit_node, 0) : lit_node;
    ast_node->type = build_lit_string(tp, lit_node, symbol);
    return (AstNode*)ast_node;
}

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

static bool handler_is_statement_position(TSNode handler_node) {
    // A `_statement` ancestor is the unambiguous statement spelling; let/array
    // operands remain value-producing expressions.
    TSNode parent = ts_node_parent(handler_node);
    while (!ts_node_is_null(parent)) {
        if (strcmp(ts_node_type(parent), "_statement") == 0) return true;
        if (strcmp(ts_node_type(parent), "content") == 0 &&
                !ts_node_is_null(ts_node_next_named_sibling(handler_node))) return true;
        if (strcmp(ts_node_type(parent), "assign_expr") == 0 ||
                strcmp(ts_node_type(parent), "let_expr") == 0) return false;
        parent = ts_node_parent(parent);
    }
    return false;
}

static bool handler_is_value_context(TSNode handler_node) {
    TSNode parent = ts_node_parent(handler_node);
    while (!ts_node_is_null(parent)) {
        const char* type = ts_node_type(parent);
        if (strcmp(type, "assign_expr") == 0 || strcmp(type, "let_expr") == 0 ||
                strcmp(type, "array") == 0 || strcmp(type, "map_item") == 0 ||
                strcmp(type, "named_argument") == 0) return true;
        if (strcmp(type, "content") == 0 || strcmp(type, "_statement") == 0) return false;
        parent = ts_node_parent(parent);
    }
    return false;
}

static AstNode* build_handler(Transpiler* tp, TSNode handler_node,
        bool is_statement) {
    AstHandlerNode* ast_node = (AstHandlerNode*)alloc_ast_node(tp,
        is_statement ? AST_NODE_HANDLER_STAM : AST_NODE_HANDLER_EXPR,
        handler_node, sizeof(AstHandlerNode));
    ast_node->is_statement = is_statement;

    TSNode operand_node = ts_node_child_by_field_id(handler_node, FIELD_OPERAND);
    TSNode body_node = ts_node_child_by_field_id(handler_node, FIELD_BODY);
    TSNode value_body_node = ts_node_child_by_field_id(handler_node, FIELD_VALUE);
    ast_node->operand = build_expr(tp, operand_node);
    bool saved_handler_body = tp->building_handler_body;
    tp->building_handler_body = true;
    ast_node->body = build_expr(tp, body_node);
    tp->building_handler_body = saved_handler_body;
    if (!ts_node_is_null(value_body_node)) {
        // The normal arm does not own a current error. Restoring the saved
        // lexical state keeps an enclosing handler's `^` visible when nested.
        ast_node->value_body = build_expr(tp, value_body_node);
    }

    bool statement_position = is_statement || handler_is_statement_position(handler_node);
    bool value_context = handler_is_value_context(handler_node);
    if (!is_statement && value_context && handler_operand_is_proc(ast_node->operand)) {
        record_semantic_error(tp, handler_node, ERR_INVALID_EXPR_CONTEXT,
            "procedure call handlers are statement-only; use a value-producing fn call here");
    }
    if (!is_statement && statement_position) {
        is_statement = true;
        ast_node->is_statement = true;
        ast_node->node_type = AST_NODE_HANDLER_STAM;
    }

    if (!ast_node->operand) {
        ast_node->type = &TYPE_ERROR;
        return (AstNode*)ast_node;
    }
    // A handler in content position is the statement spelling when its
    // operand is a procedure call.  The grammar shares the braced form with
    // expressions so ordinary call statements do not become partial handler
    // nodes before a caret is present.
    if (!is_statement && handler_operand_is_proc(ast_node->operand)) {
        is_statement = true;
        ast_node->is_statement = true;
        ast_node->node_type = AST_NODE_HANDLER_STAM;
    }
    if (is_statement && !handler_operand_is_proc(ast_node->operand)) {
        record_semantic_error(tp, handler_node, ERR_INVALID_CALL,
            "statement error handler operand must be a procedure call");
    }

    if (is_statement) {
        ast_node->type = set_type_any(tp, ANY_STATEMENT);
        return (AstNode*)ast_node;
    }

    Type* body_type = ast_node->body && ast_node->body->type
        ? ast_node->body->type : &TYPE_ANY;
    if (ast_node->value_body) {
        Type* value_type = ast_node->value_body->type
            ? ast_node->value_body->type : &TYPE_ANY;
        // An explicit value arm replaces pass-through, so only the two arm
        // result types contribute to the handler's contextual type.
        ast_node->type = lambda_type_union_normalized(tp->pool, body_type, value_type);
    } else {
        // Without a value arm, success preserves the operand's non-error contract.
        Type* successful = lambda_type_remove_error(tp->pool, ast_node->operand->type);
        if (!successful) successful = set_type_any(tp, ANY_ERROR_RECOVERY);
        ast_node->type = lambda_type_union_normalized(tp->pool, successful, body_type);
    }
    return (AstNode*)ast_node;
}

static AstNode* build_propagate_expr(Transpiler* tp, TSNode propagate_node) {
    TSNode operand_node = ts_node_child_by_field_id(propagate_node, FIELD_OPERAND);
    AstNode* operand = build_expr(tp, operand_node);
    if (!operand) return NULL;

    // the postfix tier supplies a primary wrapper around some calls; inspect
    // the effective operand so a can_raise call is not misclassified as a
    // total value merely because the grammar wrapper is present.
    AstNode* effective_operand = boundary_unwrap_primary(operand);
    bool may_error = operand->type && lambda_type_accepts_error(operand->type);
    if (effective_operand && effective_operand->node_type == AST_NODE_CALL_EXPR) {
        AstCallNode* call = (AstCallNode*)effective_operand;
        call->propagate = true;
        may_error = may_error || call->can_raise;
        if (call->can_raise && call->function && call->function->type &&
                call->function->type->type_id == LMD_TYPE_FUNC) {
            // Preserve the established direct-call narrowing rule: only a
            // resolved function signature may expose a native success lane;
            // dynamic calls must keep their boxed outcome for the tag check.
            Type* success_type = function_success_result_type(
                (TypeFunc*)call->function->type);
            if (success_type) call->type = success_type;
        }
    }
    if (!may_error) {
        record_semantic_error(tp, propagate_node, ERR_SEMANTIC_ERROR,
            "postfix `^` used on an expression that does not return errors");
    }

    Type* success_type = lambda_type_remove_error(tp->pool, operand->type);
    if (!success_type) success_type = set_type_any(tp, ANY_ERROR_RECOVERY);
    if (effective_operand && effective_operand->node_type == AST_NODE_CALL_EXPR) {
        // retain the call node for the direct-call lowering path, but publish
        // the error-free result type so a surrounding Item boundary boxes the
        // native success lane instead of returning raw MIR bits as the script result.
        operand->type = success_type;
        // keep direct-call propagation on the call node so existing lowering
        // can route the error before any native success lane is entered.
        effective_operand->type = success_type;
        return effective_operand;
    }

    AstUnaryNode* ast_node = (AstUnaryNode*)alloc_ast_node(tp,
        AST_NODE_UNARY, propagate_node, sizeof(AstUnaryNode));
    ast_node->operand = operand;
    ast_node->op = OPERATOR_PROPAGATE;
    ast_node->prefix = false;
    ast_node->type = success_type;
    ast_node->op_str = ts_node_source(tp,
        ts_node_child_by_field_id(propagate_node, FIELD_PROPAGATE));
    return (AstNode*)ast_node;
}

// --- external type-pattern tokens -------------------------------------------
// The scanner hands the whole type sub-language over as one token; the hand
// parser (parse_type_pattern.cpp) turns the token's source text into the same
// AST-node/Type shapes the CST builders used to produce.

static AstNode* build_type_pattern_token(Transpiler* tp, TSNode node) {
    StrView src = ts_node_source(tp, node);
    AstNode* built = parse_type_pattern_text(tp, src.str, src.str + src.length, node);
    if (!built) {
        AstTypeNode* err = (AstTypeNode*)alloc_ast_node(tp, AST_NODE_TYPE, node, sizeof(AstTypeNode));
        err->type = (Type*)&LIT_TYPE_ERROR;
        return (AstNode*)err;
    }
    return built;
}

static AstNode* build_primary_type_pattern_token(Transpiler* tp, TSNode node) {
    StrView src = ts_node_source(tp, node);
    AstNode* built = parse_primary_type_text(tp, src.str, src.str + src.length, node);
    if (!built) {
        AstTypeNode* err = (AstTypeNode*)alloc_ast_node(tp, AST_NODE_TYPE, node, sizeof(AstTypeNode));
        err->type = (Type*)&LIT_TYPE_ERROR;
        return (AstNode*)err;
    }
    return built;
}

static AstNode* build_return_type_token(Transpiler* tp, TSNode node) {
    StrView src = ts_node_source(tp, node);
    AstNode* built = parse_return_type_text(tp, src.str, src.str + src.length, node);
    if (built) { return built; }
    return build_function_return_contract_node(tp, node, &TYPE_ERROR,
        &TYPE_ERROR, false);
}

static AstNode* build_return_type(Transpiler* tp, TSNode node) {
    // `return_type` remains the fielded grammar wrapper; its entire interior
    // is now one token, so parse the wrapper span without inspecting children.
    StrView src = ts_node_source(tp, node);
    AstNode* built = parse_return_type_text(tp, src.str, src.str + src.length, node);
    if (built) { return built; }
    return build_function_return_contract_node(tp, node, &TYPE_ERROR,
        &TYPE_ERROR, false);
}

static AstNode* build_view_pattern_token(Transpiler* tp, TSNode node) {
    StrView src = ts_node_source(tp, node);
    AstNode* built = parse_view_pattern_text(tp, src.str, src.str + src.length, node);
    if (built) { return built; }
    AstTypeNode* err = (AstTypeNode*)alloc_ast_node(tp, AST_NODE_TYPE, node,
        sizeof(AstTypeNode));
    err->type = (Type*)&LIT_TYPE_ERROR;
    return (AstNode*)err;
}

AstNode* build_expr(Transpiler* tp, TSNode expr_node) {
    // depth guard: bail (NULL, the existing error convention) before the recursion
    // can overflow the stack on deeply nested source.
    lam::RecursionGuard depth_guard(&tp->build_depth, MAX_BUILD_DEPTH);
    if (!depth_guard) {
        log_error("build_expr: expression nesting too deep (>%d) — aborting build", MAX_BUILD_DEPTH);
        return NULL;
    }
    // get the function name
    TSSymbol symbol = ts_node_symbol(expr_node);
    log_debug("build_expr: %s", ts_node_type(expr_node));
    switch (symbol) {
    // Wrapper nodes - unwrap and recurse to single child
    case SYM_EXPR: {
        // expr is a wrapper node with a single named child
        TSNode child = ts_node_named_child(expr_node, 0);
        if (ts_node_is_null(child)) {
            log_error("expr wrapper node has no child");
            return NULL;
        }
        return build_expr(tp, child);
    }
    case SYM_TYPE_EXPR:
    case SYM_ANNOTATION_TYPE: {
        // type pattern / annotation wrapper: a single named child
        TSNode child = ts_node_named_child(expr_node, 0);
        if (ts_node_is_null(child)) {
            log_error("type pattern wrapper node has no child");
            return NULL;
        }
        return build_expr(tp, child);
    }
    case SYM_PRIMARY_EXPR:
        return build_primary_expr(tp, expr_node);
    case SYM_MEMBER_EXPR:
        return build_member_expr(tp, expr_node);
    case SYM_CURRENT_PARENT_EXPR: {
        AstNavigationNode* nav = (AstNavigationNode*)alloc_ast_node(tp,
            AST_NODE_NAVIGATION_EXPR, expr_node, sizeof(AstNavigationNode));
        AstNode* current = alloc_ast_node(tp, AST_NODE_CURRENT_ITEM, expr_node, sizeof(AstNode));
        current->type = alloc_type(tp->pool, LMD_TYPE_ANY, sizeof(Type));
        nav->object = current;
        nav->root = false;
        nav->type = nav->object->type;
        return (AstNode*)nav;
    }
    case SYM_CALL_EXPR:
        return build_call_expr(tp, expr_node, symbol);
    case SYM_HANDLER_EXPR:
        return build_handler(tp, expr_node, false);
    case SYM_PROPAGATE_EXPR:
        return build_propagate_expr(tp, expr_node);
    case SYM_UNARY_EXPR:
        return build_unary_expr(tp, expr_node);
    case SYM_BINARY_EXPR:
        return build_binary_expr(tp, expr_node);
    case SYM_CURRENT_EXPR:
        return build_current_expr(tp, expr_node);
    case SYM_CURRENT_ERROR_EXPR:
        return build_current_error_expr(tp, expr_node);
    case SYM_LET_EXPR:
        return build_let_expr(tp, expr_node);
    case SYM_LET_STAM:  case SYM_TYPE_DEFINE:
        return build_let_and_type_stam(tp, expr_node, symbol);
    case SYM_FOR_EXPR:
        return build_for_expr(tp, expr_node);
    case SYM_FOR_STAM:
        return build_for_stam(tp, expr_node);
    case SYM_WHILE_STAM:
        return build_while_stam(tp, expr_node);
    case SYM_BREAK_STAM:
        return build_break_stam(tp, expr_node);
    case SYM_CONTINUE_STAM:
        return build_continue_stam(tp, expr_node);
    case SYM_RETURN_STAM:
        return build_return_stam(tp, expr_node);
    case SYM_RAISE_STAM:
        return build_raise_stam(tp, expr_node);
    case SYM_RAISE_EXPR:
        return build_raise_expr(tp, expr_node);
    case SYM_VAR_STAM:
        return build_var_stam(tp, expr_node);
    case SYM_ASSIGN_STAM:
        return build_assign_stam(tp, expr_node);
    case SYM_APPLY_STAM:
        return build_apply_stam(tp, expr_node);
    case SYM_IF_EXPR:
    case SYM_IF_STAM:
        return build_if_expr(tp, expr_node);
    case SYM_MATCH_EXPR:
        return build_match(tp, expr_node);
    case SYM_ASSIGN_EXPR:
        return build_assign_expr(tp, expr_node, false);  // standalone assign_expr is not a type definition
    case SYM_ARRAY:
        return build_array(tp, expr_node);
    case SYM_MAP: {
        // check for {TypeName} pattern: map with single identifier resolving to object type
        uint32_t named_count = ts_node_named_child_count(expr_node);
        if (named_count == 1) {
            TSNode only_child = ts_node_named_child(expr_node, 0);
            TSSymbol cs = ts_node_symbol(only_child);
            // unwrap primary_expr wrapper if present
            if (cs == SYM_PRIMARY_EXPR) {
                only_child = ts_node_named_child(only_child, 0);
                if (!ts_node_is_null(only_child)) cs = ts_node_symbol(only_child);
            }
            if (cs == SYM_IDENT) {
                StrView name = ts_node_source(tp, only_child);
                NameEntry* entry = lookup_name(tp, name);
                if (entry && entry->node && entry->node->type) {
                    Type* resolved = entry->node->type;
                    if (resolved->type_id == LMD_TYPE_TYPE) {
                        Type* inner = ((TypeType*)resolved)->type;
                        if (inner && inner->type_id == LMD_TYPE_OBJECT) {
                            // {TypeName} with all defaults — build as empty object literal
                            log_debug("build_map: detected {%.*s} as empty object literal", (int)name.length, name.str);
                            TypeObject* obj_type = (TypeObject*)inner;
                            AstObjectLiteralNode* obj_node = (AstObjectLiteralNode*)alloc_ast_node(tp,
                                AST_NODE_OBJECT_LITERAL, expr_node, sizeof(AstObjectLiteralNode));
                            obj_node->type_name = name_pool_create_strview(tp->name_pool, name);
                            obj_node->type = (Type*)obj_type;
                            obj_node->item = NULL;
                            return (AstNode*)obj_node;
                        }
                    }
                }
            }
        }
        return build_map(tp, expr_node);
    }
    case SYM_OBJECT_TYPE:
        return build_object_type(tp, expr_node);
    case SYM_ELEMENT:
        return build_elmt(tp, expr_node);
    case SYM_CONTENT:
        return build_content(tp, expr_node, true, false);
    // SYM_LIST removed — list syntax no longer exists
    case SYM_IDENT:
        return build_identifier(tp, expr_node);
    case SYM_FUNC_STAM:
        return build_func(tp, expr_node, true, false);
    case SYM_FUNC_EXPR_STAM:
        return build_func(tp, expr_node, true, false);
    case SYM_FUNC_EXPR:  // anonymous function
        return build_func(tp, expr_node, false, false);
    case SYM_VIEW_STAM:
        return build_view_stam(tp, expr_node);
    case SYM_STRING:  case SYM_SYMBOL:  case SYM_BINARY:
        return build_lit_node(tp, expr_node, true, symbol);
    case SYM_DATETIME: {
        AstPrimaryNode* dt_node = (AstPrimaryNode*)alloc_ast_node(tp, AST_NODE_PRIMARY, expr_node, sizeof(AstPrimaryNode));
        dt_node->type = build_lit_datetime(tp, expr_node, symbol);
        return (AstNode*)dt_node;
    }
    case SYM_NAMED_VALUE: {
        AstPrimaryNode* nv_node = (AstPrimaryNode*)alloc_ast_node(tp, AST_NODE_PRIMARY, expr_node, sizeof(AstPrimaryNode));
        nv_node->type = build_lit_named_value(tp, expr_node);
        return (AstNode*)nv_node;
    }
    case SYM_INT: {
        AstPrimaryNode* i_node = (AstPrimaryNode*)alloc_ast_node(tp, AST_NODE_PRIMARY, expr_node, sizeof(AstPrimaryNode));

        // Parse the integer value to determine if it fits in 32-bit or needs 64-bit
        StrView source = ts_node_source(tp, expr_node);

        // Create a null-terminated string for strtoll
        char* num_str = (char*)mem_alloc(source.length + 1, MEM_CAT_AST);
        memcpy(num_str, source.str, source.length);
        num_str[source.length] = '\0';

        int64_t value = 0;
        bool in_band = lambda_parse_int_literal(num_str, &value);
        mem_free(num_str);

        log_debug("SYM_INT: parsed value %lld, checking range", value);
        if (in_band) {
            log_debug("Using LIT_INT for value %lld", value);
            i_node->type = &LIT_INT;
        }
        else { // promote to float outside int's literal band (spec 4.2: +/-(2^53-1))
            log_debug("Using float for value %lld (outside int literal band)", value);
            i_node->type = build_lit_float(tp, expr_node);
        }
        return (AstNode*)i_node;
    }
    case SYM_FLOAT: {
        AstPrimaryNode* f_node = (AstPrimaryNode*)alloc_ast_node(tp, AST_NODE_PRIMARY, expr_node, sizeof(AstPrimaryNode));
        f_node->type = build_lit_float(tp, expr_node);
        return (AstNode*)f_node;
    }
    case SYM_IMAGINARY: {
        AstPrimaryNode* c_node = (AstPrimaryNode*)alloc_ast_node(tp, AST_NODE_PRIMARY, expr_node, sizeof(AstPrimaryNode));
        c_node->type = build_lit_imaginary(tp, expr_node);
        return (AstNode*)c_node;
    }
    case SYM_SIZED_INT: {
        AstPrimaryNode* si_node = (AstPrimaryNode*)alloc_ast_node(tp, AST_NODE_PRIMARY, expr_node, sizeof(AstPrimaryNode));
        si_node->type = build_lit_sized_integer(tp, expr_node);
        return (AstNode*)si_node;
    }
    case SYM_SIZED_FLOAT: {
        AstPrimaryNode* sf_node = (AstPrimaryNode*)alloc_ast_node(tp, AST_NODE_PRIMARY, expr_node, sizeof(AstPrimaryNode));
        sf_node->type = build_lit_sized_float(tp, expr_node);
        return (AstNode*)sf_node;
    }
    case SYM_BASE_TYPE:
        return build_base_type(tp, expr_node);
    // The type sub-language arrives as scanner tokens; the CST type node kinds
    // that used to be dispatched here no longer exist in the trimmed grammar.
    case SYM_CONSTRAINED_TYPE:
        return build_constrained_type(tp, expr_node);
    case sym_type_pattern_token:
    case sym_content_type_token:
    case sym_pattern_island_token:
        return build_type_pattern_token(tp, expr_node);
    case sym_primary_type_pattern_token:
        return build_primary_type_pattern_token(tp, expr_node);
    case sym_return_type_token:
        return build_return_type_token(tp, expr_node);
    case sym_view_pattern_token:
        return build_view_pattern_token(tp, expr_node);
    case SYM_RETURN_TYPE:
        return build_return_type(tp, expr_node);
    case SYM_NAMED_ARGUMENT:
        return build_named_argument(tp, expr_node);
    case SYM_IMPORT_MODULE:
        // already processed
        return NULL;
    case SYM_COMMENT:
        return NULL;
    default:
        log_debug("unknown syntax node: %s", ts_node_type(expr_node));
        return NULL;
    }
}

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
static bool record_existing_lambda_import_failure(Transpiler* tp, TSNode import_node,
        AstImportNode* ast_node, const char* resolved_path) {
    if (!file_exists(resolved_path)) return false;

    // Existing .ls modules that fail parse/compile must poison the importer;
    // otherwise the fallback path can let the caller compile and run anyway.
    record_semantic_error(tp, import_node, ERR_IMPORT_ERROR,
        "failed to import Lambda module '%.*s': existing source '%s' did not compile",
        (int)ast_node->module.length, ast_node->module.str, resolved_path);
    return true;
}

static bool load_hosted_module_import(Transpiler* tp, AstImportNode* ast_node,
                                      StrBuf* resolved_path) {
    if (!tp || !ast_node || !resolved_path || resolved_path->length < 3) return false;
    size_t base_length = resolved_path->length - 3; // remove the failed ".ls"
    resolved_path->length = base_length;
    resolved_path->str[base_length] = '\0';

    int extension_count = jube_hosted_extension_count();
    for (int i = 0; i < extension_count; i++) {
        const char* extension = jube_hosted_extension_at(i);
        if (!extension || !*extension) continue;
        strbuf_append_char(resolved_path, '.');
        strbuf_append_str(resolved_path, extension[0] == '.' ? extension + 1 : extension);

        Item hosted_ns = ItemNull;
        if (jube_load_hosted_module(tp->runtime, resolved_path->str,
                                    tp->reference, &hosted_ns)) {
            ast_node->script = (Script*)create_module_import_script(
                resolved_path->str, hosted_ns, tp->runtime);
            ast_node->is_cross_lang = true;
            return ast_node->script != NULL;
        }
        resolved_path->length = base_length;
        resolved_path->str[base_length] = '\0';
    }

    strbuf_append_str(resolved_path, ".ls");
    return false;
}
#endif

static void load_module_import_from_path(Transpiler* tp, TSNode import_node,
        AstImportNode* ast_node, StrBuf* buf, bool relative) {
#ifdef SIMPLE_SCHEMA_PARSER
    ast_node->script = nullptr;
#else
    ast_node->script = load_script(tp->runtime, buf->str, NULL, true);
    if (ast_node->script && !ast_node->script->ast_root) {
        ast_node->script = NULL; // stub from failed precompile — treat as absent
    }
#endif

    if (ast_node->script) {
        declare_module_import(tp, ast_node);
        return;
    }

#ifndef SIMPLE_SCHEMA_PARSER
    if (record_existing_lambda_import_failure(tp, import_node, ast_node, buf->str)) {
        return;
    }
    // .ls failed — try .js fallback for cross-language import
    buf->str[buf->length - 2] = 'j';
    buf->str[buf->length - 1] = 's';
    Item ns = load_js_module(tp->runtime, buf->str);
    if (ns.item != ItemNull.item) {
        ast_node->script = (Script*)create_module_import_script(
            buf->str, ns, tp->runtime);
        ast_node->is_cross_lang = true;
        if (ast_node->script) {
            declare_module_import(tp, ast_node);
        }
    } else if (load_hosted_module_import(tp, ast_node, buf)) {
        declare_module_import(tp, ast_node);
    } else if (relative) {
        log_error("Error: failed to load module '%.*s' (resolved: %s, from: %s)",
            (int)ast_node->module.length, ast_node->module.str, buf->str,
            tp->reference ? tp->reference : "<unknown>");
        fprintf(stderr, "Error: Failed to import module '%.*s'\n" // PRINTF_OK: user-facing CLI import error.
            "  Resolved path: %s\n  Importing script: %s\n",
            (int)ast_node->module.length, ast_node->module.str, buf->str,
            tp->reference ? tp->reference : "<unknown>");
    } else {
        log_error("Error: failed to load module '%.*s' (resolved: %s)",
            (int)ast_node->module.length, ast_node->module.str, buf->str);
        fprintf(stderr, "Error: Failed to import module '%.*s'\n" // PRINTF_OK: user-facing CLI import error.
            "  Resolved path: %s\n",
            (int)ast_node->module.length, ast_node->module.str, buf->str);
    }
#else
    (void)tp;
    (void)import_node;
    (void)buf;
    (void)relative;
#endif
}

AstNode* build_module_import(Transpiler* tp, TSNode import_node) {
    log_debug("build_module_import");
    AstImportNode* ast_node = (AstImportNode*)alloc_ast_node(
        tp, AST_NODE_IMPORT, import_node, sizeof(AstImportNode));
    ast_node->type = &TYPE_NULL;
    TSTreeCursor cursor = ts_tree_cursor_new(import_node);
    bool has_node = ts_tree_cursor_goto_first_child(&cursor);
    while (has_node) {
        // Check if the current node's field ID matches the target field ID
        TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
        TSNode child = ts_tree_cursor_current_node(&cursor);
        if (field_id == FIELD_ALIAS) {
            StrView alias = ts_node_source(tp, child);
            ast_node->alias = name_pool_create_strview(tp->name_pool, alias);
        }
        else if (field_id == FIELD_MODULE) {
            StrView module = ts_node_source(tp, child);
            ast_node->module = module;
        }
        has_node = ts_tree_cursor_goto_next_sibling(&cursor);
    }
    ts_tree_cursor_delete(&cursor);
    if (ast_node->module.length) {
        // Check for built-in module imports (e.g., `import math`, `import io`)
        // Three modes:
        //   1. No import statement: use as math.sqrt(x) — already works at call sites
        //   2. Global import: `import math;` — allows sqrt(x) without prefix
        //   3. Aliased import: `import m:math;` — allows m.sqrt(x) with alias prefix
        if (strview_equal(&ast_node->module, "math")) {
            if (ast_node->alias) {
                tp->builtin_alias_math = ast_node->alias;
                log_debug("built-in module aliased import: %.*s:%.*s",
                    (int)ast_node->alias->len, ast_node->alias->chars,
                    (int)ast_node->module.length, ast_node->module.str);
            } else {
                tp->builtin_import_math = true;
                log_debug("built-in module global import: math");
            }
            return NULL;  // no AST node needed: resolved at call sites
        }
        if (strview_equal(&ast_node->module, "io")) {
            if (ast_node->alias) {
                tp->builtin_alias_io = ast_node->alias;
                log_debug("built-in module aliased import: %.*s:%.*s",
                    (int)ast_node->alias->len, ast_node->alias->chars,
                    (int)ast_node->module.length, ast_node->module.str);
            } else {
                tp->builtin_import_io = true;
                log_debug("built-in module global import: io");
            }
            return NULL;  // no AST node needed: resolved at call sites
        }

#ifndef SIMPLE_SCHEMA_PARSER
        {
            char module_buf[128];
            if (ast_node->module.length < sizeof(module_buf)) {
                memcpy(module_buf, ast_node->module.str, ast_node->module.length);
                module_buf[ast_node->module.length] = '\0';
                jube_register_builtin_modules();
                const JubeModuleDef* module = jube_find_static_module(module_buf);
                if (module) {
                    StrView module_view = strview_from_cstr(module->name);
                    String* module_name = name_pool_create_strview(tp->name_pool, module_view);
                    add_jube_module_import(tp, module_name, ast_node->alias);
                    log_debug("Jube module import: %.*s%s%.*s",
                        ast_node->alias ? (int)ast_node->alias->len : 0,
                        ast_node->alias ? ast_node->alias->chars : "",
                        ast_node->alias ? ":" : "",
                        (int)module_view.length, module_view.str);
                    return NULL;  // no AST node needed: resolved at call sites
                }
            }
        }
#endif

        // Check if module is a bare URI (symbol literal like 'http://...')
        // This is a namespace-only import: import ns: 'url'
        if (ast_node->module.str[0] == '\'') {
            // strip quotes from URI
            StrView uri = { .str = ast_node->module.str + 1, .length = ast_node->module.length - 2 };
            if (ast_node->alias) {
                Target* target = (Target*)pool_calloc(tp->pool, sizeof(Target));
                String* uri_string = name_pool_create_strview(tp->name_pool, uri);
                target->original = uri_string->chars;
                add_namespace(tp, ast_node->alias, target);
                log_debug("bare URI namespace import: %.*s -> %.*s",
                    (int)ast_node->alias->len, ast_node->alias->chars,
                    (int)uri.length, uri.str);
            } else {
                log_error("bare URI import requires an alias: import alias: 'url'");
            }
            // bare URI imports do not load a script — return NULL so they
            // are not added to the AST child list (no code generation needed)
            return NULL;
        }
        else if (ast_node->module.str[0] == '.') {
            // relative import: resolve relative to importing script's directory
            const char* base_dir = tp->directory ? tp->directory : "./";
            log_debug("import base dir: %s", base_dir);
            StrBuf* buf = strbuf_new();
            strbuf_append_format(buf, "%s%.*s", base_dir,
                (int)ast_node->module.length - 1, ast_node->module.str + 1);
            char* ch = buf->str + buf->length - (ast_node->module.length - 1);
            while (*ch) { if (*ch == '.') *ch = '/';  ch++; }
            strbuf_append_str(buf, ".ls");
            ast_node->is_relative = true;
            load_module_import_from_path(tp, import_node, ast_node, buf, true);
            strbuf_free(buf);
        }
        else {
            // absolute import: resolve from lambda home (g_lambda_home).
            // e.g. "lambda.package.chart.chart" →
            //   dots → slashes: "lambda/package/chart/chart"
            //   strip the first segment ("lambda") and replace with g_lambda_home:
            //   "./lmd/package/chart/chart.ls"  (release)
            //   "./lambda/package/chart/chart.ls"  (dev)
            //
            // The first segment of the module path is always "lambda" by convention
            // (scripts write `import lambda.package.*`).  We strip it and prepend
            // g_lambda_home so the same source works in both dev and release layouts.
            log_debug("absolute import: %.*s", (int)ast_node->module.length, ast_node->module.str);
            StrBuf* buf = strbuf_new();

            // Convert dot-separated module name to a slash-separated path
            strbuf_append_format(buf, "./%.*s",
                (int)ast_node->module.length, ast_node->module.str);
            char* ch = buf->str + 2;  // skip "./"
            while (*ch) { if (*ch == '.') *ch = '/';  ch++; }
            strbuf_append_str(buf, ".ls");

            // Replace the first path segment ("lambda") with g_lambda_home.
            // buf->str is "./lambda/…" — find the end of that first segment.
            char* segment_end = strchr(buf->str + 2, '/');
            if (segment_end) {
                StrBuf* fixed = strbuf_new();
                // skip leading "./" in g_lambda_home if present
                const char* home = g_lambda_home;
                if (home[0] == '.' && home[1] == '/') home += 2;
                strbuf_append_str(fixed, "./");
                strbuf_append_str(fixed, home);
                strbuf_append_str(fixed, segment_end);  // "/package/…" remainder
                strbuf_free(buf);
                buf = fixed;
            }
            ast_node->is_relative = false;
            load_module_import_from_path(tp, import_node, ast_node, buf, false);
            strbuf_free(buf);
        }
    }
    return (AstNode*)ast_node;
}

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
// Pattern body now uses _type_expr (unified grammar), built via build_expr()
AstNode* build_string_pattern(Transpiler* tp, TSNode node, bool is_symbol, AstNode* prebuilt_as) {
    log_debug("build %s pattern definition", is_symbol ? "symbol" : "string");

    AstPatternDefNode* ast_node = (AstPatternDefNode*)
        alloc_ast_node(tp, is_symbol ? AST_NODE_SYMBOL_PATTERN : AST_NODE_STRING_PATTERN,
                       node, sizeof(AstPatternDefNode));
    ast_node->is_symbol = is_symbol;

    // Get pattern name
    TSNode name_node = ts_node_child_by_field_id(node, FIELD_NAME);
    StrView name_view = node_name_text(tp, name_node);
    ast_node->name = name_pool_create_strview(tp->name_pool, name_view);

    // Get pattern expression from the tagged island in type_assign's 'as' field.
    TSNode pattern_node = ts_node_child_by_field_id(node, FIELD_AS);
    if (ts_node_is_null(pattern_node)) {
        pattern_node = ts_node_child_by_field_id(node, FIELD_PATTERN);
    }
    ast_node->as = prebuilt_as ? prebuilt_as : build_expr(tp, pattern_node);
    if (ast_node->as && ast_node->as->node_type == AST_NODE_PATTERN_ISLAND) {
        AstPatternIslandNode* island = (AstPatternIslandNode*)ast_node->as;
        bool invalid_pattern = island->type == &TYPE_ERROR;
        ast_node->as = island->pattern;
        if (invalid_pattern) {
            ast_node->as = nullptr;
        }
    }

    // Type is pattern type
    TypePattern* pattern_type = (TypePattern*)alloc_type_kind(tp->pool, TYPE_KIND_PATTERN, sizeof(TypePattern));
    pattern_type->is_symbol = is_symbol;
    pattern_type->pattern_index = -1;  // Will be set during compilation
    pattern_type->re2 = nullptr;       // Will be compiled during transpilation
    pattern_type->source = nullptr;
    pattern_type->regex_source = nullptr;
    ast_node->type = ast_node->as ? (Type*)pattern_type : &TYPE_ERROR;

    // Register pattern name in current scope
    push_name(tp, (AstNamedNode*)ast_node, NULL);

    log_debug("built pattern definition: %.*s", (int)ast_node->name->len, ast_node->name->chars);
    return (AstNode*)ast_node;
}

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
            record_semantic_error(validation->tp, start->node, ERR_INVALID_EXPR_CONTEXT,
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
        record_semantic_error(validation->tp, node->node, ERR_INVALID_EXPR_CONTEXT,
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

AstNode* build_script(Transpiler* tp, TSNode script_node) {
    log_debug("build script");
    AstScript* ast_node = (AstScript*)alloc_ast_node(tp, AST_SCRIPT, script_node, sizeof(AstScript));
    tp->current_scope = ast_node->global_vars = (NameScope*)pool_calloc(tp->pool, sizeof(NameScope));

    // build the script body
    TSNode child = ts_node_named_child(script_node, 0);
    AstNode* prev = NULL;
    while (!ts_node_is_null(child)) {
        TSSymbol symbol = ts_node_symbol(child);
        AstNode* ast = NULL;
        switch (symbol) {
        case SYM_IMPORT_MODULE:
            // import module (also handles namespace imports: import ns: 'url')
            ast = build_module_import(tp, child);
            break;
        case SYM_CONTENT:
            ast = build_content(tp, child, true, true);
            break;
        case SYM_COMMENT:
            // skip comments
            break;
        default:
            log_debug("unknown script child: %s", ts_node_type(child));
        }
        if (ast) {
            if (!prev) ast_node->child = ast;
            else { prev->next = ast; }
            prev = ast;
        }
        child = ts_node_next_named_sibling(child);
    }
    if (ast_node->child) ast_node->type = ast_node->child->type;
    // Duplicate-definition recovery can re-link a placeholder into this list.
    // E228 only needs a complete clean AST, never that recovery representation.
    if (tp->error_count == 0) {
        for (AstNode* item = ast_node->child; item; item = item->next) {
            validate_top_level_enforcing_calls(tp, item);
            validate_top_level_cross_frame_binding_reads(tp, item);
        }
    }
    // Duplicate/invalid declarations can leave recovery placeholders linked
    // into the partial AST; concurrency analysis is valid only after a clean build.
    if (tp->error_count == 0) {
        analyze_lambda_concurrency(tp, ast_node);
    }
    log_debug("build script child: %p", ast_node->child);
    return (AstNode*)ast_node;
}

AstNode* build_repl_fragment(Transpiler* tp, TSNode document_node) {
    if (!tp || ts_node_is_null(document_node) ||
            ts_node_symbol(document_node) != sym_document) {
        return NULL;
    }
    // The existing module scope is the REPL's persistent environment. Do not
    // build a second AstScript/global scope: later inputs must resolve the
    // same NameEntry and therefore the same module slab slot (D7.2.1).
    NameScope* saved_scope = tp->current_scope;
    AstNode* first = NULL;
    AstNode* last = NULL;
    for (TSNode child = ts_node_named_child(document_node, 0);
            !ts_node_is_null(child); child = ts_node_next_named_sibling(child)) {
        AstNode* fragment = NULL;
        switch (ts_node_symbol(child)) {
        case SYM_CONTENT:
            fragment = build_content(tp, child, true, true);
            break;
        case SYM_IMPORT_MODULE:
            // Import cones are sealed when their module states are built. An
            // appended import needs the Script-scoped dependency transaction,
            // so P4 rejects it rather than give an existing session a mixed
            // lifetime graph (D7.2.2/D8.1.1v2).
            record_semantic_error(tp, child, ERR_SYNTAX_ERROR,
                "imports are not supported after a REPL session starts");
            break;
        case SYM_COMMENT:
            break;
        default:
            record_semantic_error(tp, child, ERR_SYNTAX_ERROR,
                "unsupported top-level REPL fragment '%s'", ts_node_type(child));
            break;
        }
        if (!fragment) continue;
        AstNode* tail = fragment;
        while (tail->next) tail = tail->next;
        if (!first) first = fragment;
        else last->next = fragment;
        last = tail;
    }
    tp->current_scope = saved_scope;
    if (tp->error_count == 0) {
        for (AstNode* item = first; item; item = item->next) {
            validate_top_level_enforcing_calls(tp, item);
            validate_top_level_cross_frame_binding_reads(tp, item);
        }
    }
    return first;
}
