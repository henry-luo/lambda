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

// Non-local recovery bypasses generated epilogues and C++ destructors. Keep
// every runtime-owned side-stack watermark in one checkpoint so new dynamic
// regions cannot be restored by only some setjmp/longjmp boundaries.
typedef struct LambdaRecoveryCheckpoint {
    Context* context;
    LambdaSideStackSnapshot side_stack;
    uint64_t mir_return_lane;
    uint64_t mir_bitcast_scratch;
    bool active;
} LambdaRecoveryCheckpoint;

// Exact native-helper root frame. The frame reserves canonical slots above
// the current side-root watermark; generated frames may nest above it.
typedef struct LambdaRootFrame {
    Context* context;
    uint64_t* watermark;
    uint64_t* slots;
    size_t slot_count;
    size_t next_slot;
    bool active;
} LambdaRootFrame;

bool lambda_side_stack_bind(void);
// MIR imports use the thread-bound evaluator rather than carrying Context*
// through every native helper call.
bool lambda_side_stack_ensure_tls(size_t root_slots, size_t number_slots);
void lambda_side_stack_reset(void);
LambdaSideStackSnapshot lambda_side_stack_snapshot(void);
void lambda_side_stack_restore(LambdaSideStackSnapshot snapshot);
LambdaRecoveryCheckpoint lambda_recovery_checkpoint_capture(void);
void lambda_recovery_checkpoint_restore(LambdaRecoveryCheckpoint* checkpoint);
void lambda_recovery_checkpoint_disarm(LambdaRecoveryCheckpoint* checkpoint);
// Reserve canonical Item roots above the current watermark. The caller owns
// restoration through a saved side-stack snapshot or an enclosing frame.
uint64_t* lambda_side_root_alloc_n(size_t slot_count);
uint64_t* lambda_side_number_alloc(void);
void lambda_side_stack_decommit_unused(void);

bool lambda_root_frame_begin(LambdaRootFrame* frame, size_t slot_count);
uint64_t* lambda_root_frame_slot(LambdaRootFrame* frame, size_t index);
uint64_t* lambda_root_frame_take_slot(LambdaRootFrame* frame);
void lambda_root_frame_end(LambdaRootFrame* frame);

// Explicit-owner variants are test/control-plane surfaces for an inactive
// context. Runtime execution must use the TLS APIs above.
bool lambda_side_stack_bind_for(Context* context);
bool lambda_side_stack_ensure_for(Context* context, size_t root_slots,
                                  size_t number_slots);
void lambda_side_stack_reset_for(Context* context);
LambdaSideStackSnapshot lambda_side_stack_snapshot_for(Context* context);
void lambda_side_stack_restore_for(Context* context,
                                   LambdaSideStackSnapshot snapshot);
LambdaRecoveryCheckpoint lambda_recovery_checkpoint_capture_for(
    Context* context);
void lambda_recovery_checkpoint_restore_for(
    Context* context, LambdaRecoveryCheckpoint* checkpoint);
uint64_t* lambda_side_root_alloc_n_for(Context* context, size_t slot_count);
uint64_t* lambda_side_number_alloc_for(Context* context);
bool lambda_root_frame_begin_for(Context* context, LambdaRootFrame* frame,
                                 size_t slot_count);

#ifdef __cplusplus
}
#endif

#endif
