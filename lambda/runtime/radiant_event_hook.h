#pragma once

#include "../lambda-data.hpp"

typedef Item (*LambdaRadiantEmitFn)(Item event_name, Item event_data);

void lambda_radiant_event_register(LambdaRadiantEmitFn emit_fn);
Item lambda_radiant_emit(Item event_name, Item event_data);
