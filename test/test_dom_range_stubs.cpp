// Test-only stubs for the minimal DOM-range unit tests.
//
// dom_range_pre_remove() calls view_pool_release_detached_subtree() to release
// layout-owned handles from a detached subtree (see radiant/dom_range.cpp).
// That symbol lives in radiant/view_pool.cpp, which transitively pulls in the
// entire view/font/caret rendering stack (free_document, image_surface_destroy,
// caret snapshots, etc.). The DOM-range and source-position-bridge unit tests
// never build a view tree, so there are no view-owned resources to release —
// the operation is genuinely a no-op here. Provide a no-op definition so these
// deliberately-minimal tests link without the view-pool dependency chain.

#include "../lambda/lambda-data.hpp"
#include "../lambda/runtime/side_stack.h"

struct DomNode;

__thread EvalContext* context = nullptr;

void view_pool_release_detached_subtree(DomNode* root) {
    (void)root;
}

// render_map's retransform branch is linked into this focused bridge target,
// but none of these non-collecting tests invoke it. Keep its runtime-only
// dependencies unavailable so an accidental call cannot mutate test storage.
void expand_list(List*, Arena*) {}

extern "C" Context* eval_context_tls_runtime(void) {
    return (Context*)context;
}

extern "C" bool lambda_root_frame_begin(LambdaRootFrame*, size_t) {
    return false;
}

extern "C" uint64_t* lambda_root_frame_take_slot(LambdaRootFrame*) {
    return nullptr;
}

extern "C" void lambda_root_frame_end(LambdaRootFrame*) {}

extern "C" void lambda_root_frame_overflow_error(void) {}
