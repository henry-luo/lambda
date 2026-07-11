// DOM3 interface compiler + record-driven host-object dispatch.
//
// A module declares its script-facing shape once, in Lambda type syntax
// (JubeModuleDef.interface_decl), and supplies behavior as binding tables
// (JubeTypeBinding / JubeMemberBind). At registration this file parses the
// declaration with the real Lambda grammar (Tree-sitter), cross-checks it
// against the bindings, and compiles per-type member records plus a
// content-hashed name index dual-keyed on snake_case and camelCase. The
// generic host-object paths consult these records before a type's host_ops,
// so a fully declared type needs no hand-written dispatch at all.

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
extern "C" Item js_get_this();
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
    int64_t const_int;            // CONST int payload
    char* const_str;              // CONST string payload (owned copy, or NULL)
    bool const_is_str;
    Item method_fn;               // cached function object (lazy, GC-rooted)
    bool method_fn_rooted;
    Item env_slot[1];             // trampoline env -> this record; address must
                                  //   stay stable, hence records never move
    JubeMemberRecord* next_same_name;  // guard chain, declaration order
};

typedef struct JubeTypeRecord {
    const JubeTypeDef* type;      // brand: vmap->host_type of instances
    const JubeTypeBinding* binding;
    JubeMemberRecord* members;    // stable array, declaration order
    int member_count;
    HashMap* index;               // content-hashed name -> record chain head
    Item prototype;               // lazy per-type prototype object (GC-rooted)
    bool prototype_rooted;
} JubeTypeRecord;

#define JUBE_TYPE_RECORD_CAPACITY 64
static JubeTypeRecord* s_type_records[JUBE_TYPE_RECORD_CAPACITY];
static int s_type_record_count = 0;

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
    char* copy = (char*)mem_alloc(len + 1, MEM_CAT_JS_RUNTIME);
    if (!copy) return NULL;
    memcpy(copy, src, len);
    copy[len] = '\0';
    return copy;
}

// snake_case -> camelCase; returns owned copy (identity copy when no '_')
static char* jube_derive_camel(const char* snake) {
    size_t len = strlen(snake);
    char* out = (char*)mem_alloc(len + 1, MEM_CAT_JS_RUNTIME);
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
    if (!host_type) return NULL;
    for (int i = 0; i < s_type_record_count; i++) {
        if ((const void*)s_type_records[i]->type == host_type) return s_type_records[i];
    }
    return NULL;
}

static JubeTypeRecord* jube_record_for(Item receiver) {
    if (get_type_id(receiver) != LMD_TYPE_VMAP || !receiver.vmap ||
            !receiver.vmap->host_type) {
        return NULL;
    }
    return jube_record_for_type(receiver.vmap->host_type);
}

// resolve a key against the type's index; guard chains evaluate in
// declaration order (first matching guard wins)
static JubeMemberRecord* jube_resolve_member(JubeTypeRecord* trec, Item receiver, Item key) {
    const char* chars = NULL;
    uint32_t len = 0;
    if (!trec->index || !jube_item_key_chars(key, &chars, &len)) return NULL;
    JubeMemberIndexEntry probe = {chars, len, NULL};
    const JubeMemberIndexEntry* found =
        (const JubeMemberIndexEntry*)hashmap_get(trec->index, &probe);
    if (!found) return NULL;
    for (JubeMemberRecord* rec = found->rec; rec; rec = rec->next_same_name) {
        if (rec->bind && rec->bind->guard && !rec->bind->guard(receiver)) continue;
        return rec;
    }
    return NULL;
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
// Method function objects: one cached per member record, invoked through a
// per-arity trampoline. The record pointer rides the JsFunction closure env
// (the established JsProxyData pattern), so nine trampolines cover every
// declared method without per-method C stubs.
// ============================================================================

static Item jube_tramp_invoke(Item env_item, Item* args, int argc) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    JubeMemberRecord* rec = env ? (JubeMemberRecord*)(uintptr_t)env[0].item : NULL;
    Item out = jube_undefined_item();
    if (rec && rec->bind && rec->bind->call) {
        rec->bind->call(js_get_this(), args, argc, &out);
    }
    return out;
}

static Item jube_tramp_0(Item env) { return jube_tramp_invoke(env, NULL, 0); }
static Item jube_tramp_1(Item env, Item a0) {
    Item a[] = {a0}; return jube_tramp_invoke(env, a, 1);
}
static Item jube_tramp_2(Item env, Item a0, Item a1) {
    Item a[] = {a0, a1}; return jube_tramp_invoke(env, a, 2);
}
static Item jube_tramp_3(Item env, Item a0, Item a1, Item a2) {
    Item a[] = {a0, a1, a2}; return jube_tramp_invoke(env, a, 3);
}
static Item jube_tramp_4(Item env, Item a0, Item a1, Item a2, Item a3) {
    Item a[] = {a0, a1, a2, a3}; return jube_tramp_invoke(env, a, 4);
}
static Item jube_tramp_5(Item env, Item a0, Item a1, Item a2, Item a3, Item a4) {
    Item a[] = {a0, a1, a2, a3, a4}; return jube_tramp_invoke(env, a, 5);
}
static Item jube_tramp_6(Item env, Item a0, Item a1, Item a2, Item a3, Item a4, Item a5) {
    Item a[] = {a0, a1, a2, a3, a4, a5}; return jube_tramp_invoke(env, a, 6);
}
static Item jube_tramp_7(Item env, Item a0, Item a1, Item a2, Item a3, Item a4, Item a5,
                         Item a6) {
    Item a[] = {a0, a1, a2, a3, a4, a5, a6}; return jube_tramp_invoke(env, a, 7);
}
static Item jube_tramp_8(Item env, Item a0, Item a1, Item a2, Item a3, Item a4, Item a5,
                         Item a6, Item a7) {
    Item a[] = {a0, a1, a2, a3, a4, a5, a6, a7}; return jube_tramp_invoke(env, a, 8);
}

static void* const s_jube_tramps[9] = {
    (void*)jube_tramp_0, (void*)jube_tramp_1, (void*)jube_tramp_2,
    (void*)jube_tramp_3, (void*)jube_tramp_4, (void*)jube_tramp_5,
    (void*)jube_tramp_6, (void*)jube_tramp_7, (void*)jube_tramp_8,
};

static Item jube_member_method_item(JubeMemberRecord* rec) {
    if (rec->method_fn_rooted) return rec->method_fn;
    const JubeHostAPI* host = jube_internal_host_api();
    int arity = rec->arity;
    if (arity < 0) arity = 0;
    if (arity > 8) arity = 8;
    Item fn_item = host->script->new_function(s_jube_tramps[arity], arity);
    JsFunction* fn = (JsFunction*)fn_item.function;
    if (fn) {
        rec->env_slot[0] = (Item){.item = (uint64_t)(uintptr_t)rec};
        fn->env = rec->env_slot;
        fn->env_size = 1;
    }
    host->script->set_function_name(fn_item, jube_name_item(rec->camel_name));
    rec->method_fn = fn_item;
    host->gc->register_root(&rec->method_fn.item);
    rec->method_fn_rooted = true;
    return rec->method_fn;
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

static Item jube_type_prototype_for(JubeTypeRecord* trec) {
    if (trec->prototype_rooted) return trec->prototype;
    const JubeHostAPI* host = jube_internal_host_api();
    // adopt the module's existing prototype object when one is seeded, so
    // constructor .prototype identity (instanceof) survives the conversion
    if (trec->binding && trec->binding->prototype_seed) {
        trec->prototype = trec->binding->prototype_seed();
    } else {
        trec->prototype = host->value->new_object();
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
                                      jube_member_method_item(rec));
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
    if (rec) {
        switch (rec->kind) {
        case JUBE_MEMBER_CONST:
            *out = jube_member_const_item(rec);
            return 1;
        case JUBE_MEMBER_METHOD:
            *out = jube_member_method_item(rec);
            return 1;
        default:
            if (rec->bind && rec->bind->get && rec->bind->get(receiver, out)) return 1;
            *out = jube_undefined_item();
            return 1;
        }
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

int jube_member_set(Item receiver, Item key, Item value, Item* out) {
    JubeTypeRecord* trec = jube_record_for(receiver);
    if (!trec || !out) return 0;
    if (!receiver.vmap->host_data) {
        *out = jube_undefined_item();
        return 1;
    }
    JubeMemberRecord* rec = jube_resolve_member(trec, receiver, key);
    if (rec) {
        if (rec->readonly || rec->kind != JUBE_MEMBER_FIELD) {
            // declared read-only surface swallows writes (pinned DOM1 behavior:
            // no expando shadowing of projected members)
            *out = value;
            return 1;
        }
        if (rec->bind && rec->bind->set && rec->bind->set(receiver, value, out)) return 1;
        *out = value;
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

int jube_member_call(Item receiver, Item name, Item* args, int argc, Item* out) {
    JubeTypeRecord* trec = jube_record_for(receiver);
    if (!trec || !out) return 0;
    if (!receiver.vmap->host_data) {
        *out = jube_undefined_item();
        return 1;
    }
    JubeMemberRecord* rec = jube_resolve_member(trec, receiver, name);
    if (rec && rec->kind == JUBE_MEMBER_METHOD && rec->bind && rec->bind->call) {
        rec->bind->call(receiver, args, argc, out);
        return 1;
    }
    // not a declared method: fall through so the engine can read the property
    // (expando-stored functions) and call it
    return 0;
}

int jube_member_has(Item receiver, Item key, Item* out) {
    JubeTypeRecord* trec = jube_record_for(receiver);
    if (!trec || !out) return 0;
    bool present = jube_resolve_member(trec, receiver, key) != NULL;
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

int jube_member_descriptor(Item receiver, Item key, Item* out) {
    JubeTypeRecord* trec = jube_record_for(receiver);
    if (!trec || !out) return 0;
    const JubeHostAPI* host = jube_internal_host_api();
    JubeMemberRecord* rec = jube_resolve_member(trec, receiver, key);
    if (rec && rec->kind != JUBE_MEMBER_METHOD && receiver.vmap->host_data) {
        Item value = jube_undefined_item();
        jube_member_get(receiver, key, &value);
        Item desc = host->value->new_object();
        host->value->property_set(desc, jube_name_item("value"), value);
        host->value->property_set(desc, jube_name_item("writable"),
                                  (Item){.item = b2it(!rec->readonly)});
        host->value->property_set(desc, jube_name_item("enumerable"),
                                  (Item){.item = b2it(true)});
        host->value->property_set(desc, jube_name_item("configurable"),
                                  (Item){.item = b2it(false)});
        *out = desc;
        return 1;
    }
    if (receiver.vmap->host_data) {
        Item expando = jube_expando_object(receiver, false);
        if (get_type_id(expando) == LMD_TYPE_MAP) {
            Item value = host->value->property_get(expando, key);
            if (jube_expando_value_present(value)) {
                Item desc = host->value->new_object();
                host->value->property_set(desc, jube_name_item("value"), value);
                host->value->property_set(desc, jube_name_item("writable"),
                                          (Item){.item = b2it(true)});
                host->value->property_set(desc, jube_name_item("enumerable"),
                                          (Item){.item = b2it(true)});
                host->value->property_set(desc, jube_name_item("configurable"),
                                          (Item){.item = b2it(true)});
                *out = desc;
                return 1;
            }
        }
    }
    *out = jube_undefined_item();
    return 1;
}

int jube_member_own_keys(Item receiver, Item* out) {
    JubeTypeRecord* trec = jube_record_for(receiver);
    if (!trec || !out) return 0;
    const JubeHostAPI* host = jube_internal_host_api();
    Item keys = host->value->array_new(0);
    for (int i = 0; i < trec->member_count; i++) {
        JubeMemberRecord* rec = &trec->members[i];
        if (!rec->enumerable) continue;
        if (rec->bind && rec->bind->guard && !rec->bind->guard(receiver)) continue;
        host->value->array_push(keys, jube_name_item(rec->camel_name));
    }
    if (receiver.vmap->host_data) {
        Item expando = jube_expando_object(receiver, false);
        if (get_type_id(expando) == LMD_TYPE_MAP) {
            Item expando_keys = host->script->reflect_own_keys(expando);
            if (get_type_id(expando_keys) == LMD_TYPE_ARRAY && expando_keys.array) {
                Array* arr = expando_keys.array;
                for (int64_t i = 0; i < arr->length; i++) {
                    host->value->array_push(keys, arr->items[i]);
                }
            }
        }
    }
    *out = keys;
    return 1;
}

int jube_member_prototype(Item receiver, Item* out) {
    JubeTypeRecord* trec = jube_record_for(receiver);
    if (!trec || !out) return 0;
    *out = jube_type_prototype(trec->type);
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
    bool has_default;
    bool default_is_str;
    int64_t default_int;
    char* default_str;
} JubeParsedMember;

#define JUBE_PARSE_MEMBER_CAPACITY 256

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
                               bool* can_raise) {
    *arity = 0;
    *can_raise = false;
    uint32_t count = ts_node_named_child_count(fn_node);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(fn_node, i);
        if (jube_node_is(child, "fn_param")) (*arity)++;
        if (jube_node_is(child, "return_type")) {
            char* text = jube_node_text(source, child);
            if (text) {
                if (strchr(text, '^')) *can_raise = true;
                mem_free(text);
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
            mem_free(text);
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
    mem_free(text);
    return true;
}

static void jube_free_parsed_members(JubeParsedMember* members, int count) {
    for (int i = 0; i < count; i++) {
        if (members[i].name) mem_free(members[i].name);
        if (members[i].default_str) mem_free(members[i].default_str);
    }
}

static const JubeMemberBind* jube_find_bind(const JubeTypeBinding* binding,
                                            const char* name) {
    if (!binding || !binding->members) return NULL;
    for (int32_t i = 0; i < binding->member_count; i++) {
        if (binding->members[i].name && strcmp(binding->members[i].name, name) == 0) {
            return &binding->members[i];
        }
    }
    return NULL;
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
        // same-spelling chain keeps declaration order for guard resolution
        JubeMemberRecord* tail = existing->rec;
        while (tail->next_same_name) tail = tail->next_same_name;
        if (tail != rec) tail->next_same_name = rec;
        return true;
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
        mem_free(type_name);
        return -1;
    }
    host_brand = binding->host_brand ? binding->host_brand
                                     : jube_module_type_by_name(module, type_name);
    if (!host_brand) {
        log_error("JUBE_IFACE: module '%s' type '%s' has no host brand JubeTypeDef",
                  module->name, type_name);
        mem_free(type_name);
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
            if (base_name) mem_free(base_name);
            mem_free(type_name);
            return -1;
        }
        mem_free(base_name);
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
            mem_free(type_name);
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
            mem_free(member->name);
            member->name = bare;
            if (!member->name) continue;
        }
        if (jube_node_is(attr_type, "fn_type")) {
            member->is_method = true;
            jube_parse_fn_type(source, attr_type, &member->arity, &member->can_raise);
        }
        TSNode default_node = ts_node_child_by_field_name(child, "default", 7);
        if (!ts_node_is_null(default_node)) {
            jube_parse_default_literal(source, default_node, member);
        }
        parsed_count++;
    }

    // cross-check declared members against bindings before compiling records
    for (int i = 0; i < parsed_count; i++) {
        const JubeMemberBind* bind = jube_find_bind(binding, parsed[i].name);
        if (!bind) {
            if (parsed[i].is_method || !parsed[i].has_default) {
                log_error("JUBE_IFACE: type '%s' member '%s' is declared but unbound "
                          "(only default-valued constants may omit a binding)",
                          type_name, parsed[i].name);
                jube_free_parsed_members(parsed, parsed_count);
                mem_free(type_name);
                return -1;
            }
            continue;
        }
        if (parsed[i].is_method && !bind->call) {
            log_error("JUBE_IFACE: type '%s' method '%s' binding lacks a call handler",
                      type_name, parsed[i].name);
            jube_free_parsed_members(parsed, parsed_count);
            mem_free(type_name);
            return -1;
        }
        if (!parsed[i].is_method && !bind->get && !bind->reflect_attr) {
            log_error("JUBE_IFACE: type '%s' field '%s' binding lacks a getter",
                      type_name, parsed[i].name);
            jube_free_parsed_members(parsed, parsed_count);
            mem_free(type_name);
            return -1;
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
            mem_free(type_name);
            return -1;
        }
    }

    if (s_type_record_count >= JUBE_TYPE_RECORD_CAPACITY) {
        log_error("JUBE_IFACE: type record capacity exceeded at '%s'", type_name);
        jube_free_parsed_members(parsed, parsed_count);
        mem_free(type_name);
        return -1;
    }

    int base_count = base_rec ? base_rec->member_count : 0;
    int total = base_count + parsed_count;
    JubeTypeRecord* trec = (JubeTypeRecord*)mem_calloc(1, sizeof(JubeTypeRecord),
                                                       MEM_CAT_JS_RUNTIME);
    JubeMemberRecord* records = (JubeMemberRecord*)mem_calloc(
        (size_t)(total > 0 ? total : 1), sizeof(JubeMemberRecord), MEM_CAT_JS_RUNTIME);
    if (!trec || !records) {
        if (trec) mem_free(trec);
        if (records) mem_free(records);
        jube_free_parsed_members(parsed, parsed_count);
        mem_free(type_name);
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
        dst->const_int = src->const_int;
        dst->const_is_str = src->const_is_str;
        dst->const_str = src->const_str
            ? jube_strndup(src->const_str, strlen(src->const_str)) : NULL;
    }

    int method_count = 0, const_count = 0;
    for (int i = 0; i < parsed_count; i++) {
        JubeMemberRecord* rec = &records[out_count++];
        const JubeMemberBind* bind = jube_find_bind(binding, parsed[i].name);
        rec->bind = bind;
        rec->snake_name = parsed[i].name;
        parsed[i].name = NULL;  // ownership moved into the record
        rec->camel_name = (bind && bind->js_name)
            ? jube_strndup(bind->js_name, strlen(bind->js_name))
            : jube_derive_camel(rec->snake_name);
        if (parsed[i].is_method) {
            rec->kind = JUBE_MEMBER_METHOD;
            rec->arity = parsed[i].arity;
            rec->can_raise = parsed[i].can_raise;
            rec->readonly = true;
            method_count++;
        } else if (!bind) {
            rec->kind = JUBE_MEMBER_CONST;
            rec->readonly = true;
            rec->const_int = parsed[i].default_int;
            rec->const_is_str = parsed[i].default_is_str;
            rec->const_str = parsed[i].default_str;
            parsed[i].default_str = NULL;
            const_count++;
        } else {
            rec->kind = JUBE_MEMBER_FIELD;
            rec->readonly = !bind->set && !bind->reflect_attr;
            if (parsed[i].default_str) mem_free(parsed[i].default_str);
            parsed[i].default_str = NULL;
        }
        // constants live on the prototype in WebIDL terms and aliases shadow a
        // canonical member, so neither enumerates; fields may opt out via flags
        rec->enumerable = rec->kind == JUBE_MEMBER_FIELD &&
            !(bind && (bind->flags & JUBE_MEMBER_NON_ENUMERABLE));
    }

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
    trec->members = records;
    trec->member_count = out_count;
    trec->index = index;
    trec->prototype = ItemNull;
    s_type_records[s_type_record_count++] = trec;

    log_info("JUBE_REG: type %s.%s members=%d (methods=%d, consts=%d, inherited=%d)",
             module->name, type_name, out_count, method_count, const_count, base_count);
    jube_free_parsed_members(parsed, parsed_count);
    mem_free(type_name);
    return 0;
}

// process-exit teardown: frees the compiled records so the memtrack zero-leak
// gate stays honest. Runs after GC-heap destruction, so rooted Items inside
// records are already dead memory — only the C-side allocations are released.
extern "C" void jube_interface_cleanup(void) {
    for (int i = 0; i < s_type_record_count; i++) {
        JubeTypeRecord* trec = s_type_records[i];
        if (!trec) continue;
        for (int j = 0; j < trec->member_count; j++) {
            JubeMemberRecord* rec = &trec->members[j];
            if (rec->snake_name) mem_free(rec->snake_name);
            if (rec->camel_name) mem_free(rec->camel_name);
            if (rec->const_str) mem_free(rec->const_str);
        }
        if (trec->members) mem_free(trec->members);
        if (trec->index) hashmap_free(trec->index);
        mem_free(trec);
        s_type_records[i] = NULL;
    }
    s_type_record_count = 0;
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
    return rc;
}
