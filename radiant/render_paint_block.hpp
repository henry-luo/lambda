#pragma once

struct ViewBlock;

typedef struct RenderPaintBlockOps {
    void* ctx;
    bool (*begin)(void* ctx, ViewBlock* block, void** phase);
    bool (*paint_self)(void* ctx, ViewBlock* block, void* phase);
    double (*paint_children)(void* ctx, ViewBlock* block, void* phase);
    void (*finish)(void* ctx, ViewBlock* block, void* phase);
} RenderPaintBlockOps;

typedef struct RenderPaintBlockResult {
    double children_time;
    bool painted;
} RenderPaintBlockResult;

RenderPaintBlockResult render_paint_block_run(RenderPaintBlockOps* ops,
                                              ViewBlock* block);
