#include "recovery_frame.h"

#include <string.h>

#include "../../lib/log.h"
#include "../../lib/memtrack.h"

extern void* eval_context_tls_runtime(void);

#if defined(_MSC_VER)
#define RECOVERY_FRAME_TLS __declspec(thread)
#else
#define RECOVERY_FRAME_TLS __thread
#endif

RECOVERY_FRAME_TLS LambdaRecoveryFrame* lambda_recovery_frame_tls_top = NULL;
static RECOVERY_FRAME_TLS LambdaFaultRecord lambda_recovery_fault_fallback;

uint64_t lambda_recovery_frame_static_fault_item(LambdaRecoveryFrame* frame) {
    if (!frame || !frame->fault.active) return ITEM_ERROR;
    // A landed local frame must be popped before its handler executes. Copying
    // the embedded record here prevents its Error Item from pointing into the
    // heap frame that `lambda_recovery_frame_end` is about to release.
    lambda_recovery_fault_fallback = frame->fault;
    return (uint64_t)err2it(&lambda_recovery_fault_fallback.error);
}

LambdaRecoveryFrame* lambda_recovery_frame_current(void) {
    return lambda_recovery_frame_tls_top;
}

bool lambda_recovery_frame_push_for(Context* runtime_context,
                                    LambdaRecoveryFrame* frame,
                                    uint32_t capabilities) {
    if (!runtime_context || !frame ||
        (frame->state != LAMBDA_RECOVERY_FRAME_EMPTY &&
         frame->state != LAMBDA_RECOVERY_FRAME_DISARMED)) {
        return false;
    }

    memset(frame, 0, sizeof(*frame));
    frame->previous = lambda_recovery_frame_tls_top;
    frame->context = runtime_context;
    frame->checkpoint = lambda_recovery_checkpoint_capture_for(runtime_context);
    lambda_fault_record_init(&frame->fault);
    frame->capabilities = capabilities;
    frame->state = LAMBDA_RECOVERY_FRAME_PREPARED;
    lambda_recovery_frame_tls_top = frame;
    return true;
}

bool lambda_recovery_frame_push(LambdaRecoveryFrame* frame, uint32_t capabilities) {
    return lambda_recovery_frame_push_for(
        (Context*)eval_context_tls_runtime(), frame, capabilities);
}

bool lambda_recovery_frame_arm(LambdaRecoveryFrame* frame) {
    if (!frame || lambda_recovery_frame_tls_top != frame ||
        frame->state != LAMBDA_RECOVERY_FRAME_PREPARED) {
        return false;
    }
    frame->state = LAMBDA_RECOVERY_FRAME_ARMED;
    frame->signal_armed = 1;
    return true;
}

bool lambda_recovery_frame_prepare_fault(LambdaRecoveryFrame* frame,
                                         LambdaFaultReason reason,
                                         LambdaErrorCode prior_error_code) {
    if (!frame || lambda_recovery_frame_tls_top != frame ||
        frame->state != LAMBDA_RECOVERY_FRAME_ARMED ||
        reason == LAMBDA_FAULT_NONE) {
        return false;
    }
    lambda_fault_record_prepare(&frame->fault, reason, prior_error_code);
    return true;
}

bool lambda_recovery_frame_restore_landing(LambdaRecoveryFrame* frame) {
    if (!frame || lambda_recovery_frame_tls_top != frame ||
        frame->state != LAMBDA_RECOVERY_FRAME_ARMED ||
        !frame->checkpoint.active) {
        return false;
    }
    frame->signal_armed = 0;
    LambdaFaultReason signal_reason =
        (LambdaFaultReason)frame->signal_fault_reason;
    if (signal_reason != LAMBDA_FAULT_NONE && !frame->fault.active) {
        lambda_fault_record_prepare(&frame->fault, signal_reason, ERR_OK);
    }
    // Landing must restore every tracked watermark before its caller inspects
    // the fault or allocates, because the jump skipped generated epilogues.
    lambda_recovery_checkpoint_restore_for(frame->context, &frame->checkpoint);
    frame->state = LAMBDA_RECOVERY_FRAME_LANDED;
    return true;
}

bool lambda_recovery_frame_pop(LambdaRecoveryFrame* frame) {
    if (!frame || lambda_recovery_frame_tls_top != frame) {
        // A stale outer target must never be removed while an inner frame owns
        // the TLS top; that would make a later signal jump into returned code.
        log_error("recovery-frame: rejected non-LIFO pop");
        return false;
    }
    frame->signal_armed = 0;
    lambda_recovery_checkpoint_disarm(&frame->checkpoint);
    lambda_recovery_frame_tls_top = frame->previous;
    frame->previous = NULL;
    frame->state = LAMBDA_RECOVERY_FRAME_DISARMED;
    return true;
}

LambdaRecoveryFrame* lambda_recovery_frame_begin_for(Context* runtime_context,
                                                      uint32_t capabilities) {
    if (!runtime_context) return NULL;
    LambdaRecoveryFrame* frame =
        (LambdaRecoveryFrame*)mem_alloc(sizeof(LambdaRecoveryFrame), MEM_CAT_EVAL);
    if (!frame) return NULL;
    memset(frame, 0, sizeof(*frame));
    if (!lambda_recovery_frame_push_for(runtime_context, frame, capabilities)) {
        mem_free(frame);
        return NULL;
    }
    return frame;
}

bool lambda_recovery_frame_end(LambdaRecoveryFrame* frame) {
    if (!lambda_recovery_frame_pop(frame)) return false;
    mem_free(frame);
    return true;
}
