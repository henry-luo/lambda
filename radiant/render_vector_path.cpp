#include "render.hpp"

#include "../lib/log.h"

void render_vector_path(RenderContext* rdcon, ViewBlock* block) {
    VectorPathProp* vpath = block->vector_path();
    if (!vpath || !vpath->segments) return;

    log_info("[VPATH] Rendering vector path for block at (%.1f, %.1f)", block->x, block->y);

    RdtPath* p = rdt_path_new();
    if (!p) {
        log_error("[VPATH] Failed to create RdtPath");
        return;
    }

    float offset_x = rdcon->block.x + block->x;
    float offset_y = rdcon->block.y + block->y;

    for (VectorPathSegment* seg = vpath->segments; seg; seg = seg->next) {
        float sx = offset_x + seg->x;
        float sy = offset_y + seg->y;

        switch (seg->type) {
            case VectorPathSegment::VPATH_MOVETO:
                rdt_path_move_to(p, sx, sy);
                log_debug("[VPATH] moveto (%.1f, %.1f)", sx, sy);
                break;
            case VectorPathSegment::VPATH_LINETO:
                rdt_path_line_to(p, sx, sy);
                log_debug("[VPATH] lineto (%.1f, %.1f)", sx, sy);
                break;
            case VectorPathSegment::VPATH_CURVETO: {
                float cx1 = offset_x + seg->x1;
                float cy1 = offset_y + seg->y1;
                float cx2 = offset_x + seg->x2;
                float cy2 = offset_y + seg->y2;
                rdt_path_cubic_to(p, cx1, cy1, cx2, cy2, sx, sy);
                log_debug("[VPATH] curveto (%.1f,%.1f)-(%.1f,%.1f)->(%.1f,%.1f)",
                          cx1, cy1, cx2, cy2, sx, sy);
                break;
            }
            case VectorPathSegment::VPATH_CLOSE:
                rdt_path_close(p);
                log_debug("[VPATH] close");
                break;
        }
    }

    if (vpath->has_stroke) {
        rc_stroke_path(rdcon, p, vpath->stroke_color, vpath->stroke_width,
                       RDT_CAP_BUTT, RDT_JOIN_MITER,
                       vpath->dash_pattern, vpath->dash_pattern_length > 0 ? vpath->dash_pattern_length : 0,
                       NULL);

        if (vpath->dash_pattern && vpath->dash_pattern_length > 0) {
            log_debug("[VPATH] Setting dash pattern: count=%d, values=[%.1f, %.1f]",
                      vpath->dash_pattern_length,
                      vpath->dash_pattern[0],
                      vpath->dash_pattern_length > 1 ? vpath->dash_pattern[1] : 0.0f);
        }

        log_debug("[VPATH] Stroke: RGB(%d,%d,%d) width=%.1f",
                  vpath->stroke_color.r, vpath->stroke_color.g, vpath->stroke_color.b,
                  vpath->stroke_width);
    }

    if (vpath->has_fill) {
        rc_fill_path(rdcon, p, vpath->fill_color, RDT_FILL_WINDING, NULL);
    }

    rdt_path_free(p);
    log_info("[VPATH] Rendered vector path successfully");
}
