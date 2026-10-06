#pragma once

#include "../lambda-data.hpp"

typedef Item (*LambdaRadiantEmitFn)(Item event_name, Item event_data);
typedef Item (*LambdaScopedEmitFn)(void* receiver, Item event_name,
                                   Item event_data);

typedef struct LambdaEmitScope {
    LambdaScopedEmitFn receiver_fn;
    void* receiver;
    struct LambdaEmitScope* previous;
} LambdaEmitScope;

void lambda_radiant_event_register(LambdaRadiantEmitFn emit_fn);
void lambda_emit_scope_enter(LambdaEmitScope* scope, LambdaScopedEmitFn receiver_fn,
                             void* receiver);
void lambda_emit_scope_leave(LambdaEmitScope* scope);
Item lambda_radiant_emit(Item event_name, Item event_data);
