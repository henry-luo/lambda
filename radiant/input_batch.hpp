#pragma once

#include "event.hpp"

typedef void (*RdtInputDispatch)(void* data, RdtEvent* event);

struct RdtInputBatch {
    RdtEvent motion;
    bool pending_motion;
};

inline bool rdt_input_batch_flush(RdtInputBatch* batch, RdtInputDispatch dispatch, void* data) {
    if (!batch->pending_motion) return false;
    RdtEvent motion = batch->motion;
    // dispatch may reenter the host; consume this sample before calling user code.
    batch->pending_motion = false;
    dispatch(data, &motion);
    return true;
}

inline void rdt_input_batch_submit(RdtInputBatch* batch, RdtEvent* event,
                                   RdtInputDispatch dispatch, void* data) {
    if (event->type == RDT_EVENT_MOUSE_MOVE) {
        // platform polling must stay cheap enough to reach paint under continuous input.
        batch->motion = *event;
        batch->pending_motion = true;
    } else {
        rdt_input_batch_flush(batch, dispatch, data);
        dispatch(data, event);
    }
}
