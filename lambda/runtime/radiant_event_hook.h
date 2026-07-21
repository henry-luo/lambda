#pragma once

#include "../lambda-data.hpp"

typedef Item (*LambdaRadiantEmitFn)(Item event_name, Item event_data);
typedef Item (*LambdaRadiantSelectionFn)(Item selection);

void lambda_radiant_event_register(LambdaRadiantEmitFn emit_fn,
                                   LambdaRadiantSelectionFn selection_fn);
Item lambda_radiant_emit(Item event_name, Item event_data);
Item lambda_radiant_set_selection(Item selection);
