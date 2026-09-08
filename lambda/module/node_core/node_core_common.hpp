// node_core_common.hpp — helpers shared by the node_core Jube modules.
//
// Each module owns its own `JubeHostAPI*` static, so every helper here takes
// the host explicitly instead of reaching for a module-local global. Promote
// shared shapes into this header rather than re-cloning them per module
// (CLAUDE.md rule 13).
#pragma once

#include "../../jube/jube_registry.h"
#include <cstring>

// Look up a property on globalThis by name.
//
// The key string is rooted across the lookup: constructing it can trigger a
// compaction, which would otherwise leave `global_property` reading a stale
// key. Returns ItemNull when the host is unavailable or rooting fails.
static inline Item jube_node_global_property(const JubeHostAPI* host, const char* name) {
    if (!host || !host->script || !host->script->global_property) return ItemNull;
    JubeRootFrame frame = {};
    if (!host->node->roots->root_frame_begin(&frame, 1)) return ItemNull;
    uint64_t* key_root = host->node->roots->root_frame_take_slot(&frame);
    if (!key_root) {
        host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    Item key = host->value->string_from_utf8_n(name, strlen(name));
    *key_root = key.item;
    Item result = host->script->global_property((Item){.item = *key_root});
    host->node->roots->root_frame_end(&frame);
    return result;
}

// A Jube root frame that releases itself at scope exit.
//
// Native factories MUST root the object they are building. A freshly created
// object is reachable from nowhere else while its properties are installed, and
// every install allocates (at least the key string), so a collection triggered
// mid-construction reclaims it — D5.4.2: a value under construction is live and
// needs an exact root. The symptom is never an allocator error. The finished
// object simply comes back missing properties, or a later store writes into
// reclaimed memory and crashes. D2.1.7 pins the heap as non-moving, so a root
// keeps a plain local valid; the root is about liveness, not address stability.
struct JubeScopedRoots {
    const JubeHostAPI* host;
    JubeRootFrame frame;
    bool active;

    JubeScopedRoots(const JubeHostAPI* h, size_t count) : host(h), frame(), active(false) {
        if (h && h->node && h->node->roots && h->node->roots->root_frame_begin &&
                h->node->roots->root_frame_take_slot && h->node->roots->root_frame_end) {
            active = h->node->roots->root_frame_begin(&frame, count);
        }
    }
    ~JubeScopedRoots() { if (active) host->node->roots->root_frame_end(&frame); }
    JubeScopedRoots(const JubeScopedRoots&) = delete;
    JubeScopedRoots& operator=(const JubeScopedRoots&) = delete;

    // take one slot, pre-charged with `value`; NULL when the frame is unusable
    uint64_t* slot(Item value) {
        if (!active) return NULL;
        uint64_t* root = host->node->roots->root_frame_take_slot(&frame);
        if (root) *root = value.item;
        return root;
    }
};

static inline Item jube_root_item(const uint64_t* root) {
    return (Item){.item = root ? *root : 0};
}

// Store `object[name] = value` with the key and the value both rooted.
//
// Written out at a call site this is
// `property_set(obj, string(name), <value expression>)`, whose two argument
// expressions each allocate with unspecified evaluation order (C++17 [expr.call]
// still leaves argument order indeterminate), so whichever is built first is an
// unrooted temporary while its sibling allocates. Returns `object` so callers
// can keep assigning through the result.
static inline Item jube_node_object_set(const JubeHostAPI* host, Item object,
                                        const char* name, Item value) {
    if (!host || !host->value || !host->value->property_set ||
            !host->value->string_from_utf8_n || !name) return object;
    JubeScopedRoots roots(host, 3);
    uint64_t* object_root = roots.slot(object);
    uint64_t* value_root = roots.slot(value);
    if (!object_root || !value_root) return object;
    Item key = host->value->string_from_utf8_n(name, strlen(name));
    uint64_t* key_root = roots.slot(key);
    if (!key_root) return object;
    host->value->property_set(jube_root_item(object_root), jube_root_item(key_root),
                              jube_root_item(value_root));
    return jube_root_item(object_root);
}
