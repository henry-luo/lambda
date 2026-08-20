#pragma once

#include "../../jube/jube.h"

#define NODE_TRACE_MAX_CATEGORIES 64
#define NODE_TRACE_MAX_EVENTS 2048

struct NodeTraceCategory {
    char name[64];
    int refs;
    bool from_exec_argv;
};

struct NodeTraceEvent {
    char ph;
    char cat[128];
    char name[96];
    uint64_t ts;
    int64_t id;
    bool has_id;
};

// Node trace data belongs to the optional NodeRuntimeSession. Keeping it out
// of JsRuntimeState avoids paying for the fixed event buffer in non-Node JS realms.
struct NodeTraceState {
    NodeTraceCategory categories[NODE_TRACE_MAX_CATEGORIES];
    int category_count;
    NodeTraceEvent events[NODE_TRACE_MAX_EVENTS];
    int event_count;
    bool initialized;
    bool file_written;
    uint64_t namespace_item;
    bool namespace_rooted;
};

int node_trace_events_init(const JubeHostAPI* host);
void node_trace_events_shutdown(void);
void node_trace_events_runtime_reset(void* session);
void node_trace_events_runtime_detach(void* session);
Item node_trace_events_namespace(void);
Item node_trace_events_internal_binding(void);
void node_trace_events_emit_async_hooks_init(const char* type_chars, int type_len,
                                             int64_t async_id, int64_t trigger_id);
void node_trace_events_flush(void);
