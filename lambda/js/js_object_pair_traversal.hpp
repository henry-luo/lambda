#pragma once

#include "../lambda.hpp"
#include "../runtime/side_stack.h"
#include "../../lib/log.h"

// active object pairs live directly in the precise root side-stack.  Keeping
// the traversal watermark here avoids a second native pair stack and lets the
// collector see every pair while allocation-capable property work recurses.
struct JsObjectPairTraversal {
    Context* runtime_context;
    uint64_t* root_base;
    int depth;

    JsObjectPairTraversal()
        : runtime_context((Context*)eval_context_tls_runtime()),
          root_base(NULL), depth(0) {}

    int enter(Item left, Item right) {
        if (!runtime_context) return 1;
        if (depth > 0) {
            uint64_t* expected_top = root_base + (size_t)depth * 2;
            if (runtime_context->side_root_top != expected_top) {
                log_error("js-pair-traversal: root stack is not at pair watermark");
                return -1;
            }
        } else {
            root_base = runtime_context->side_root_top;
        }

        for (int i = 0; i < depth; i++) {
            Item active_left = pair_left(i);
            Item active_right = pair_right(i);
            if (active_left.item == left.item && active_right.item == right.item) return 0;
            if (active_left.item == left.item || active_right.item == right.item) return -1;
        }

        uint64_t* slots = lambda_side_root_alloc_n_for(runtime_context, 2);
        if (!slots) return -1;
        if (depth == 0) root_base = slots;
        slots[0] = left.item;
        slots[1] = right.item;
        depth++;
        return 1;
    }

    void leave() {
        if (!runtime_context || depth <= 0) return;
        uint64_t* expected_top = root_base + (size_t)depth * 2;
        if (runtime_context->side_root_top != expected_top) {
            log_error("js-pair-traversal: root stack is not at leave watermark");
            return;
        }
        if (!lambda_side_root_pop_n_for(runtime_context, 2)) {
            log_error("js-pair-traversal: failed to pop pair roots");
            return;
        }
        depth--;
        if (depth == 0) root_base = NULL;
    }

private:
    Item pair_left(int index) const {
        return (Item){.item = root_base[(size_t)index * 2]};
    }

    Item pair_right(int index) const {
        return (Item){.item = root_base[(size_t)index * 2 + 1]};
    }
};
