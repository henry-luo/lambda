#pragma once

#include "dom_provider.hpp"
#include "../lambda-data.hpp"

// dispatch keys borrow native targets; wrappers retain their existing traced
// backing edges. Provider publication has process lifetime (D4.5.1v4, D7.1.3).
typedef enum DomEventTargetKeyKind {
    DOM_EVENT_KEY_NONE = 0,
    DOM_EVENT_KEY_NODE,
    DOM_EVENT_KEY_WINDOW,
    DOM_EVENT_KEY_DOCUMENT,
    DOM_EVENT_KEY_CONTAINER,
} DomEventTargetKeyKind;
typedef struct DomEventTargetKey { DomEventTargetKeyKind kind; void* ptr; } DomEventTargetKey;

#define DOM_EVENT_RECORD_HOST_OPERATIONS(OP) \
    OP(Item, create, (const char* type, bool bubbles, bool cancelable, bool composed, int class_id), (type, bubbles, cancelable, composed, class_id), ItemNull) \
    OP(bool, is, (Item item), (item), false) \
    OP(bool, default_prevented, (Item item), (item), false) \
    OP(bool, prevent_default, (Item item), (item), false) \
    OP(void, set_trusted, (Item item, bool trusted), (item, trusted), (void)0) \
    OP(void, set_time_values, (Item event, Item view, Item detail), (event, view, detail), (void)0) \
    OP(bool, set_payload, (Item event, Item payload), (event, payload), false) \
    OP(Item, payload, (Item event, int class_id), (event, class_id), ItemNull) \
    OP(void, set_prototype_override, (Item item, Item prototype), (item, prototype), (void)0) \
    OP(int, named_get, (Item receiver, Item key, Item* out), (receiver, key, out), 0) \
    OP(int, named_set, (Item receiver, Item key, Item value, Item* out), (receiver, key, value, out), 0) \
    OP(void, set_target, (Item event, Item target), (event, target), (void)0) \
    OP(void, set_position_key, (Item event, DomEventTargetKey key, int event_phase), (event, key, event_phase), (void)0) \
    OP(void, set_path, (Item event, const DomEventTargetKey* keys, int length), (event, keys, length), (void)0) \
    OP(void, clear_path, (Item event), (event), (void)0) \
    OP(void, set_lambda_dispatch_position, (Item event, Item current_target, int event_phase), (event, current_target, event_phase), (void)0) \
    OP(void, clear_lambda_dispatch_position, (Item event), (event), (void)0) \
    OP(int, call, (Item receiver, const char* name, Item* arguments, int count, Item* out), (receiver, name, arguments, count, out), 0)

#define DOM_EVENT_HOST_OPERATIONS(OP) \
    DOM_EVENT_RECORD_HOST_OPERATIONS(OP) \
    OP(Item, dispatch_synthetic, (Item target, Item event), (target, event), ItemNull) \
    OP(bool, synthetic_dispatch_is_reentry, (Item event), (event), false) \
    OP(bool, author_template_dispatch_begin, (Item event), (event), false) \
    OP(void, author_template_dispatch_end, (Item event), (event), (void)0)

struct DomEventHostOps {
#define DOM_EVENT_HOST_FIELD(result, name, parameters, arguments, absence) result (*name) parameters;
    DOM_EVENT_HOST_OPERATIONS(DOM_EVENT_HOST_FIELD)
#undef DOM_EVENT_HOST_FIELD
};

extern "C" bool dom_event_register_host(const DomEventHostOps* operations);
extern "C" const DomEventHostOps* dom_event_host_ops(void);
#define DOM_EVENT_HOST_DECLARE(result, name, parameters, arguments, absence) extern "C" result dom_event_host_##name parameters;
DOM_EVENT_HOST_OPERATIONS(DOM_EVENT_HOST_DECLARE)
#undef DOM_EVENT_HOST_DECLARE
