#pragma once

/**
 * dom_ops.h — the DOM core's capability entry points.
 *
 * These three functions are the whole executable surface of the DOM core:
 * every DOM capability reaches its implementation through the ordinal executor
 * or the two property entries. The JS realm arrives here through the Jube
 * declared-interface records and the JubeHostDomAPI seam (ES34); the Lambda
 * `dom` module arrives here directly (same link target, ES36); both land on the
 * same body, which is what ES38 means by one implementation per operation.
 *
 * Declared in their own header rather than in dom.h because the ordinal enum
 * lives in jube.h, and jube.h pulls in C++ headers that dom.h's C consumers
 * (sys_func_registry.c) cannot take.
 */

#include "../jube/jube.h"

/** Execute a DOM capability against a wrapped node. */
extern "C" Item dom_element_operation_impl(Item elem_item,
                                           JubeDomElementOperation operation,
                                           Item* args, int argc);

/** Read a DOM property by name (tagName, textContent, innerHTML, ...). */
extern "C" Item dom_get_property_impl(Item elem_item, Item prop_name);

/** Write a DOM property by name. */
extern "C" Item dom_set_property_impl(Item elem_item, Item prop_name, Item value);
