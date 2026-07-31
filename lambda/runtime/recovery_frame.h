#ifndef LAMBDA_RECOVERY_FRAME_H
#define LAMBDA_RECOVERY_FRAME_H

#include <setjmp.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>

#include "../lambda.h"
#include "lambda-error.h"
#include "side_stack.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum LambdaRecoveryFrameCapability {
    LAMBDA_RECOVERY_CAP_LOCAL_FAULT = 1u << 0,
    LAMBDA_RECOVERY_CAP_EXECUTION_BOUNDARY = 1u << 1,
    LAMBDA_RECOVERY_CAP_TRANSACTION_BARRIER = 1u << 2,
    LAMBDA_RECOVERY_CAP_TEST_CONTAINMENT = 1u << 3,
} LambdaRecoveryFrameCapability;

typedef enum LambdaRecoveryFrameState {
    LAMBDA_RECOVERY_FRAME_EMPTY = 0,
    LAMBDA_RECOVERY_FRAME_PREPARED,
    LAMBDA_RECOVERY_FRAME_ARMED,
    LAMBDA_RECOVERY_FRAME_LANDED,
    LAMBDA_RECOVERY_FRAME_DISARMED,
} LambdaRecoveryFrameState;

// Recovery frames are a TLS LIFO. The jump buffer lives in the native caller
// that will receive a non-local jump; no helper may set it and then return.
typedef struct LambdaRecoveryFrame {
    struct LambdaRecoveryFrame* previous;
    Context* context;
    LambdaRecoveryCheckpoint checkpoint;
    LambdaFaultRecord fault;
#if defined(__APPLE__) || defined(__linux__)
    sigjmp_buf jump_buffer;
#else
    jmp_buf jump_buffer;
#endif
    uint32_t capabilities;
    LambdaRecoveryFrameState state;
    // The stack-fault origin reads only these signal-safe fields before it
    // jumps; reporting and fault-record construction stay at the landing.
    volatile sig_atomic_t signal_armed;
    volatile sig_atomic_t signal_fault_reason;
} LambdaRecoveryFrame;

// POSIX signal handling cannot call a helper to discover the target. The
// stack-overflow origin reads this TLS top directly, then follows `previous`
// to the nearest armed execution boundary.
#if defined(_MSC_VER)
extern __declspec(thread) LambdaRecoveryFrame* lambda_recovery_frame_tls_top;
#else
extern __thread LambdaRecoveryFrame* lambda_recovery_frame_tls_top;
#endif

// `setjmp` must expand in the native function that owns the corresponding
// recovery frame. These macros deliberately contain no helper call.
#if defined(__APPLE__) || defined(__linux__)
#define LAMBDA_RECOVERY_FRAME_SETJMP(frame) sigsetjmp((frame)->jump_buffer, 1)
#define LAMBDA_RECOVERY_FRAME_LONGJMP(frame) siglongjmp((frame)->jump_buffer, 1)
#define LAMBDA_RECOVERY_FRAME_MIR_CHECKPOINT_NAME "sigsetjmp"
#else
#define LAMBDA_RECOVERY_FRAME_SETJMP(frame) setjmp((frame)->jump_buffer)
#define LAMBDA_RECOVERY_FRAME_LONGJMP(frame) longjmp((frame)->jump_buffer, 1)
#define LAMBDA_RECOVERY_FRAME_MIR_CHECKPOINT_NAME "setjmp"
#endif

// MIR must pass this address to the platform primitive directly. A wrapper
// that calls setjmp and returns would leave no live native activation to land.
#define LAMBDA_RECOVERY_FRAME_JUMP_BUFFER_OFFSET \
    offsetof(LambdaRecoveryFrame, jump_buffer)

LambdaRecoveryFrame* lambda_recovery_frame_current(void);
bool lambda_recovery_frame_push(LambdaRecoveryFrame* frame, uint32_t capabilities);
bool lambda_recovery_frame_arm(LambdaRecoveryFrame* frame);
bool lambda_recovery_frame_prepare_fault(LambdaRecoveryFrame* frame,
                                         LambdaFaultReason reason,
                                         LambdaErrorCode prior_error_code);
bool lambda_recovery_frame_restore_landing(LambdaRecoveryFrame* frame);
bool lambda_recovery_frame_pop(LambdaRecoveryFrame* frame);

// Transfers an active frame record into durable thread-local static storage.
// It is intentionally not a checkpoint wrapper; MIR owns the direct platform
// setjmp call site.
uint64_t lambda_recovery_frame_static_fault_item(LambdaRecoveryFrame* frame);

// Publishes that durable fault Item to the current evaluator after a restored
// landing, then returns it for the local `^err` binding.
Item lambda_recovery_frame_fault_item(Context* context,
                                      LambdaRecoveryFrame* frame);

// Execution entries use heap-backed frame storage because a non-local jump
// leaves automatic objects modified after setjmp indeterminate. The caller's
// `setjmp` still lives in its own native activation.
LambdaRecoveryFrame* lambda_recovery_frame_begin_for(Context* context,
                                                      uint32_t capabilities);
bool lambda_recovery_frame_end(LambdaRecoveryFrame* frame);

// Explicit-owner setup is for tests and control-plane code. Runtime execution
// pushes through the TLS-owner form above.
bool lambda_recovery_frame_push_for(Context* context, LambdaRecoveryFrame* frame,
                                    uint32_t capabilities);

#ifdef __cplusplus
}
#endif

#endif
