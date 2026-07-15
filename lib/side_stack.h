#ifndef LAMBDA_SIDE_STACK_H
#define LAMBDA_SIDE_STACK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Context Context;

#define LAMBDA_SIDE_ROOT_RESERVE_BYTES (16u * 1024u * 1024u)
#define LAMBDA_SIDE_NUMBER_RESERVE_BYTES (64u * 1024u * 1024u)

typedef struct LambdaSideStackSnapshot {
    uint64_t* root_top;
    uint64_t* number_top;
} LambdaSideStackSnapshot;

bool lambda_side_stack_bind(Context* context);
bool lambda_side_stack_ensure(Context* context, size_t root_slots,
                              size_t number_slots);
void lambda_side_stack_reset(Context* context);
LambdaSideStackSnapshot lambda_side_stack_snapshot(Context* context);
void lambda_side_stack_restore(Context* context, LambdaSideStackSnapshot snapshot);
uint64_t* lambda_side_number_alloc(Context* context);
void lambda_side_stack_decommit_unused(Context* context);

#ifdef __cplusplus
}
#endif

#endif
