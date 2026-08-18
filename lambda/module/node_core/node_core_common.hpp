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
