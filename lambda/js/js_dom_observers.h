#pragma once

#include "../lambda.h"
#include "../input/css/dom_element.hpp"

#ifdef __cplusplus
extern "C" {
#endif

Item js_mutation_observer_new(Item callback);
Item js_resize_observer_new(Item callback);
Item js_intersection_observer_new(Item callback, Item options);

void js_dom_observers_mutation_notify(DomJsMutationKind kind,
                                      void* target, void* parent,
                                      const char* attribute_name,
                                      const char* old_value);
void js_dom_observers_post_layout(void);
void js_dom_observers_reset(void);

#ifdef __cplusplus
}
#endif
