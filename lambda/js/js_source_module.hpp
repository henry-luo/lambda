#pragma once
#include "../lambda-data.hpp"
#include "../runtime/module_registry.h"
#include "../io/resource_policy.h"
#include "../../lib/arraylist.h"

struct JsScript;
struct JsSourceModuleRecord;
struct JsSourceModuleOps;
struct JsPropertyDescriptor;

enum JsSourceModuleError { JS_SOURCE_MODULE_SYNTAX, JS_SOURCE_MODULE_REFERENCE,
    JS_SOURCE_MODULE_TYPE, JS_SOURCE_MODULE_RANGE, JS_SOURCE_MODULE_CAPABILITY };
enum JsSourceModuleStatus { JS_SOURCE_MODULE_NEW, JS_SOURCE_MODULE_PARSED,
    JS_SOURCE_MODULE_LINKED, JS_SOURCE_MODULE_INSTANTIATED,
    JS_SOURCE_MODULE_EVALUATING, JS_SOURCE_MODULE_ASYNC,
    JS_SOURCE_MODULE_EVALUATED, JS_SOURCE_MODULE_FAILED };
enum JsSourceBindingStatus { JS_SOURCE_BINDING_MISSING, JS_SOURCE_BINDING_FOUND,
    JS_SOURCE_BINDING_AMBIGUOUS, JS_SOURCE_BINDING_ERROR };

struct JsResolvedModuleBinding {
    JsSourceModuleRecord* module;
    NameEntry* entry;                 // defining declaration identity, including default cells
    void* slab;                       // existing precise LambdaModuleState owner
    uint32_t slot;
    bool namespace_binding;
};
struct JsSourceModuleImport {
    NameEntry* local;
    String* source;
    String* imported;
    bool namespace_binding;
    JsResolvedModuleBinding resolved;
};
struct JsSourceModuleExport {
    String* name;                     // null only for an export-star request
    String* source;
    String* imported;
    NameEntry* local;
    bool namespace_binding;
};
struct JsSourceModuleRequest {
    String* source;
    JsSourceModuleRecord* module;
};
struct JsSourceModuleExportCell {
    String* name;
    JsResolvedModuleBinding binding;
};

// Parse/admit and link never run guest bodies. All units retain one immutable
// frontend policy; source declarations stay in their original retained AST.
struct JsSourceModuleOps {
    Item (*prepare)(void* owner, JsSourceModuleRecord* record,
        const char* source, size_t length);
    Item (*plan)(void* owner, JsSourceModuleRecord* record);
    bool (*local_binding)(void* owner, JsSourceModuleRecord* record,
        NameEntry* entry, JsResolvedModuleBinding* result);
    Item (*link)(void* owner, JsSourceModuleRecord* record);
    Item (*instantiate)(void* owner, JsSourceModuleRecord* record);
    Item (*evaluate)(void* owner, JsSourceModuleRecord* record, bool async_frame);
    bool (*same_value)(Item left, Item right);
    Item (*import_meta_create)(void* owner, JsSourceModuleRecord* record);
    Item (*error)(void* owner, JsSourceModuleError kind, const char* message);
    Runtime* (*ensure_runtime)(void* owner);
    Item (*to_string)(void* owner, Item value);
    Item (*error_value)(void* owner, Item error);
    Item (*import_value)(void* owner, Item value);
};
struct JsSourceModuleRecord {
    Runtime* runtime;
    ModuleDescriptor* descriptor;
    const JsSourceModuleOps* ops;
    void* owner;
    JsScript* script;                 // retained by the frontend realm, through heap finalization
    void* unit;
    ArrayList* requests;              // ordered JsSourceModuleRequest*
    ArrayList* imports;               // JsSourceModuleImport*
    ArrayList* exports;               // JsSourceModuleExport*
    ArrayList* export_cells;          // sorted, unambiguous JsSourceModuleExportCell*
    NameEntry default_entry;          // synthetic environment cell, never inserted into guest scope
    AstNode* default_declaration;
    JsSourceModuleStatus status;
    Item import_meta;                 // lazily registered exact root, never an unowned scalar
    bool import_meta_registered;
    bool linking;
    bool evaluation_on_stack;
    uint32_t evaluation_index;
    uint32_t evaluation_ancestor;
    uint64_t async_evaluation_order;
    bool async_evaluation;
    // foreign exports retain the canonical language namespace and use the
    // same precise module cells and JS namespace exotic as source exports.
    bool foreign_module;
    NameEntry* foreign_entries;
    void* foreign_module_state;
    Item foreign_js_namespace;
    bool foreign_namespace_registered;
};

// Existing resolution/source-cache services, promoted without a second loader.
void jm_resolve_module_path(const char* base, const char* specifier, int length,
    char* output, int capacity);
bool jm_resolve_document_module_path(Runtime*, const char* reference,
    const char* specifier, int length, char* output, int capacity);
const char* jm_document_script_base_url(Runtime* runtime, const char* script_reference);
void js_resolve_script_module_path(Runtime* runtime, const char* reference,
    const char* specifier, int length, char* output, int capacity);
char* js_load_script_source_from_cache(const char* path, const char* profile,
    const char* execution_mode, bool module_mode, size_t* out_length,
    InputResourcePolicy policy = INPUT_RESOURCE_ALLOW_NETWORK);
bool jm_path_is_lambda_source(const char* path);
Item js_load_lambda_module(Runtime* runtime, const char* path);

Item js_module_load_link(Runtime* runtime, const JsSourceModuleOps* ops, void* owner,
    const char* path, const char* source, size_t length, JsSourceModuleRecord** output);
Item js_module_evaluate(JsSourceModuleRecord* record);
bool js_module_set_source_ops(const JsSourceModuleOps* ops, void* owner);
Item js_source_dynamic_import(Item specifier, const char* reference,
    const JsSourceModuleOps* ops, void* owner);
// completion is either a normal null or an encoded abrupt lane, including throw null.
void js_module_source_complete(JsSourceModuleRecord* record, Item completion);
JsSourceBindingStatus js_module_resolve_binding(JsSourceModuleRecord* record,
    String* name, JsResolvedModuleBinding* output);
Item js_module_binding_get(const JsResolvedModuleBinding* binding);
Item js_module_environment_binding(JsSourceModuleRecord* record, NameEntry* entry,
    Item value, bool write);
Item js_module_namespace_create(JsSourceModuleRecord* record);

enum JsModuleNamespaceOperation { JS_MODULE_NAMESPACE_GET, JS_MODULE_NAMESPACE_HAS,
    JS_MODULE_NAMESPACE_SET, JS_MODULE_NAMESPACE_DELETE };
JsSourceModuleRecord* js_module_namespace_record(Item object);
Item js_module_namespace_property(Item owner, Item key, JsModuleNamespaceOperation operation);
Item js_module_namespace_get_own(Item owner, Item key, JsPropertyDescriptor* descriptor);
Item js_module_namespace_define(Item owner, Item key, const JsPropertyDescriptor* descriptor);
Item js_module_namespace_project(Item owner, int64_t projection);
Item js_module_import_meta(JsSourceModuleRecord* record);
