#ifndef EVENT_HANDLER_REGISTRY_H
#define EVENT_HANDLER_REGISTRY_H

#include "../lib/mempool.h"

struct DomElement;
struct hashmap;

// Linked list of compiled event handlers for a single element
typedef struct JsEventHandler {
    DomElement* element;
    const char* event_type;         // "click", "mouseover", etc.
    const char* handler_source;     // original attribute value
    void* compiled_func;            // compiled function pointer (void -> Item)
    struct JsEventHandler* next;    // next handler on same element
} JsEventHandler;

// Registry: maps DomElement* → JsEventHandler* linked list
typedef struct JsEventRegistry {
    struct hashmap* element_map;    // key = DomElement*, value = JsEventHandler*
    int count;                      // total handler count
    Pool* pool;                     // pool for handler allocations
} JsEventRegistry;

#endif // EVENT_HANDLER_REGISTRY_H
