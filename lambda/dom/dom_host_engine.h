#pragma once

#include "dom_provider.hpp"
#include "../lambda-data.hpp"

// native host calls borrow Items/documents/text synchronously; publication has process lifetime
// (D4.5.1v4 / D4.1.4v5, D7.1.3 / D7.5.3).
#define DOM_HOST_ENGINE_OPERATIONS(OP) \
    OP(Item, request_frame, (Item owner, Item event_name), (owner, event_name), ItemNull) \
    OP(Item, cancel_frame, (Item owner, Item token), (owner, token), ItemNull) \
    OP(Item, viewport_size, (Item owner), (owner), ItemNull) \
    OP(Item, watch_window, (Item owner, Item event_name, Item enabled), (owner, event_name, enabled), ItemNull) \
    OP(Item, history_traverse, (Item owner, Item delta), (owner, delta), ItemNull) \
    OP(Item, media_info, (Item owner), (owner), ItemNull) \
    OP(Item, media_command, (Item owner, Item command, Item value), (owner, command, value), ItemNull) \
    OP(Item, clear_editing_focus, (Item owner), (owner), ItemNull) \
    OP(Item, edit_target, (Item owner, Item token), (owner, token), ItemNull) \
    OP(Item, query_command, (void* document, const char* kind, const char* command), (document, kind, command), ItemNull) \
    OP(bool, set_design_mode, (void* document, const char* value), (document, value), false)

struct DomHostEngineOps {
#define DOM_HOST_FIELD(result, name, parameters, arguments, absence) result (*name) parameters;
    DOM_HOST_ENGINE_OPERATIONS(DOM_HOST_FIELD)
#undef DOM_HOST_FIELD
};

extern "C" bool dom_host_register_engine(const DomHostEngineOps* operations);
extern "C" const DomHostEngineOps* dom_host_engine_ops(void);

#define DOM_HOST_DECLARE(result, name, parameters, arguments, absence) extern "C" result dom_engine_##name parameters;
DOM_HOST_ENGINE_OPERATIONS(DOM_HOST_DECLARE)
#undef DOM_HOST_DECLARE
