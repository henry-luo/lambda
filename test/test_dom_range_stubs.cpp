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

struct DomNode;

void view_pool_release_detached_subtree(DomNode* root) {
    (void)root;
}
