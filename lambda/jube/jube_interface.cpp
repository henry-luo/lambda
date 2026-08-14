// DOM3 interface compiler + record-driven host-object dispatch.
//
// A module declares its script-facing shape once, in Lambda type syntax
// (JubeModuleDef.interface_decl), and supplies behavior as binding tables
// (JubeTypeBinding / JubeMemberBind). At registration this file parses the
// declaration with the real Lambda grammar (Tree-sitter), cross-checks it
// against the bindings, and compiles per-type member records plus a
// content-hashed name index dual-keyed on snake_case and camelCase. The
// generic host-object paths consult these records, so a fully declared type
// needs no hand-written dispatch at all.

#include "jube_interface.h"
#include "jube_registry.h"
#include "../lambda.hpp"
#include "../js/js_runtime_internal.hpp"
#include "../../lib/log.h"
#include "../../lib/mem.h"
#include "../../lib/hashmap.h"
#include <tree_sitter/api.h>
#include <string.h>
#include <stdlib.h>

// engine entry points not exposed through public headers
extern "C" TSParser* lambda_parser(void);
extern "C" TSTree* lambda_parse_source(TSParser* parser, const char* source_code);
extern __thread EvalContext* context;
// raw VMap backing-store access (vmap.cpp); bypasses host-object routing so
// the generic expando store cannot recurse back into member dispatch
extern "C" Item vmap_backing_get(VMap* vm, Item key);
extern "C" bool vmap_backing_set(VMap* vm, Item key, Item value);

// ============================================================================
// Compiled records
// ============================================================================

typedef enum JubeMemberKind {
    JUBE_MEMBER_FIELD = 0,   // data property backed by get/set handlers
    JUBE_MEMBER_METHOD,      // fn-typed member backed by a call handler
    JUBE_MEMBER_CONST,       // default literal, no binding
} JubeMemberKind;

typedef struct JubeMemberRecord JubeMemberRecord;
struct JubeMemberRecord {
    const JubeMemberBind* bind;   // NULL for constants
    char* snake_name;             // declared spelling (owned copy)
    char* camel_name;             // derived or js_name override (owned copy)
    uint8_t kind;                 // JubeMemberKind
    bool readonly;                // no set binding and not reflected
    bool enumerable;              // fields only; aliases/constants/methods opt out
    bool can_raise;
    int arity;                    // methods: declared parameter count
    const JubeTypeDef* result_type; // resolved field/method result type, if declared
    int64_t const_int;            // CONST int payload
    char* const_str;              // CONST string payload (owned copy, or NULL)
    bool const_is_str;
    Item method_fn;               // cached function object (lazy, GC-rooted)
    bool method_fn_rooted;
};

typedef struct JubeTypeRecord {
    const JubeTypeDef* type;      // brand: vmap->host_type of instances
    const JubeTypeBinding* binding;
    struct JubeTypeRecord* base_record;
    int type_slot;                // process-stable registry slot
    int family_root_slot;         // base slot shared by derived records
    JubeMemberRecord* members;    // stable array, declaration order
    int member_count;
    HashMap* index;               // content-hashed name -> one member record
    Item prototype;               // lazy per-type prototype object (GC-rooted)
    bool prototype_rooted;
} JubeTypeRecord;

typedef struct JubeTypeIndexEntry {
    const void* type;
    JubeTypeRecord* record;
} JubeTypeIndexEntry;

#define JUBE_TYPE_RECORD_CAPACITY 64
static JubeTypeRecord* s_type_records[JUBE_TYPE_RECORD_CAPACITY];
static int s_type_record_count = 0;
static HashMap* s_type_index = NULL;

typedef struct JubeMemberIndexEntry {
    const char* chars;
    uint32_t len;
    JubeMemberRecord* rec;
} JubeMemberIndexEntry;

static uint64_t jube_member_index_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const JubeMemberIndexEntry* entry = (const JubeMemberIndexEntry*)item;
    return hashmap_sip(entry->chars, entry->len, seed0, seed1);
}

static int jube_member_index_compare(const void* a, const void* b, void* udata) {
    (void)udata;
    const JubeMemberIndexEntry* ea = (const JubeMemberIndexEntry*)a;
    const JubeMemberIndexEntry* eb = (const JubeMemberIndexEntry*)b;
    if (ea->len != eb->len) return 1;
    return memcmp(ea->chars, eb->chars, ea->len);
}

static uint64_t jube_type_index_hash(const void* item, uint64_t seed0,
                                     uint64_t seed1) {
    const JubeTypeIndexEntry* entry = (const JubeTypeIndexEntry*)item;
    return hashmap_sip(&entry->type, sizeof(entry->type), seed0, seed1);
}

static int jube_type_index_compare(const void* a, const void* b, void* udata) {
    (void)udata;
    const JubeTypeIndexEntry* ea = (const JubeTypeIndexEntry*)a;
    const JubeTypeIndexEntry* eb = (const JubeTypeIndexEntry*)b;
    return ea->type == eb->type ? 0 : 1;
}

// ============================================================================
// Small helpers
// ============================================================================

static Item jube_undefined_item(void) {
    return (Item){.item = ITEM_JS_UNDEFINED};
}

static bool jube_item_key_chars(Item key, const char** chars, uint32_t* len) {
    TypeId type_id = get_type_id(key);
    if (!is_text_type_id(type_id)) return false;
    const char* key_chars = key.get_chars();
    if (!key_chars) return false;
    *chars = key_chars;
    *len = key.get_len();
    return true;
}

static Item jube_name_item(const char* name) {
    return (Item){.item = s2it(heap_create_name(name))};
}

static char* jube_strndup(const char* src, size_t len) {
    // Interface metadata outlives individual JS heaps and is released after
    // memtrack may change mode, so it cannot use a phase-bound tracked block.
    char* copy = (char*)malloc(len + 1);
    if (!copy) return NULL;
    memcpy(copy, src, len);
    copy[len] = '\0';
    return copy;
}

// snake_case -> camelCase; returns owned copy (identity copy when no '_')
static char* jube_derive_camel(const char* snake) {
    size_t len = strlen(snake);
    // Matches jube_strndup(): member metadata is released outside a JS heap
    // lifetime, so its ownership must not depend on memtrack's current mode.
    char* out = (char*)malloc(len + 1);
    if (!out) return NULL;
    size_t oi = 0;
    for (size_t i = 0; i < len; i++) {
        if (snake[i] == '_' && i + 1 < len && snake[i + 1] >= 'a' && snake[i + 1] <= 'z') {
            out[oi++] = (char)(snake[i + 1] - 'a' + 'A');
            i++;
        } else {
            out[oi++] = snake[i];
        }
    }
    out[oi] = '\0';
    return out;
}

static JubeTypeRecord* jube_record_for_type(const void* host_type) {
    if (!host_type || !s_type_index) return NULL;
    JubeTypeIndexEntry probe = {host_type, NULL};
    const JubeTypeIndexEntry* found =
        (const JubeTypeIndexEntry*)hashmap_get(s_type_index, &probe);
    return found ? found->record : NULL;
}

static JubeTypeRecord* jube_record_for(Item receiver) {
    if (get_type_id(receiver) != LMD_TYPE_VMAP || !receiver.vmap ||
            !receiver.vmap->host_type) {
        return NULL;
    }
    return jube_record_for_type(receiver.vmap->host_type);
}

void* jube_host_identity(Item item) {
    JubeTypeRecord* trec = jube_record_for(item);
    if (!trec) return NULL;
    return item.vmap->host_data;
}

// resolve a key against the type's compiled ordinal index.
static JubeMemberRecord* jube_resolve_member(JubeTypeRecord* trec, Item receiver, Item key) {
    (void)receiver;
    const char* chars = NULL;
    uint32_t len = 0;
    if (!trec->index || !jube_item_key_chars(key, &chars, &len)) return NULL;
    JubeMemberIndexEntry probe = {chars, len, NULL};
    const JubeMemberIndexEntry* found =
        (const JubeMemberIndexEntry*)hashmap_get(trec->index, &probe);
    if (!found) return NULL;
    return found->rec;
}

// ============================================================================
// Generic expando store: a plain JS object kept in the wrapper's own lazy
// VMap backing store under a reserved key. GC marks backing entries, so the
// expando object needs no explicit rooting and dies with the wrapper.
// ============================================================================

#define JUBE_EXPANDO_KEY "__jube_expando__"

static Item jube_expando_object(Item receiver, bool create) {
    VMap* vm = receiver.vmap;
    Item reserved = jube_name_item(JUBE_EXPANDO_KEY);
    Item existing = vmap_backing_get(vm, reserved);
    if (get_type_id(existing) == LMD_TYPE_MAP && existing.map) return existing;
    if (!create) return jube_undefined_item();
    const JubeHostAPI* host = jube_internal_host_api();
    Item obj = host->value->new_object();
    if (!vmap_backing_set(vm, reserved, obj)) return jube_undefined_item();
    return obj;
}

static bool jube_expando_value_present(Item value) {
    return value.item != ITEM_JS_UNDEFINED && value.item != ITEM_NULL && value.item != 0;
}

// ============================================================================
// Method function objects: one per member record. A typed payload body owns
// every arity, and the process-stable record pointer is not an Item/GC edge.
// ============================================================================

static Item jube_tramp_invoke(Item fn_item, Item this_value, Item* args,
        int argc, uint64_t* result_home) {
    (void)result_home;
    JsFunction* fn = get_type_id(fn_item) == LMD_TYPE_FUNC
        ? (JsFunction*)fn_item.function : NULL;
    JubeMemberRecord* rec = fn
        ? (JubeMemberRecord*)(uintptr_t)fn->native_target.bits : NULL;
    Item out = jube_undefined_item();
    if (rec && rec->bind && rec->bind->call) {
        rec->bind->call(this_value, args, argc, &out);
    }
    return out;
}

static Item jube_member_js_method_item(JubeMemberRecord* rec) {
    if (rec->method_fn_rooted) return rec->method_fn;
    const JubeHostAPI* host = jube_internal_host_api();
    int arity = rec->arity;
    if (arity < 0) arity = 0;
    if (arity > 8) arity = 8;
    Item fn_item = js_new_native_payload_function(jube_tramp_invoke,
        (uint64_t)(uintptr_t)rec, arity);
    host->script->set_function_name(fn_item, jube_name_item(rec->camel_name));
    rec->method_fn = fn_item;
    host->gc->register_root(&rec->method_fn.item);
    rec->method_fn_rooted = true;
    return rec->method_fn;
}

static Item jube_lambda_method_invoke(Item env_item, Item* args, int argc) {
    Item* env = (Item*)env_item.item;
    Item out = jube_undefined_item();
    JubeMemberRecord* rec = env
        ? (JubeMemberRecord*)(uintptr_t)env[1].item : NULL;
    if (rec && rec->bind && rec->bind->call) {
        rec->bind->call(env[0], args, argc, &out);
    }
    return out;
}

// Keep the captured environment Item-typed: the shared hosted dispatcher
// rejects the old incompatible void* closure-prefix prototype.
static Item jube_lambda_method_tramp_0(Item env) {
    return jube_lambda_method_invoke(env, NULL, 0);
}
static Item jube_lambda_method_tramp_1(Item env, Item a0) {
    Item args[] = {a0};
    return jube_lambda_method_invoke(env, args, 1);
}
static Item jube_lambda_method_tramp_2(Item env, Item a0, Item a1) {
    Item args[] = {a0, a1};
    return jube_lambda_method_invoke(env, args, 2);
}
static Item jube_lambda_method_tramp_3(Item env, Item a0, Item a1, Item a2) {
    Item args[] = {a0, a1, a2};
    return jube_lambda_method_invoke(env, args, 3);
}
static Item jube_lambda_method_tramp_4(Item env, Item a0, Item a1, Item a2, Item a3) {
    Item args[] = {a0, a1, a2, a3};
    return jube_lambda_method_invoke(env, args, 4);
}
static Item jube_lambda_method_tramp_5(Item env, Item a0, Item a1, Item a2, Item a3,
                                       Item a4) {
    Item args[] = {a0, a1, a2, a3, a4};
    return jube_lambda_method_invoke(env, args, 5);
}
static Item jube_lambda_method_tramp_6(Item env, Item a0, Item a1, Item a2, Item a3,
                                       Item a4, Item a5) {
    Item args[] = {a0, a1, a2, a3, a4, a5};
    return jube_lambda_method_invoke(env, args, 6);
}
static Item jube_lambda_method_tramp_7(Item env, Item a0, Item a1, Item a2, Item a3,
                                       Item a4, Item a5, Item a6) {
    Item args[] = {a0, a1, a2, a3, a4, a5, a6};
    return jube_lambda_method_invoke(env, args, 7);
}

static void* const s_jube_lambda_method_tramps[8] = {
    (void*)jube_lambda_method_tramp_0, (void*)jube_lambda_method_tramp_1,
    (void*)jube_lambda_method_tramp_2, (void*)jube_lambda_method_tramp_3,
    (void*)jube_lambda_method_tramp_4, (void*)jube_lambda_method_tramp_5,
    (void*)jube_lambda_method_tramp_6, (void*)jube_lambda_method_tramp_7,
};

static Item jube_member_lambda_method_item(Item receiver, JubeMemberRecord* rec) {
    int arity = rec->arity;
    if (arity < 0) arity = 0;
    if (arity > 7) arity = 7;
    RootFrame roots(2);
    Rooted<Item> rooted_receiver(roots, receiver);
    Rooted<Function*> rooted_fn(roots, (Function*)NULL);
    // Lambda projection reads run outside js_input, so Jube methods cannot use
    // JS function allocation there. The stable record pointer is executable
    // payload, not an Item edge; closure_field_count traces only the receiver.
    Function* fn = (Function*)heap_calloc(sizeof(Function), LMD_TYPE_FUNC);
    if (!fn) return jube_undefined_item();
    rooted_fn.set(fn);
    // The data-zone environment must acquire a rooted GC owner before another
    // allocation can collect or compact it. Allocating the Function first
    // closes the former ownerless-env window between these two allocations.
    Item* env = (Item*)heap_data_calloc(sizeof(Item) * 2);
    if (!env) return jube_undefined_item();
    fn = rooted_fn.get();
    env[0] = rooted_receiver.get();
    env[1] = (Item){.item = (uint64_t)(uintptr_t)rec};
    fn->type_id = LMD_TYPE_FUNC;
    fn->entry_abi = FN_ENTRY_ABI_HOST_ADAPTER;
    fn->arity = (uint8_t)arity;
    fn->fn_type = NULL;
    fn->ptr = (fn_ptr)s_jube_lambda_method_tramps[arity];
    fn->closure_env = env;
    fn->name = rec->snake_name;
    fn->closure_field_count = 1;
    return (Item){.function = fn};
}

static Item jube_member_method_item(Item receiver, JubeMemberRecord* rec) {
    // Pure Lambda evaluators have no JS capsule; do not dereference derived JS
    // TLS merely to choose the host-language method wrapper.
    if (js_active_runtime_state && js_input && js_input->pool) {
        return jube_member_js_method_item(rec);
    }
    return jube_member_lambda_method_item(receiver, rec);
}

static Item jube_member_const_item(JubeMemberRecord* rec) {
    if (rec->const_is_str) {
        return rec->const_str ? jube_name_item(rec->const_str) : jube_undefined_item();
    }
    return (Item){.item = i2it(rec->const_int)};
}

// ============================================================================
// Dispatch entry points
// ============================================================================

bool jube_type_has_interface(const JubeTypeDef* type) {
    return jube_record_for_type((const void*)type) != NULL;
}

static JubeTypeRecord* jube_record_for_query(const JubeTypeDef* type, int ordinal) {
    JubeTypeRecord* trec = jube_record_for_type((const void*)type);
    if (!trec || ordinal < 0 || ordinal >= trec->member_count) return NULL;
    return trec;
}

static uint64_t jube_digest_mix(uint64_t hash, uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
    return hash;
}

static uint64_t jube_digest_text(uint64_t hash, const char* text) {
    size_t len = text ? strlen(text) : 0;
    hash = jube_digest_mix(hash, (uint64_t)len);
    if (len > 0) hash = jube_digest_mix(hash, hashmap_xxhash3(text, len, 0, 0));
    return hash;
}

extern "C" uint64_t jube_interface_registry_digest(void) {
    uint64_t hash = 0xcbf29ce484222325ULL;
    hash = jube_digest_mix(hash, (uint64_t)JUBE_ABI_VERSION);
    hash = jube_digest_mix(hash, (uint64_t)s_type_record_count);
    for (int i = 0; i < s_type_record_count; i++) {
        JubeTypeRecord* trec = s_type_records[i];
        if (!trec) continue;
        hash = jube_digest_text(hash, trec->type ? trec->type->name : NULL);
        hash = jube_digest_mix(hash, (uint64_t)trec->family_root_slot);
        hash = jube_digest_mix(hash, (uint64_t)trec->member_count);
        for (int ordinal = 0; ordinal < trec->member_count; ordinal++) {
            JubeMemberRecord* rec = &trec->members[ordinal];
            hash = jube_digest_text(hash, rec->snake_name);
            hash = jube_digest_text(hash, rec->camel_name);
            hash = jube_digest_mix(hash, (uint64_t)rec->kind);
            hash = jube_digest_mix(hash, (uint64_t)rec->arity);
            hash = jube_digest_mix(hash, rec->can_raise ? 1 : 0);
            hash = jube_digest_text(hash,
                rec->result_type ? rec->result_type->name : NULL);
            hash = jube_digest_text(hash, rec->bind ? rec->bind->name : NULL);
        }
    }
    return hash;
}

extern "C" const JubeTypeDef* jube_iface_type_by_name(const char* name,
                                                       uint32_t len) {
    if (!name || len == 0) return NULL;
    for (int i = 0; i < s_type_record_count; i++) {
        JubeTypeRecord* trec = s_type_records[i];
        if (!trec || !trec->type || !trec->type->name) continue;
        if (strlen(trec->type->name) == len && memcmp(trec->type->name, name, len) == 0) {
            return trec->type;
        }
    }
    return NULL;
}

extern "C" int jube_iface_type_slot(const JubeTypeDef* type) {
    JubeTypeRecord* trec = jube_record_for_type((const void*)type);
    return trec ? trec->family_root_slot : -1;
}

extern "C" int jube_member_count(const JubeTypeDef* type) {
    JubeTypeRecord* trec = jube_record_for_type((const void*)type);
    return trec ? trec->member_count : -1;
}

extern "C" const char* jube_member_name_at(const JubeTypeDef* type, int ordinal,
                                             bool camel_case) {
    JubeTypeRecord* trec = jube_record_for_query(type, ordinal);
    if (!trec) return NULL;
    return camel_case ? trec->members[ordinal].camel_name :
                        trec->members[ordinal].snake_name;
}

extern "C" int jube_member_ordinal(const JubeTypeDef* type, const char* name,
                                    uint32_t len) {
    JubeTypeRecord* trec = jube_record_for_type((const void*)type);
    if (!trec || !name || len == 0) return -1;
    for (int i = 0; i < trec->member_count; i++) {
        JubeMemberRecord* rec = &trec->members[i];
        if ((strlen(rec->snake_name) == len && memcmp(rec->snake_name, name, len) == 0) ||
            (strlen(rec->camel_name) == len && memcmp(rec->camel_name, name, len) == 0)) {
            return i;
        }
    }
    return -1;
}

extern "C" uint8_t jube_member_kind_at(const JubeTypeDef* type, int ordinal) {
    JubeTypeRecord* trec = jube_record_for_query(type, ordinal);
    return trec ? trec->members[ordinal].kind : UINT8_MAX;
}

extern "C" bool jube_member_can_raise_at(const JubeTypeDef* type, int ordinal) {
    JubeTypeRecord* trec = jube_record_for_query(type, ordinal);
    return trec ? trec->members[ordinal].can_raise : false;
}

extern "C" int jube_member_arity_at(const JubeTypeDef* type, int ordinal) {
    JubeTypeRecord* trec = jube_record_for_query(type, ordinal);
    return trec ? trec->members[ordinal].arity : -1;
}

extern "C" const JubeTypeDef* jube_member_result_type_at(const JubeTypeDef* type,
                                                          int ordinal) {
    JubeTypeRecord* trec = jube_record_for_query(type, ordinal);
    return trec ? trec->members[ordinal].result_type : NULL;
}

static JubeMemberRecord* jube_record_at_guarded(Item receiver, int slot,
                                                uint32_t ordinal,
                                                JubeTypeRecord** out_trec) {
    if (out_trec) *out_trec = NULL;
    JubeTypeRecord* trec = jube_record_for(receiver);
    if (!trec || slot < 0 || trec->family_root_slot != slot ||
            ordinal >= (uint32_t)trec->member_count) return NULL;
    if (out_trec) *out_trec = trec;
    return &trec->members[ordinal];
}

static int jube_dispatch_get_record(Item receiver, JubeMemberRecord* rec, Item* out) {
    if (!rec || !out) return 0;
    switch (rec->kind) {
    case JUBE_MEMBER_CONST:
        *out = jube_member_const_item(rec);
        return 1;
    case JUBE_MEMBER_METHOD:
        // A method may have an availability handler for a feature-gated
        // surface; the handler owns the semantic predicate, not the kernel.
        if (rec->bind && rec->bind->get && !rec->bind->get(receiver, out)) return 0;
        *out = jube_member_method_item(receiver, rec);
        return 1;
    default:
        if (rec->bind && rec->bind->get && rec->bind->get(receiver, out)) return 1;
        return 0;
    }
}

static int jube_dispatch_set_record(Item receiver, JubeTypeRecord* trec,
                                    JubeMemberRecord* rec, Item value, Item* out) {
    if (!trec || !rec || !out) return 0;
    if (rec->readonly || rec->kind != JUBE_MEMBER_FIELD) {
        if (trec->binding && trec->binding->named_set && receiver.vmap->host_data &&
                trec->binding->named_set(receiver, jube_name_item(rec->camel_name),
                                          value, out)) return 1;
        *out = value;
        return 1;
    }
    if (rec->bind && rec->bind->set && rec->bind->set(receiver, value, out)) return 1;
    return 0;
}

static Item jube_type_prototype_for(JubeTypeRecord* trec) {
    if (trec->prototype_rooted) return trec->prototype;
    const JubeHostAPI* host = jube_internal_host_api();
    // adopt the module's existing prototype object when one is seeded, so
    // constructor .prototype identity (instanceof) survives the conversion.
    // A seed returning a non-map means "this type has no prototype" (style
    // objects): record ItemNull, publish nothing, register no root.
    if (trec->binding && trec->binding->prototype_seed) {
        trec->prototype = trec->binding->prototype_seed();
        if (get_type_id(trec->prototype) != LMD_TYPE_MAP) {
            trec->prototype = ItemNull;
            trec->prototype_rooted = true;
            return trec->prototype;
        }
    } else {
        trec->prototype = host->value->new_object();
    }
    if (trec->base_record) {
        // DOM4 subtypes expose the declared interface hierarchy through the
        // ordinary prototype chain while their compiled member rows retain
        // the inherited prefix used by ordinal dispatch.
        Item parent = jube_type_prototype_for(trec->base_record);
        if (get_type_id(parent) == LMD_TYPE_MAP) {
            js_set_prototype(trec->prototype, parent);
        }
    }
    host->gc->register_root(&trec->prototype.item);
    trec->prototype_rooted = true;
    // publish method function objects onto the prototype: scripts read them as
    // Range.prototype.setStart (IDL shape / .length probes), and instance reads
    // must return the identical Item (range.setStart === Range.prototype.setStart)
    if (get_type_id(trec->prototype) == LMD_TYPE_MAP) {
        for (int i = 0; i < trec->member_count; i++) {
            JubeMemberRecord* rec = &trec->members[i];
            if (rec->kind != JUBE_MEMBER_METHOD) continue;
            host->value->property_set(trec->prototype, jube_name_item(rec->camel_name),
                                      jube_member_js_method_item(rec));
        }
    }
    return trec->prototype;
}

// per-JS-runtime reset: prototype seeds read the CURRENT global constructor's
// .prototype, and batch runs recreate globals per script — cached prototypes
// and method items must drop so the next access rebuilds against the new
// runtime's globals (roots unregister while the old heap is still alive).
extern "C" void jube_interface_runtime_reset(void) {
    const JubeHostAPI* host = jube_internal_host_api();
    for (int i = 0; i < s_type_record_count; i++) {
        JubeTypeRecord* trec = s_type_records[i];
        if (!trec) continue;
        if (trec->prototype_rooted) {
            host->gc->unregister_root(&trec->prototype.item);
            trec->prototype = ItemNull;
            trec->prototype_rooted = false;
        }
        for (int j = 0; j < trec->member_count; j++) {
            JubeMemberRecord* rec = &trec->members[j];
            if (rec->method_fn_rooted) {
                host->gc->unregister_root(&rec->method_fn.item);
                rec->method_fn = ItemNull;
                rec->method_fn_rooted = false;
            }
        }
    }
}

extern "C" Item jube_type_prototype(const JubeTypeDef* type) {
    JubeTypeRecord* trec = jube_record_for_type((const void*)type);
    if (!trec) return ItemNull;
    return jube_type_prototype_for(trec);
}

extern "C" int jube_member_get_by_ordinal(Item receiver, int slot,
                                           uint32_t ordinal, Item* out) {
    JubeTypeRecord* trec = NULL;
    JubeMemberRecord* rec = jube_record_at_guarded(receiver, slot, ordinal, &trec);
    if (!trec || !rec || !out) return 0;
    if (!receiver.vmap->host_data) {
        // the native payload is gone, but the wrapper identity remains a valid husk.
        *out = jube_undefined_item();
        return 1;
    }
    return jube_dispatch_get_record(receiver, rec, out);
}

extern "C" int jube_member_set_by_ordinal(Item receiver, int slot,
                                           uint32_t ordinal, Item value, Item* out) {
    JubeTypeRecord* trec = NULL;
    JubeMemberRecord* rec = jube_record_at_guarded(receiver, slot, ordinal, &trec);
    if (!trec || !rec || !out) return 0;
    if (!receiver.vmap->host_data) {
        *out = jube_undefined_item();
        return 1;
    }
    return jube_dispatch_set_record(receiver, trec, rec, value, out);
}

extern "C" int jube_member_call_by_ordinal(Item receiver, int slot,
                                            uint32_t ordinal, Item* args, int argc,
                                            Item* out) {
    JubeTypeRecord* trec = NULL;
    JubeMemberRecord* rec = jube_record_at_guarded(receiver, slot, ordinal, &trec);
    if (!trec || !rec || !out || rec->kind != JUBE_MEMBER_METHOD ||
            !rec->bind || !rec->bind->call) return 0;
    if (!receiver.vmap->host_data) return 0;
    return rec->bind->call(receiver, args, argc, out) ? 1 : 0;
}

int jube_member_get(Item receiver, Item key, Item* out) {
    JubeTypeRecord* trec = jube_record_for(receiver);
    if (!trec || !out) return 0;
    if (!receiver.vmap->host_data) {
        // neutered husk (post-release / document teardown): every read degrades
        // to undefined instead of touching the freed native payload
        *out = jube_undefined_item();
        return 1;
    }
    JubeMemberRecord* rec = jube_resolve_member(trec, receiver, key);
    if (rec && jube_dispatch_get_record(receiver, rec, out)) {
        return 1;
    }
    // array-index reads (sheet[0]) resolve through the indexed hook; JS index
    // keys arrive as ints or all-digit strings depending on the access path
    if (trec->binding && trec->binding->indexed_get) {
        int64_t index = -1;
        TypeId key_type = get_type_id(key);
        if (key_type == LMD_TYPE_INT) {
            index = it2i(key);
        } else {
            const char* digits = NULL;
            uint32_t dlen = 0;
            if (jube_item_key_chars(key, &digits, &dlen) && dlen > 0) {
                index = 0;
                for (uint32_t i = 0; i < dlen; i++) {
                    if (digits[i] < '0' || digits[i] > '9') { index = -1; break; }
                    index = index * 10 + (digits[i] - '0');
                }
            }
        }
        if (index >= 0 && trec->binding->indexed_get(receiver, index, out)) return 1;
    }
    if (trec->binding && trec->binding->named_get &&
            trec->binding->named_get(receiver, key, out)) {
        return 1;
    }
    const char* key_chars = NULL;
    uint32_t key_len = 0;
    if (jube_item_key_chars(key, &key_chars, &key_len) && key_len == 9 &&
            memcmp(key_chars, "__proto__", 9) == 0) {
        *out = jube_type_prototype_for(trec);
        return 1;
    }
    Item expando = jube_expando_object(receiver, false);
    if (get_type_id(expando) == LMD_TYPE_MAP) {
        Item value = jube_internal_host_api()->value->property_get(expando, key);
        if (jube_expando_value_present(value)) {
            *out = value;
            return 1;
        }
    }
    // prototype-chain fallthrough: patched prototype members and inherited
    // Object.prototype methods must stay reachable on declared types
    Item proto = jube_type_prototype_for(trec);
    if (get_type_id(proto) == LMD_TYPE_MAP) {
        *out = jube_internal_host_api()->value->property_get(proto, key);
        return 1;
    }
    *out = jube_undefined_item();
    return 1;
}

int jube_member_projected_get(Item receiver, Item key, Item* out) {
    JubeTypeRecord* trec = jube_record_for(receiver);
    if (!trec || !out || !receiver.vmap->host_data) return 0;
    JubeMemberRecord* rec = jube_resolve_member(trec, receiver, key);
    if (!rec) return 0;
    // Host own-property descriptors need declared record members only; full
    // jube_member_get may now continue into named hooks and expando fallback.
    return jube_dispatch_get_record(receiver, rec, out);
}

int jube_member_set(Item receiver, Item key, Item value, Item* out) {
    JubeTypeRecord* trec = jube_record_for(receiver);
    if (!trec || !out) return 0;
    if (!receiver.vmap->host_data) {
        *out = jube_undefined_item();
        return 1;
    }
    JubeMemberRecord* rec = jube_resolve_member(trec, receiver, key);
    if (rec && jube_dispatch_set_record(receiver, trec, rec, value, out)) {
        return 1;
    }
    if (trec->binding && trec->binding->named_set &&
            trec->binding->named_set(receiver, key, value, out)) {
        return 1;
    }
    Item expando = jube_expando_object(receiver, true);
    if (get_type_id(expando) == LMD_TYPE_MAP) {
        *out = jube_internal_host_api()->value->property_set(expando, key, value);
        return 1;
    }
    *out = value;
    return 1;
}

int jube_member_has(Item receiver, Item key, Item* out) {
    JubeTypeRecord* trec = jube_record_for(receiver);
    if (!trec || !out) return 0;
    bool present = jube_resolve_member(trec, receiver, key) != NULL;
    if (!present && trec->binding && trec->binding->object_has &&
            receiver.vmap->host_data &&
            trec->binding->object_has(receiver, key, out)) {
        return 1;
    }
    if (!present && trec->binding && trec->binding->named_has &&
            receiver.vmap->host_data &&
            trec->binding->named_has(receiver, key, out)) {
        return 1;
    }
    if (!present && receiver.vmap->host_data) {
        Item expando = jube_expando_object(receiver, false);
        if (get_type_id(expando) == LMD_TYPE_MAP) {
            Item value = jube_internal_host_api()->value->property_get(expando, key);
            present = jube_expando_value_present(value);
        }
    }
    *out = (Item){.item = b2it(present)};
    return 1;
}

int jube_member_delete(Item receiver, Item key, Item* out) {
    JubeTypeRecord* trec = jube_record_for(receiver);
    if (!trec || !out) return 0;
    if (jube_resolve_member(trec, receiver, key)) {
        *out = (Item){.item = b2it(false)};
        return 1;
    }
    if (trec->binding && trec->binding->object_delete && receiver.vmap->host_data &&
            trec->binding->object_delete(receiver, key, out)) {
        return 1;
    }
    // open-name members (CSS properties on style objects) refuse deletion,
    // matching the projected-property non-configurable contract
    if (trec->binding && trec->binding->named_has && receiver.vmap->host_data) {
        Item present = ItemNull;
        if (trec->binding->named_has(receiver, key, &present) &&
                present.item == b2it(true)) {
            *out = (Item){.item = b2it(false)};
            return 1;
        }
    }
    if (receiver.vmap->host_data) {
        Item expando = jube_expando_object(receiver, false);
        if (get_type_id(expando) == LMD_TYPE_MAP) {
            *out = jube_internal_host_api()->script->reflect_delete_property(expando, key);
            return 1;
        }
    }
    *out = (Item){.item = b2it(true)};
    return 1;
}

static Item jube_make_data_descriptor(Item value, bool writable,
        bool configurable) {
    const JubeHostAPI* host = jube_internal_host_api();
    RootFrame roots(2);
    Rooted<Item> value_root(roots, value);
    Rooted<Item> descriptor_root(roots, host->value->new_object());
    // D5.1.1: descriptor shape transitions may collect, so both the object
    // under construction and its potentially managed value need exact roots.
    host->value->property_set(descriptor_root.get(), jube_name_item("value"),
                              value_root.get());
    host->value->property_set(descriptor_root.get(), jube_name_item("writable"),
                              (Item){.item = b2it(writable)});
    host->value->property_set(descriptor_root.get(), jube_name_item("enumerable"),
                              (Item){.item = b2it(true)});
    host->value->property_set(descriptor_root.get(), jube_name_item("configurable"),
                              (Item){.item = b2it(configurable)});
    return descriptor_root.get();
}

int jube_member_descriptor(Item receiver, Item key, Item* out) {
    JubeTypeRecord* trec = jube_record_for(receiver);
    if (!trec || !out) return 0;
    RootFrame roots(4);
    Rooted<Item> receiver_root(roots, receiver);
    Rooted<Item> key_root(roots, key);
    Rooted<Item> value_root(roots, ItemNull);
    Rooted<Item> expando_root(roots, ItemNull);
    if (trec->binding && trec->binding->object_descriptor && receiver.vmap->host_data &&
            trec->binding->object_descriptor(receiver_root.get(), key_root.get(), out)) {
        return 1;
    }
    JubeMemberRecord* rec = jube_resolve_member(trec, receiver_root.get(), key_root.get());
    if (rec && rec->kind != JUBE_MEMBER_METHOD && receiver_root.get().vmap->host_data) {
        Item member_value = jube_undefined_item();
        jube_member_get(receiver_root.get(), key_root.get(), &member_value);
        value_root.set(member_value);
        *out = jube_make_data_descriptor(value_root.get(), !rec->readonly, false);
        return 1;
    }
    if (receiver_root.get().vmap->host_data) {
        expando_root.set(jube_expando_object(receiver_root.get(), false));
        if (get_type_id(expando_root.get()) == LMD_TYPE_MAP) {
            value_root.set(jube_internal_host_api()->value->property_get(
                expando_root.get(), key_root.get()));
            if (jube_expando_value_present(value_root.get())) {
                *out = jube_make_data_descriptor(value_root.get(), true, true);
                return 1;
            }
        }
    }
    *out = jube_undefined_item();
    return 1;
}

static void jube_append_expando_keys(const JubeHostAPI* host, Item keys, Item expando,
                                     Rooted<Item>* expando_keys, Rooted<Item>* name);

int jube_member_own_keys(Item receiver, Item* out) {
    JubeTypeRecord* trec = jube_record_for(receiver);
    if (!trec || !out) return 0;
    RootFrame roots(5);
    Rooted<Item> rooted_receiver(roots, receiver);
    Rooted<Item> rooted_keys(roots, ItemNull);
    Rooted<Item> rooted_name(roots, ItemNull);
    Rooted<Item> rooted_expando(roots, ItemNull);
    Rooted<Item> rooted_expando_keys(roots, ItemNull);
    if (trec->binding && trec->binding->object_own_keys && receiver.vmap->host_data &&
            trec->binding->object_own_keys(rooted_receiver.get(), out)) {
        return 1;
    }
    const JubeHostAPI* host = jube_internal_host_api();
    rooted_keys.set(host->value->array_new(0));
    for (int i = 0; i < trec->member_count; i++) {
        JubeMemberRecord* rec = &trec->members[i];
        if (!rec->enumerable) continue;
        rooted_name.set(jube_name_item(rec->camel_name));
        host->value->array_push(rooted_keys.get(), rooted_name.get());
    }
    if (rooted_receiver.get().vmap->host_data) {
        rooted_expando.set(jube_expando_object(rooted_receiver.get(), false));
        jube_append_expando_keys(host, rooted_keys.get(), rooted_expando.get(),
            &rooted_expando_keys, &rooted_name);
    }
    *out = rooted_keys.get();
    return 1;
}

static bool jube_array_has_string_key(Item keys, const char* chars) {
    if (!chars || get_type_id(keys) != LMD_TYPE_ARRAY || !keys.array) return false;
    size_t len = strlen(chars);
    for (int64_t i = 0; i < keys.array->length; i++) {
        Item existing = keys.array->items[i];
        if (get_type_id(existing) != LMD_TYPE_STRING) continue;
        String* str = it2s(existing);
        if (str && str->len == len && memcmp(str->chars, chars, len) == 0) return true;
    }
    return false;
}

static void jube_append_expando_keys(const JubeHostAPI* host, Item keys, Item expando,
                                     Rooted<Item>* expando_keys, Rooted<Item>* name) {
    if (get_type_id(expando) != LMD_TYPE_MAP) return;
    expando_keys->set(host->script->reflect_own_keys(expando));
    if (get_type_id(expando_keys->get()) != LMD_TYPE_ARRAY || !expando_keys->get().array) return;
    for (int64_t i = 0; i < expando_keys->get().array->length; i++) {
        // reload the source array through its root because array_push may collect.
        Array* arr = expando_keys->get().array;
        name->set(arr->items[i]);
        host->value->array_push(keys, name->get());
    }
}

int jube_member_projection_keys(Item receiver, Item* out) {
    JubeTypeRecord* trec = jube_record_for(receiver);
    if (!trec || !out) return 0;
    RootFrame roots(5);
    Rooted<Item> rooted_receiver(roots, receiver);
    Rooted<Item> rooted_keys(roots, ItemNull);
    Rooted<Item> rooted_name(roots, ItemNull);
    Rooted<Item> rooted_expando(roots, ItemNull);
    Rooted<Item> rooted_expando_keys(roots, ItemNull);
    const JubeHostAPI* host = jube_internal_host_api();
    rooted_keys.set(host->value->array_new(0));
    for (int i = 0; i < trec->member_count; i++) {
        JubeMemberRecord* rec = &trec->members[i];
        if (rec->kind != JUBE_MEMBER_FIELD) continue;
        if (jube_array_has_string_key(rooted_keys.get(), rec->snake_name)) continue;
        // Lambda projection iteration exposes declared snake_case fields; JS
        // own-key enumeration remains WebIDL/camelCase through object_own_keys.
        rooted_name.set(jube_name_item(rec->snake_name));
        host->value->array_push(rooted_keys.get(), rooted_name.get());
    }
    if (rooted_receiver.get().vmap->host_data) {
        rooted_expando.set(jube_expando_object(rooted_receiver.get(), false));
        jube_append_expando_keys(host, rooted_keys.get(), rooted_expando.get(),
            &rooted_expando_keys, &rooted_name);
    }
    *out = rooted_keys.get();
    return 1;
}

int jube_member_prototype(Item receiver, Item* out) {
    JubeTypeRecord* trec = jube_record_for(receiver);
    if (!trec || !out) return 0;
    if (trec->binding && trec->binding->object_prototype && receiver.vmap->host_data &&
            trec->binding->object_prototype(receiver, out)) {
        // DOM nodes need receiver-specific Element/Text/Document prototypes;
        // a static prototype_seed cannot preserve that WebIDL identity.
        return 1;
    }
    *out = jube_type_prototype_for(trec);
    return 1;
}

// ============================================================================
// Interface compilation
// ============================================================================

typedef struct JubeParsedMember {
    char* name;
    bool is_method;
    int arity;
    bool can_raise;
    char* result_type_name;
    bool has_default;
    bool default_is_str;
    int64_t default_int;
    char* default_str;
} JubeParsedMember;

#define JUBE_PARSE_MEMBER_CAPACITY 256

static const JubeTypeDef* jube_module_type_by_name(const JubeModuleDef* module,
                                                   const char* name);

static void jube_member_record_release_owned(JubeMemberRecord* record) {
    if (!record) return;
    if (record->snake_name) free(record->snake_name);
    if (record->camel_name) free(record->camel_name);
    if (record->const_str) free(record->const_str);
}

static void jube_member_record_init(JubeMemberRecord* record,
                                    const JubeParsedMember* parsed,
                                    const JubeMemberBind* bind,
                                    const JubeModuleDef* module) {
    if (!record || !parsed) return;
    jube_member_record_release_owned(record);
    memset(record, 0, sizeof(*record));
    record->bind = bind;
    record->snake_name = jube_strndup(parsed->name, strlen(parsed->name));
    record->camel_name = (bind && bind->js_name)
        ? jube_strndup(bind->js_name, strlen(bind->js_name))
        : jube_derive_camel(record->snake_name);
    if (parsed->is_method) {
        record->kind = JUBE_MEMBER_METHOD;
        record->arity = parsed->arity;
        record->can_raise = parsed->can_raise;
        record->result_type = parsed->result_type_name
            ? jube_module_type_by_name(module, parsed->result_type_name) : NULL;
        record->readonly = true;
    } else if (!bind) {
        record->kind = JUBE_MEMBER_CONST;
        record->readonly = true;
        record->const_int = parsed->default_int;
        record->const_is_str = parsed->default_is_str;
        record->const_str = parsed->default_str
            ? jube_strndup(parsed->default_str, strlen(parsed->default_str)) : NULL;
        record->result_type = parsed->result_type_name
            ? jube_module_type_by_name(module, parsed->result_type_name) : NULL;
    } else {
        record->kind = JUBE_MEMBER_FIELD;
        record->readonly = !bind->set && !bind->reflect_attr;
        record->result_type = parsed->result_type_name
            ? jube_module_type_by_name(module, parsed->result_type_name) : NULL;
    }
    record->enumerable = record->kind == JUBE_MEMBER_FIELD &&
        !(bind && (bind->flags & JUBE_MEMBER_NON_ENUMERABLE));
}

static char* jube_node_text(const char* source, TSNode node) {
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);
    if (end < start) return NULL;
    return jube_strndup(source + start, end - start);
}

static bool jube_node_is(TSNode node, const char* type) {
    return strcmp(ts_node_type(node), type) == 0;
}

// count fn_param children and detect '^' in the return type of a fn_type node
static void jube_parse_fn_type(const char* source, TSNode fn_node, int* arity,
                               bool* can_raise, char** result_type_name) {
    *arity = 0;
    *can_raise = false;
    if (result_type_name) *result_type_name = NULL;
    uint32_t count = ts_node_named_child_count(fn_node);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(fn_node, i);
        if (jube_node_is(child, "fn_param")) (*arity)++;
        if (jube_node_is(child, "return_type")) {
            char* text = jube_node_text(source, child);
            if (text) {
                char* raise_marker = strchr(text, '^');
                if (raise_marker) {
                    *can_raise = true;
                    *raise_marker = '\0';
                }
                if (result_type_name) *result_type_name = text;
                else free(text);
            }
        }
    }
}

static bool jube_parse_default_literal(const char* source, TSNode value_node,
                                       JubeParsedMember* member) {
    char* text = jube_node_text(source, value_node);
    if (!text) return false;
    if (jube_node_is(value_node, "string")) {
        size_t len = strlen(text);
        // strip the surrounding quotes the grammar keeps in the literal text
        if (len >= 2 && (text[0] == '"' || text[0] == '\'')) {
            member->default_str = jube_strndup(text + 1, len - 2);
            free(text);
        } else {
            member->default_str = text;
        }
        member->default_is_str = true;
        member->has_default = true;
        return true;
    }
    member->default_int = strtoll(text, NULL, 10);
    member->default_is_str = false;
    member->has_default = true;
    free(text);
    return true;
}

static void jube_free_parsed_members(JubeParsedMember* members, int count) {
    for (int i = 0; i < count; i++) {
        if (members[i].name) free(members[i].name);
        if (members[i].default_str) free(members[i].default_str);
        if (members[i].result_type_name) free(members[i].result_type_name);
    }
}

static int jube_count_binds(const JubeTypeBinding* binding,
                            const char* name) {
    if (!binding || !binding->members || !name) return 0;
    int count = 0;
    for (int32_t i = 0; i < binding->member_count; i++) {
        if (binding->members[i].name &&
                strcmp(binding->members[i].name, name) == 0) {
            count++;
        }
    }
    return count;
}

static const JubeTypeBinding* jube_find_type_binding(const JubeTypeBinding* bindings,
                                                     int32_t count, const char* type_name) {
    for (int32_t i = 0; i < count; i++) {
        if (bindings[i].type_name && strcmp(bindings[i].type_name, type_name) == 0) {
            return &bindings[i];
        }
    }
    return NULL;
}

static const JubeTypeDef* jube_module_type_by_name(const JubeModuleDef* module,
                                                   const char* name) {
    for (int32_t i = 0; i < module->type_count; i++) {
        if (module->types[i].name && strcmp(module->types[i].name, name) == 0) {
            return &module->types[i];
        }
    }
    return NULL;
}

static bool jube_index_insert(HashMap* index, const char* chars,
                              JubeMemberRecord* rec) {
    JubeMemberIndexEntry probe = {chars, (uint32_t)strlen(chars), NULL};
    JubeMemberIndexEntry* existing =
        (JubeMemberIndexEntry*)hashmap_get(index, &probe);
    if (existing) {
        // duplicate spellings are rejected by declaration validation; retain
        // the first index entry if a malformed binding table slips through.
        return existing->rec == rec;
    }
    probe.rec = rec;
    hashmap_set(index, &probe);
    return !hashmap_oom(index);
}

// find a previously compiled base type by declared name within the same module
static JubeTypeRecord* jube_find_compiled_base(const JubeModuleDef* module,
                                               const char* base_name) {
    const JubeTypeDef* base_type = jube_module_type_by_name(module, base_name);
    if (!base_type) return NULL;
    return jube_record_for_type((const void*)base_type);
}

static void jube_interface_release_record(JubeTypeRecord* trec, bool unregister_roots);

static int jube_compile_type(const JubeModuleDef* module, const char* source,
                             TSNode type_node, const JubeTypeBinding* bindings,
                             int32_t binding_count) {
    TSNode name_node = ts_node_child_by_field_name(type_node, "name", 4);
    if (ts_node_is_null(name_node)) {
        log_error("JUBE_IFACE: module '%s' object type missing a name", module->name);
        return -1;
    }
    char* type_name = jube_node_text(source, name_node);
    if (!type_name) return -1;

    const JubeTypeDef* host_brand = NULL;
    const JubeTypeBinding* binding =
        jube_find_type_binding(bindings, binding_count, type_name);
    if (!binding) {
        log_error("JUBE_IFACE: module '%s' declares type '%s' with no binding table",
                  module->name, type_name);
        free(type_name);
        return -1;
    }
    host_brand = binding->host_brand ? binding->host_brand
                                     : jube_module_type_by_name(module, type_name);
    if (!host_brand) {
        log_error("JUBE_IFACE: module '%s' type '%s' has no host brand JubeTypeDef",
                  module->name, type_name);
        free(type_name);
        return -1;
    }

    // inherited members flatten in first so derived declarations can override
    JubeTypeRecord* base_rec = NULL;
    TSNode base_node = ts_node_child_by_field_name(type_node, "base", 4);
    if (!ts_node_is_null(base_node)) {
        char* base_name = jube_node_text(source, base_node);
        base_rec = base_name ? jube_find_compiled_base(module, base_name) : NULL;
        if (!base_rec) {
            log_error("JUBE_IFACE: module '%s' type '%s' inherits unknown/uncompiled "
                      "base '%s' (declare bases before derived types)",
                      module->name, type_name, base_name ? base_name : "(null)");
            if (base_name) free(base_name);
            free(type_name);
            return -1;
        }
        free(base_name);
    }

    JubeParsedMember parsed[JUBE_PARSE_MEMBER_CAPACITY];
    memset(parsed, 0, sizeof(parsed));
    int parsed_count = 0;

    uint32_t child_count = ts_node_named_child_count(type_node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(type_node, i);
        if (!jube_node_is(child, "attr")) continue;
        if (parsed_count >= JUBE_PARSE_MEMBER_CAPACITY) {
            log_error("JUBE_IFACE: type '%s' exceeds member capacity", type_name);
            jube_free_parsed_members(parsed, parsed_count);
            free(type_name);
            return -1;
        }
        TSNode attr_name = ts_node_child_by_field_name(child, "name", 4);
        TSNode attr_type = ts_node_child_by_field_name(child, "as", 2);
        if (ts_node_is_null(attr_name) || ts_node_is_null(attr_type)) continue;
        JubeParsedMember* member = &parsed[parsed_count];
        member->name = jube_node_text(source, attr_name);
        if (!member->name) continue;
        // symbol-quoted member names ('type': ...) let keywords act as member
        // names; strip the quotes so bindings match on the bare spelling
        size_t name_len = strlen(member->name);
        if (name_len >= 2 && member->name[0] == '\'' &&
                member->name[name_len - 1] == '\'') {
            char* bare = jube_strndup(member->name + 1, name_len - 2);
            free(member->name);
            member->name = bare;
            if (!member->name) continue;
        }
        if (jube_node_is(attr_type, "fn_type")) {
            member->is_method = true;
            jube_parse_fn_type(source, attr_type, &member->arity, &member->can_raise,
                               &member->result_type_name);
        } else {
            member->result_type_name = jube_node_text(source, attr_type);
        }
        TSNode default_node = ts_node_child_by_field_name(child, "default", 7);
        if (!ts_node_is_null(default_node)) {
            jube_parse_default_literal(source, default_node, member);
        }
        parsed_count++;
    }

    // cross-check declared members against bindings before compiling records
    for (int i = 0; i < parsed_count; i++) {
        int matching_binds = jube_count_binds(binding, parsed[i].name);
        if (matching_binds == 0) {
            if (parsed[i].is_method || !parsed[i].has_default) {
                log_error("JUBE_IFACE: type '%s' member '%s' is declared but unbound "
                          "(only default-valued constants may omit a binding)",
                          type_name, parsed[i].name);
                jube_free_parsed_members(parsed, parsed_count);
                free(type_name);
                return -1;
            }
            continue;
        }
        for (int32_t j = 0; j < binding->member_count; j++) {
            const JubeMemberBind* bind = &binding->members[j];
            if (!bind->name || strcmp(bind->name, parsed[i].name) != 0) continue;
            if (parsed[i].is_method && !bind->call) {
                log_error("JUBE_IFACE: type '%s' method '%s' binding lacks a call handler",
                          type_name, parsed[i].name);
                jube_free_parsed_members(parsed, parsed_count);
                free(type_name);
                return -1;
            }
            if (!parsed[i].is_method && !bind->get && !bind->reflect_attr) {
                log_error("JUBE_IFACE: type '%s' field '%s' binding lacks a getter",
                          type_name, parsed[i].name);
                jube_free_parsed_members(parsed, parsed_count);
                free(type_name);
                return -1;
            }
        }
    }
    for (int32_t i = 0; i < binding->member_count; i++) {
        const char* bind_name = binding->members[i].name;
        bool declared = false;
        for (int j = 0; j < parsed_count && !declared; j++) {
            declared = strcmp(parsed[j].name, bind_name) == 0;
        }
        if (!declared && base_rec) {
            for (int j = 0; j < base_rec->member_count && !declared; j++) {
                declared = strcmp(base_rec->members[j].snake_name, bind_name) == 0;
            }
        }
        if (!declared) {
            log_error("JUBE_IFACE: type '%s' binds undeclared member '%s'",
                      type_name, bind_name);
            jube_free_parsed_members(parsed, parsed_count);
            free(type_name);
            return -1;
        }
    }

    if (s_type_record_count >= JUBE_TYPE_RECORD_CAPACITY) {
        log_error("JUBE_IFACE: type record capacity exceeded at '%s'", type_name);
        jube_free_parsed_members(parsed, parsed_count);
        free(type_name);
        return -1;
    }
    if (!s_type_index) {
        s_type_index = hashmap_new(sizeof(JubeTypeIndexEntry), 16, 0, 0,
                                   jube_type_index_hash, jube_type_index_compare,
                                   NULL, NULL);
        if (!s_type_index) {
            log_error("JUBE_IFACE: failed to allocate type index for '%s'", type_name);
            jube_free_parsed_members(parsed, parsed_count);
            free(type_name);
            return -1;
        }
    }

    int base_count = base_rec ? base_rec->member_count : 0;
    int declared_record_count = 0;
    for (int i = 0; i < parsed_count; i++) {
        int matching_binds = jube_count_binds(binding, parsed[i].name);
        bool overrides_base = false;
        if (base_rec) {
            for (int j = 0; j < base_count; j++) {
                if (strcmp(base_rec->members[j].snake_name, parsed[i].name) == 0) {
                    overrides_base = true;
                    break;
                }
            }
        }
        if (!overrides_base) declared_record_count += matching_binds > 0 ? matching_binds : 1;
    }
    int total = base_count + declared_record_count;
    // Interface records survive runtime teardown and are released after the
    // tracker can change phase, so keep their C ownership independent of a
    // particular JS heap or memtrack mode.
    JubeTypeRecord* trec = (JubeTypeRecord*)calloc(1, sizeof(JubeTypeRecord));
    JubeMemberRecord* records = (JubeMemberRecord*)calloc(
        (size_t)(total > 0 ? total : 1), sizeof(JubeMemberRecord));
    if (!trec || !records) {
        if (trec) free(trec);
        if (records) free(records);
        jube_free_parsed_members(parsed, parsed_count);
        free(type_name);
        return -1;
    }

    int out_count = 0;
    for (int i = 0; i < base_count; i++) {
        // derived types re-record inherited members (records carry per-type
        // caches like method_fn, so they cannot be shared across brands)
        JubeMemberRecord* src = &base_rec->members[i];
        JubeMemberRecord* dst = &records[out_count++];
        dst->bind = src->bind;
        dst->snake_name = jube_strndup(src->snake_name, strlen(src->snake_name));
        dst->camel_name = jube_strndup(src->camel_name, strlen(src->camel_name));
        dst->kind = src->kind;
        dst->readonly = src->readonly;
        dst->enumerable = src->enumerable;
        dst->can_raise = src->can_raise;
        dst->arity = src->arity;
        dst->result_type = src->result_type;
        dst->const_int = src->const_int;
        dst->const_is_str = src->const_is_str;
        dst->const_str = src->const_str
            ? jube_strndup(src->const_str, strlen(src->const_str)) : NULL;
    }

    // release strips log_info(), so keep diagnostic counters out of NDEBUG builds.
#ifndef NDEBUG
    int method_count = 0, const_count = 0;
#endif
    for (int i = 0; i < parsed_count; i++) {
        int matching_binds = jube_count_binds(binding, parsed[i].name);
        int base_ordinal = -1;
        if (base_rec) {
            for (int j = 0; j < base_count; j++) {
                if (strcmp(base_rec->members[j].snake_name, parsed[i].name) == 0) {
                    base_ordinal = j;
                    break;
                }
            }
        }
        if (base_ordinal >= 0) {
            // Derived interfaces override an inherited ordinal in place. This
            // preserves the inherited prefix while replacing the base guard
            // row with the concrete subtype's unambiguous operation.
            if (matching_binds > 1) {
                log_error("JUBE_IFACE: derived type '%s' overrides member '%s' "
                          "with multiple bindings", type_name, parsed[i].name);
                free(records);
                free(trec);
                jube_free_parsed_members(parsed, parsed_count);
                free(type_name);
                return -1;
            }
            const JubeMemberBind* override_bind = NULL;
            for (int32_t j = 0; j < binding->member_count; j++) {
                if (binding->members[j].name &&
                        strcmp(binding->members[j].name, parsed[i].name) == 0) {
                    override_bind = &binding->members[j];
                    break;
                }
            }
            jube_member_record_init(&records[base_ordinal], &parsed[i],
                                    override_bind, module);
            continue;
        }
        int variants = matching_binds > 0 ? matching_binds : 1;
        int variant_index = 0;
        for (int32_t j = 0; j < binding->member_count ||
                (matching_binds == 0 && variant_index == 0); j++) {
            const JubeMemberBind* bind = matching_binds > 0
                ? &binding->members[j] : NULL;
            if (bind && (!bind->name || strcmp(bind->name, parsed[i].name) != 0)) {
                continue;
            }
            JubeMemberRecord* rec = &records[out_count++];
            jube_member_record_init(rec, &parsed[i], bind, module);
            if (rec->kind == JUBE_MEMBER_METHOD) {
#ifndef NDEBUG
                method_count++;
#endif
#ifndef NDEBUG
            } else if (rec->kind == JUBE_MEMBER_CONST) {
                const_count++;
#endif
            }
            variant_index++;
            if (variant_index >= variants) break;
        }
    }

#ifndef NDEBUG
    // H4 requires derived ordinals to retain the complete inherited prefix;
    // catch a declaration-order regression before publishing the type record.
    if (base_rec) {
        for (int i = 0; i < base_count; i++) {
            if (strcmp(records[i].snake_name, base_rec->members[i].snake_name) != 0) {
                log_error("JUBE_IFACE: type '%s' broke inherited ordinal prefix at %d",
                          type_name, i);
                for (int j = 0; j < out_count; j++) {
                    jube_member_record_release_owned(&records[j]);
                }
                free(records);
                free(trec);
                jube_free_parsed_members(parsed, parsed_count);
                free(type_name);
                return -1;
            }
        }
    }
#endif

    HashMap* index = hashmap_new(sizeof(JubeMemberIndexEntry), 16, 0, 0,
                                 jube_member_index_hash, jube_member_index_compare,
                                 NULL, NULL);
    for (int i = 0; i < out_count; i++) {
        JubeMemberRecord* rec = &records[i];
        jube_index_insert(index, rec->snake_name, rec);
        if (strcmp(rec->snake_name, rec->camel_name) != 0) {
            jube_index_insert(index, rec->camel_name, rec);
        }
    }

    trec->type = host_brand;
    trec->binding = binding;
    trec->base_record = base_rec;
    trec->type_slot = s_type_record_count;
    trec->family_root_slot = base_rec ? base_rec->family_root_slot : trec->type_slot;
    trec->members = records;
    trec->member_count = out_count;
    trec->index = index;
    trec->prototype = ItemNull;
    JubeTypeIndexEntry type_entry = {host_brand, trec};
    hashmap_set(s_type_index, &type_entry);
    if (hashmap_oom(s_type_index)) {
        jube_interface_release_record(trec, false);
        jube_free_parsed_members(parsed, parsed_count);
        free(type_name);
        return -1;
    }
    s_type_records[s_type_record_count++] = trec;

#ifndef NDEBUG
    log_info("JUBE_REG: type %s.%s members=%d (methods=%d, consts=%d, inherited=%d)",
             module->name, type_name, out_count, method_count, const_count, base_count);
#endif
    jube_free_parsed_members(parsed, parsed_count);
    free(type_name);
    return 0;
}

static void jube_interface_release_record(JubeTypeRecord* trec, bool unregister_roots) {
    if (!trec) return;
    const JubeHostAPI* host = jube_internal_host_api();
    if (unregister_roots && host && host->gc) {
        if (trec->prototype_rooted) host->gc->unregister_root(&trec->prototype.item);
        for (int j = 0; j < trec->member_count; j++) {
            JubeMemberRecord* rec = &trec->members[j];
            if (rec->method_fn_rooted) host->gc->unregister_root(&rec->method_fn.item);
        }
    }
    // The index stores borrowed member-name pointers; release it before the
    // records so hashmap teardown never hashes already-freed key storage.
    if (trec->index) {
        hashmap_free(trec->index);
        trec->index = NULL;
    }
    for (int j = 0; j < trec->member_count; j++) {
        JubeMemberRecord* rec = &trec->members[j];
        if (rec->snake_name) free(rec->snake_name);
        if (rec->camel_name) free(rec->camel_name);
        if (rec->const_str) free(rec->const_str);
    }
    if (trec->members) free(trec->members);
    free(trec);
}

static bool jube_interface_record_belongs_to_module(const JubeTypeRecord* trec,
                                                     const JubeModuleDef* module) {
    if (!trec || !module || !module->types || module->type_count <= 0) return false;
    for (int32_t i = 0; i < module->type_count; i++) {
        if (trec->type == &module->types[i]) return true;
    }
    return false;
}

extern "C" void jube_interface_remove_module(const JubeModuleDef* module) {
    for (int i = 0; i < s_type_record_count;) {
        JubeTypeRecord* trec = s_type_records[i];
        if (!jube_interface_record_belongs_to_module(trec, module)) {
            i++;
            continue;
        }
        // Registration rollback must release module-owned compiled records
        // before dlclose, because the type descriptors live in that image.
        if (s_type_index) {
            JubeTypeIndexEntry probe = {trec->type, NULL};
            hashmap_delete(s_type_index, &probe);
        }
        jube_interface_release_record(trec, true);
        s_type_record_count--;
        s_type_records[i] = s_type_records[s_type_record_count];
        s_type_records[s_type_record_count] = NULL;
    }
}

// process-exit teardown: frees the compiled records so the memtrack zero-leak
// gate stays honest. Runs after GC-heap destruction, so rooted Items inside
// records are already dead memory — only the C-side allocations are released.
extern "C" void jube_interface_cleanup(void) {
    while (s_type_record_count > 0) {
        int index = --s_type_record_count;
        jube_interface_release_record(s_type_records[index], false);
        s_type_records[index] = NULL;
    }
    if (s_type_index) {
        hashmap_free(s_type_index);
        s_type_index = NULL;
    }
}

static int jube_compile_types_in(const JubeModuleDef* module, const char* source,
                                 TSNode node, const JubeTypeBinding* bindings,
                                 int32_t binding_count, int* compiled) {
    if (jube_node_is(node, "object_type")) {
        int rc = jube_compile_type(module, source, node, bindings, binding_count);
        if (rc == 0) (*compiled)++;
        return rc;
    }
    uint32_t child_count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        int rc = jube_compile_types_in(module, source, ts_node_named_child(node, i),
                                       bindings, binding_count, compiled);
        if (rc != 0) return rc;
    }
    return 0;
}

extern "C" int jube_compile_module_interface(const JubeModuleDef* module) {
    const char* decl = jube_module_interface_decl(module);
    if (!decl || !*decl) return 0;
    int32_t binding_count = 0;
    const JubeTypeBinding* bindings = jube_module_type_bindings(module, &binding_count);
    if (!bindings || binding_count <= 0) {
        log_error("JUBE_IFACE: module '%s' has an interface_decl but no type bindings",
                  module->name);
        return -1;
    }

    TSParser* parser = lambda_parser();
    if (!parser) {
        log_error("JUBE_IFACE: failed to create Lambda parser for module '%s'",
                  module->name);
        return -1;
    }
    TSTree* tree = lambda_parse_source(parser, decl);
    if (!tree) {
        ts_parser_delete(parser);
        log_error("JUBE_IFACE: failed to parse interface of module '%s'", module->name);
        return -1;
    }

    int rc = 0;
    TSNode root = ts_tree_root_node(tree);
    if (ts_node_has_error(root)) {
        log_error("JUBE_IFACE: module '%s' interface_decl has syntax errors",
                  module->name);
        rc = -1;
    }
    // object_type statements sit under document > content, so walk the tree
    // recursively (without descending into matched types) instead of assuming
    // they are direct children of the root
    int compiled = 0;
    if (rc == 0) {
        rc = jube_compile_types_in(module, decl, root, bindings, binding_count,
                                   &compiled);
    }
    // top-level fn/pn signatures stay on JubeFuncDef for now; the interface
    // text carries them for documentation until Phase 4 unifies functions
    if (rc == 0 && compiled == 0) {
        log_error("JUBE_IFACE: module '%s' interface_decl declares no object types",
                  module->name);
        rc = -1;
    }

    ts_tree_delete(tree);
    ts_parser_delete(parser);
    if (rc != 0) {
        // A multi-type declaration may compile a prefix before a later type
        // fails; leave no dispatch records visible from that failed module.
        jube_interface_remove_module(module);
    }
    return rc;
}
