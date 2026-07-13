#include "render.hpp"

RenderPaintBlockResult render_paint_block_run(RenderPaintBlockOps* ops,
                                              ViewBlock* block) {
    RenderPaintBlockResult result = {};
    if (!ops || !block || !ops->begin) return result;

    void* phase = nullptr;
    if (!ops->begin(ops->ctx, block, &phase)) {
        return result;
    }

    bool paint_children = true;
    if (ops->paint_self) {
        paint_children = ops->paint_self(ops->ctx, block, phase);
    }
    if (paint_children && ops->paint_children) {
        result.children_time = ops->paint_children(ops->ctx, block, phase);
    }
    if (ops->finish) {
        ops->finish(ops->ctx, block, phase);
    }

    result.painted = true;
    return result;
}
