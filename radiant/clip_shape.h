#pragma once
#include <math.h>

// ============================================================================
// Vector clip shapes for overflow:hidden and CSS clip-path
// ============================================================================

enum ClipShapeType {
    CLIP_SHAPE_NONE = 0,
    CLIP_SHAPE_POLYGON,
    CLIP_SHAPE_CIRCLE,
    CLIP_SHAPE_ELLIPSE,
    CLIP_SHAPE_INSET,
    CLIP_SHAPE_ROUNDED_RECT
};

struct ClipShape {
    ClipShapeType type;
    union {
        struct { float* vx; float* vy; int count; } polygon;
        struct { float cx, cy, r; } circle;
        struct { float cx, cy, rx, ry; } ellipse;
        struct { float x, y, w, h, rx, ry; } inset;
        struct { float x, y, w, h, r_tl, r_tr, r_br, r_bl; } rounded_rect;
    };
};

#define RDT_MAX_CLIP_SHAPES 8

// Point-in-rounded-rect test
static inline bool clip_point_in_rounded_rect(float px, float py,
    float rx, float ry, float rw, float rh,
    float r_tl, float r_tr, float r_br, float r_bl) {
    if (px < rx || px > rx + rw || py < ry || py > ry + rh) return false;
    if (r_tl > 0 && px < rx + r_tl && py < ry + r_tl) {
        float dx = px - (rx + r_tl), dy = py - (ry + r_tl);
        if (dx * dx + dy * dy > r_tl * r_tl) return false;
    }
    if (r_tr > 0 && px > rx + rw - r_tr && py < ry + r_tr) {
        float dx = px - (rx + rw - r_tr), dy = py - (ry + r_tr);
        if (dx * dx + dy * dy > r_tr * r_tr) return false;
    }
    if (r_br > 0 && px > rx + rw - r_br && py > ry + rh - r_br) {
        float dx = px - (rx + rw - r_br), dy = py - (ry + rh - r_br);
        if (dx * dx + dy * dy > r_br * r_br) return false;
    }
    if (r_bl > 0 && px < rx + r_bl && py > ry + rh - r_bl) {
        float dx = px - (rx + r_bl), dy = py - (ry + rh - r_bl);
        if (dx * dx + dy * dy > r_bl * r_bl) return false;
    }
    return true;
}

// Compute scanline left/right bounds for a rounded rect at a given y-coordinate.
// Outputs the visible x-range [out_left, out_right] for the rounded rect interior at row y.
static inline void clip_scanline_rounded_rect(
    float rx, float ry, float rw, float rh,
    float r_tl, float r_tr, float r_br, float r_bl,
    float y, float* out_left, float* out_right) {

    *out_left = rx;
    *out_right = rx + rw;

    // top-left corner arc
    if (r_tl > 0 && y < ry + r_tl) {
        float cy = ry + r_tl;
        float dy = y - cy;
        float d2 = r_tl * r_tl - dy * dy;
        if (d2 > 0) {
            float xl = rx + r_tl - sqrtf(d2);
            if (xl > *out_left) *out_left = xl;
        } else {
            *out_left = rx + r_tl; // at the very top of the arc
        }
    }
    // top-right corner arc
    if (r_tr > 0 && y < ry + r_tr) {
        float cy = ry + r_tr;
        float dy = y - cy;
        float d2 = r_tr * r_tr - dy * dy;
        if (d2 > 0) {
            float xr = rx + rw - r_tr + sqrtf(d2);
            if (xr < *out_right) *out_right = xr;
        } else {
            *out_right = rx + rw - r_tr;
        }
    }
    // bottom-left corner arc
    if (r_bl > 0 && y > ry + rh - r_bl) {
        float cy = ry + rh - r_bl;
        float dy = y - cy;
        float d2 = r_bl * r_bl - dy * dy;
        if (d2 > 0) {
            float xl = rx + r_bl - sqrtf(d2);
            if (xl > *out_left) *out_left = xl;
        } else {
            *out_left = rx + r_bl;
        }
    }
    // bottom-right corner arc
    if (r_br > 0 && y > ry + rh - r_br) {
        float cy = ry + rh - r_br;
        float dy = y - cy;
        float d2 = r_br * r_br - dy * dy;
        if (d2 > 0) {
            float xr = rx + rw - r_br + sqrtf(d2);
            if (xr < *out_right) *out_right = xr;
        } else {
            *out_right = rx + rw - r_br;
        }
    }
}

// Point-in-circle test
static inline bool clip_point_in_circle(float px, float py, float cx, float cy, float r) {
    float dx = px - cx, dy = py - cy;
    return dx * dx + dy * dy <= r * r;
}

// Point-in-ellipse test
static inline bool clip_point_in_ellipse(float px, float py, float cx, float cy, float rx, float ry) {
    float dx = (px - cx) / rx, dy = (py - cy) / ry;
    return dx * dx + dy * dy <= 1.0f;
}

// Point-in-inset test (axis-aligned rect)
static inline bool clip_point_in_inset(float px, float py, float ix, float iy, float iw, float ih) {
    return px >= ix && px <= ix + iw && py >= iy && py <= iy + ih;
}

// Point-in-polygon test (ray casting)
static inline bool clip_point_in_polygon(float px, float py, const float* vx, const float* vy, int count) {
    bool inside = false;
    for (int i = 0, j = count - 1; i < count; j = i++) {
        if (((vy[i] > py) != (vy[j] > py)) &&
            (px < (vx[j] - vx[i]) * (py - vy[i]) / (vy[j] - vy[i]) + vx[i])) {
            inside = !inside;
        }
    }
    return inside;
}

// Point-in-clip-shape test (dispatches by type)
static inline bool clip_point_in_shape(ClipShape* cs, float px, float py) {
    if (!cs) return true;
    switch (cs->type) {
        case CLIP_SHAPE_ROUNDED_RECT:
            return clip_point_in_rounded_rect(px, py,
                cs->rounded_rect.x, cs->rounded_rect.y, cs->rounded_rect.w, cs->rounded_rect.h,
                cs->rounded_rect.r_tl, cs->rounded_rect.r_tr, cs->rounded_rect.r_br, cs->rounded_rect.r_bl);
        case CLIP_SHAPE_CIRCLE:
            return clip_point_in_circle(px, py, cs->circle.cx, cs->circle.cy, cs->circle.r);
        case CLIP_SHAPE_ELLIPSE:
            return clip_point_in_ellipse(px, py, cs->ellipse.cx, cs->ellipse.cy, cs->ellipse.rx, cs->ellipse.ry);
        case CLIP_SHAPE_INSET:
            return clip_point_in_inset(px, py, cs->inset.x, cs->inset.y, cs->inset.w, cs->inset.h);
        case CLIP_SHAPE_POLYGON:
            return clip_point_in_polygon(px, py, cs->polygon.vx, cs->polygon.vy, cs->polygon.count);
        default: return true;
    }
}

// Compute scanline left/right bounds for a circle at a given y-coordinate.
static inline void clip_scanline_circle(float cx, float cy, float r,
    float y, float* out_left, float* out_right) {
    float dy = y - cy;
    float d2 = r * r - dy * dy;
    if (d2 > 0) {
        float dx = sqrtf(d2);
        *out_left = cx - dx;
        *out_right = cx + dx;
    } else {
        *out_left = cx;
        *out_right = cx;
    }
}

// Compute scanline left/right bounds for an ellipse at a given y-coordinate.
static inline void clip_scanline_ellipse(float cx, float cy, float rx, float ry,
    float y, float* out_left, float* out_right) {
    float dy = (y - cy) / ry;
    float d2 = 1.0f - dy * dy;
    if (d2 > 0) {
        float dx = sqrtf(d2) * rx;
        *out_left = cx - dx;
        *out_right = cx + dx;
    } else {
        *out_left = cx;
        *out_right = cx;
    }
}

// Compute scanline left/right bounds for a polygon at a given y-coordinate.
// Uses edge intersection: find all x-coordinates where polygon edges cross y,
// then take the min and max of those intersections.
static inline void clip_scanline_polygon(const float* vx, const float* vy, int count,
    float y, float* out_left, float* out_right) {
    float x_min = 1e9f, x_max = -1e9f;
    int crossings = 0;
    for (int i = 0, j = count - 1; i < count; j = i++) {
        float y0 = vy[j], y1 = vy[i];
        if ((y0 <= y && y1 > y) || (y1 <= y && y0 > y)) {
            float x = vx[j] + (y - y0) / (y1 - y0) * (vx[i] - vx[j]);
            if (x < x_min) x_min = x;
            if (x > x_max) x_max = x;
            crossings++;
        }
    }
    if (crossings >= 2) {
        *out_left = x_min;
        *out_right = x_max;
    } else {
        *out_left = *out_right = 0;
    }
}

// Test if an axis-aligned rect is entirely inside a single clip shape.
static inline bool clip_shape_rect_inside(ClipShape* cs, float x, float y, float w, float h) {
    if (!cs || cs->type == CLIP_SHAPE_NONE) return true;
    return clip_point_in_shape(cs, x, y) &&
           clip_point_in_shape(cs, x + w, y) &&
           clip_point_in_shape(cs, x, y + h) &&
           clip_point_in_shape(cs, x + w, y + h);
}

// Test if an axis-aligned rect is entirely inside ALL clip shapes in the stack.
static inline bool clip_shapes_rect_inside(ClipShape** shapes, int depth,
    float x, float y, float w, float h) {
    for (int i = 0; i < depth; i++) {
        if (!clip_shape_rect_inside(shapes[i], x, y, w, h)) return false;
    }
    return true;
}

// Compute the tightest scanline [left, right] at row y across ALL clip shapes.
// Intersects with [base_left, base_right] from the axis-aligned clip.
static inline void clip_shapes_scanline_bounds(ClipShape** shapes, int depth,
    float y, int base_left, int base_right, int* out_left, int* out_right) {
    int left = base_left, right = base_right;
    for (int i = 0; i < depth; i++) {
        ClipShape* cs = shapes[i];
        float sl, sr;
        switch (cs->type) {
            case CLIP_SHAPE_ROUNDED_RECT: {
                auto& rr = cs->rounded_rect;
                clip_scanline_rounded_rect(rr.x, rr.y, rr.w, rr.h,
                    rr.r_tl, rr.r_tr, rr.r_br, rr.r_bl,
                    y, &sl, &sr);
                int il = (int)ceilf(sl);
                int ir = (int)floorf(sr);
                if (il > left) left = il;
                if (ir < right) right = ir;
                break;
            }
            case CLIP_SHAPE_CIRCLE:
                clip_scanline_circle(cs->circle.cx, cs->circle.cy, cs->circle.r, y, &sl, &sr);
                if ((int)ceilf(sl) > left) left = (int)ceilf(sl);
                if ((int)floorf(sr) < right) right = (int)floorf(sr);
                break;
            case CLIP_SHAPE_ELLIPSE:
                clip_scanline_ellipse(cs->ellipse.cx, cs->ellipse.cy, cs->ellipse.rx, cs->ellipse.ry, y, &sl, &sr);
                if ((int)ceilf(sl) > left) left = (int)ceilf(sl);
                if ((int)floorf(sr) < right) right = (int)floorf(sr);
                break;
            case CLIP_SHAPE_INSET:
                if ((int)ceilf(cs->inset.x) > left) left = (int)ceilf(cs->inset.x);
                if ((int)floorf(cs->inset.x + cs->inset.w) < right) right = (int)floorf(cs->inset.x + cs->inset.w);
                break;
            case CLIP_SHAPE_POLYGON:
                clip_scanline_polygon(cs->polygon.vx, cs->polygon.vy, cs->polygon.count, y, &sl, &sr);
                if ((int)ceilf(sl) > left) left = (int)ceilf(sl);
                if ((int)floorf(sr) < right) right = (int)floorf(sr);
                break;
            default: break;
        }
    }
    *out_left = left;
    *out_right = right;
}

// Build a stack-local ClipShape from serialized DL parameters (type + float[8]).
// Used by display list replay to reconstruct clip shape for post-blur masking.
static inline ClipShape clip_shape_from_params(int type, const float* params) {
    ClipShape cs = {};
    cs.type = (ClipShapeType)type;
    switch (cs.type) {
        case CLIP_SHAPE_CIRCLE:
            cs.circle = {params[0], params[1], params[2]};
            break;
        case CLIP_SHAPE_ELLIPSE:
            cs.ellipse = {params[0], params[1], params[2], params[3]};
            break;
        case CLIP_SHAPE_INSET:
            cs.inset = {params[0], params[1], params[2], params[3], params[4], params[5]};
            break;
        case CLIP_SHAPE_ROUNDED_RECT:
            cs.rounded_rect = {params[0], params[1], params[2], params[3],
                               params[4], params[5], params[6], params[7]};
            break;
        default: break;
    }
    return cs;
}

// Serialize a ClipShape into type + float[8] for DL recording.
static inline void clip_shape_to_params(const ClipShape* cs, int* out_type, float* out_params) {
    *out_type = cs ? (int)cs->type : 0;
    memset(out_params, 0, 8 * sizeof(float));
    if (!cs) return;
    switch (cs->type) {
        case CLIP_SHAPE_CIRCLE:
            out_params[0] = cs->circle.cx; out_params[1] = cs->circle.cy;
            out_params[2] = cs->circle.r;
            break;
        case CLIP_SHAPE_ELLIPSE:
            out_params[0] = cs->ellipse.cx; out_params[1] = cs->ellipse.cy;
            out_params[2] = cs->ellipse.rx; out_params[3] = cs->ellipse.ry;
            break;
        case CLIP_SHAPE_INSET:
            out_params[0] = cs->inset.x;  out_params[1] = cs->inset.y;
            out_params[2] = cs->inset.w;  out_params[3] = cs->inset.h;
            out_params[4] = cs->inset.rx; out_params[5] = cs->inset.ry;
            break;
        case CLIP_SHAPE_ROUNDED_RECT:
            out_params[0] = cs->rounded_rect.x;    out_params[1] = cs->rounded_rect.y;
            out_params[2] = cs->rounded_rect.w;    out_params[3] = cs->rounded_rect.h;
            out_params[4] = cs->rounded_rect.r_tl; out_params[5] = cs->rounded_rect.r_tr;
            out_params[6] = cs->rounded_rect.r_br; out_params[7] = cs->rounded_rect.r_bl;
            break;
        default: break;
    }
}
