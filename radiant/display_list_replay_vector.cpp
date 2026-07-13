#include "render.hpp"

DisplayReplayVectorResult dl_replay_vector_item(RdtVector* vec,
                                                DisplayItem* item,
                                                bool duplicate_picture) {
    if (!vec || !item) return DL_REPLAY_VECTOR_NOT_HANDLED;

    if (dl_replay_vector_clip_item(vec, item)) {
        return DL_REPLAY_VECTOR_HANDLED;
    }

    switch (item->op) {
        case DL_FILL_RECT: {
            DlFillRect* r = &item->fill_rect;
            rdt_fill_rect(vec, r->x, r->y, r->w, r->h, r->color);
            return DL_REPLAY_VECTOR_DREW;
        }

        case DL_FILL_ROUNDED_RECT: {
            DlFillRoundedRect* r = &item->fill_rounded_rect;
            rdt_fill_rounded_rect(vec, r->x, r->y, r->w, r->h, r->rx, r->ry, r->color);
            return DL_REPLAY_VECTOR_DREW;
        }

        case DL_FILL_PATH: {
            DlFillPath* r = &item->fill_path;
            rdt_fill_path(vec, r->path, r->color, r->rule,
                          r->has_transform ? &r->transform : nullptr);
            return DL_REPLAY_VECTOR_DREW;
        }

        case DL_STROKE_PATH: {
            DlStrokePath* r = &item->stroke_path;
            rdt_stroke_path(vec, r->path, r->color, r->width, r->cap, r->join,
                            r->dash_array, r->dash_count, r->dash_phase,
                            r->has_transform ? &r->transform : nullptr);
            return DL_REPLAY_VECTOR_DREW;
        }

        case DL_FILL_LINEAR_GRADIENT: {
            DlFillLinearGradient* r = &item->fill_linear_gradient;
            rdt_fill_linear_gradient(vec, r->path, r->x1, r->y1, r->x2, r->y2,
                                     r->stops, r->stop_count, r->rule,
                                     r->has_transform ? &r->transform : nullptr,
                                     r->has_gradient_transform ? &r->gradient_transform : nullptr);
            return DL_REPLAY_VECTOR_DREW;
        }

        case DL_FILL_RADIAL_GRADIENT: {
            DlFillRadialGradient* r = &item->fill_radial_gradient;
            rdt_fill_radial_gradient(vec, r->path, r->cx, r->cy, r->r,
                                     r->stops, r->stop_count, r->rule,
                                     r->has_transform ? &r->transform : nullptr,
                                     r->has_gradient_transform ? &r->gradient_transform : nullptr);
            return DL_REPLAY_VECTOR_DREW;
        }

        case DL_DRAW_IMAGE: {
            DlDrawImage* r = &item->draw_image;
            rdt_draw_image(vec, r->pixels, r->src_w, r->src_h, r->src_stride,
                           r->dst_x, r->dst_y, r->dst_w, r->dst_h, r->opacity,
                           r->has_transform ? &r->transform : nullptr,
                           r->resource_generation);
            return DL_REPLAY_VECTOR_DREW;
        }

        case DL_DRAW_PICTURE: {
            DlDrawPicture* r = &item->draw_picture;
            if (duplicate_picture) {
                rdt_picture_draw_dup(vec, r->picture, r->opacity,
                                     r->has_transform ? &r->transform : nullptr);
            } else {
                rdt_picture_draw(vec, r->picture, r->opacity,
                                 r->has_transform ? &r->transform : nullptr);
            }
            return DL_REPLAY_VECTOR_DREW;
        }

        default:
            return DL_REPLAY_VECTOR_NOT_HANDLED;
    }
}

bool dl_replay_vector_clip_item(RdtVector* vec, DisplayItem* item) {
    if (!vec || !item) return false;

    switch (item->op) {
        case DL_PUSH_CLIP: {
            DlPushClip* r = &item->push_clip;
            rdt_push_clip(vec, r->path,
                          r->has_transform ? &r->transform : nullptr);
            return true;
        }

        case DL_POP_CLIP:
            rdt_pop_clip(vec);
            return true;

        default:
            return false;
    }
}
