#include "render_clip.hpp"
#include "render_path.hpp"

#include <math.h>
#include <string.h>
#include <stdlib.h>

RdtPath* render_clip_create_rounded_rect_path(float x, float y, float w, float h,
                                              float r_tl, float r_tr,
                                              float r_br, float r_bl) {
    float max_rx = w * 0.5f, max_ry = h * 0.5f;
    if (r_tl > max_rx) r_tl = max_rx; if (r_tl > max_ry) r_tl = max_ry;
    if (r_tr > max_rx) r_tr = max_rx; if (r_tr > max_ry) r_tr = max_ry;
    if (r_br > max_rx) r_br = max_rx; if (r_br > max_ry) r_br = max_ry;
    if (r_bl > max_rx) r_bl = max_rx; if (r_bl > max_ry) r_bl = max_ry;
    Corner radius = {};
    radius.top_left = r_tl;
    radius.top_right = r_tr;
    radius.bottom_right = r_br;
    radius.bottom_left = r_bl;
    radius.top_left_y = r_tl;
    radius.top_right_y = r_tr;
    radius.bottom_right_y = r_br;
    radius.bottom_left_y = r_bl;
    Rect rect = {x, y, w, h};
    return render_path_create_rounded_rect(rect, &radius);
}

RdtPath* render_clip_create_shape_path(ClipShape* shape) {
    if (!shape) return nullptr;
    RdtPath* p = rdt_path_new();
    switch (shape->type) {
        case CLIP_SHAPE_ROUNDED_RECT: {
            auto& rr = shape->rounded_rect;
            rdt_path_free(p);
            return render_clip_create_rounded_rect_path(rr.x, rr.y, rr.w, rr.h,
                rr.r_tl, rr.r_tr, rr.r_br, rr.r_bl);
        }
        case CLIP_SHAPE_CIRCLE:
            rdt_path_add_circle(p, shape->circle.cx, shape->circle.cy,
                                shape->circle.r, shape->circle.r);
            break;
        case CLIP_SHAPE_ELLIPSE:
            rdt_path_add_circle(p, shape->ellipse.cx, shape->ellipse.cy,
                                shape->ellipse.rx, shape->ellipse.ry);
            break;
        case CLIP_SHAPE_INSET:
            rdt_path_add_rect(p, shape->inset.x, shape->inset.y,
                              shape->inset.w, shape->inset.h, 0, 0);
            break;
        case CLIP_SHAPE_POLYGON:
            if (shape->polygon.count >= 3) {
                rdt_path_move_to(p, shape->polygon.vx[0], shape->polygon.vy[0]);
                for (int i = 1; i < shape->polygon.count; i++) {
                    rdt_path_line_to(p, shape->polygon.vx[i], shape->polygon.vy[i]);
                }
                rdt_path_close(p);
            }
            break;
        default:
            break;
    }
    return p;
}

void render_clip_free_shape(ScratchArena* scratch, ClipShape* shape) {
    if (!scratch || !shape) return;
    if (shape->type == CLIP_SHAPE_POLYGON) {
        scratch_free(scratch, shape->polygon.vy);
        scratch_free(scratch, shape->polygon.vx);
    }
    scratch_free(scratch, shape);
}

static float render_clip_parse_len(const char*& s, float ref) {
    while (*s == ' ' || *s == ',') s++;
    float val = strtof(s, (char**)&s);
    while (*s == ' ') s++;
    if (*s == '%') { s++; return val / 100.0f * ref; }
    if (s[0] == 'p' && s[1] == 'x') s += 2;
    return val;
}

ClipShape* render_clip_parse_css_shape(ScratchArena* scratch, const char* value,
                                       float elem_w, float elem_h,
                                       float abs_x, float abs_y) {
    if (!scratch || !value || strncmp(value, "none", 4) == 0) return nullptr;

    if (strncmp(value, "inset(", 6) == 0) {
        const char* s = value + 6;
        float vals[4] = {0};
        int val_count = 0;
        while (*s && *s != ')' && val_count < 4) {
            while (*s == ' ') s++;
            if (*s == ')' || strncmp(s, "round", 5) == 0) break;
            float ref = (val_count == 0 || val_count == 2) ? elem_h : elem_w;
            vals[val_count++] = render_clip_parse_len(s, ref);
        }
        float top, right_v, bottom, left_v;
        if (val_count == 1)      { top = right_v = bottom = left_v = vals[0]; }
        else if (val_count == 2) { top = bottom = vals[0]; right_v = left_v = vals[1]; }
        else if (val_count == 3) { top = vals[0]; right_v = left_v = vals[1]; bottom = vals[2]; }
        else                     { top = vals[0]; right_v = vals[1]; bottom = vals[2]; left_v = vals[3]; }
        float rx = 0, ry = 0;
        while (*s == ' ') s++;
        if (strncmp(s, "round", 5) == 0) {
            s += 5;
            rx = render_clip_parse_len(s, elem_w);
            ry = rx;
        }
        ClipShape* cs = (ClipShape*)scratch_calloc(scratch, sizeof(ClipShape));
        if (rx > 0 || ry > 0) {
            cs->type = CLIP_SHAPE_ROUNDED_RECT;
            cs->rounded_rect = {abs_x + left_v, abs_y + top,
                                elem_w - left_v - right_v, elem_h - top - bottom,
                                rx, rx, rx, rx};
        } else {
            cs->type = CLIP_SHAPE_INSET;
            cs->inset = {abs_x + left_v, abs_y + top,
                         elem_w - left_v - right_v, elem_h - top - bottom, 0, 0};
        }
        return cs;
    }

    if (strncmp(value, "circle(", 7) == 0) {
        const char* s = value + 7;
        float ref = fmin(elem_w, elem_h);
        float r = render_clip_parse_len(s, ref);
        float cx = abs_x + elem_w * 0.5f, cy = abs_y + elem_h * 0.5f;
        while (*s == ' ') s++;
        if (strncmp(s, "at", 2) == 0) {
            s += 2;
            cx = abs_x + render_clip_parse_len(s, elem_w);
            cy = abs_y + render_clip_parse_len(s, elem_h);
        }
        ClipShape* cs = (ClipShape*)scratch_calloc(scratch, sizeof(ClipShape));
        cs->type = CLIP_SHAPE_CIRCLE;
        cs->circle = {cx, cy, r};
        return cs;
    }

    if (strncmp(value, "ellipse(", 8) == 0) {
        const char* s = value + 8;
        float rx = render_clip_parse_len(s, elem_w);
        float ry = render_clip_parse_len(s, elem_h);
        float cx = abs_x + elem_w * 0.5f, cy = abs_y + elem_h * 0.5f;
        while (*s == ' ') s++;
        if (strncmp(s, "at", 2) == 0) {
            s += 2;
            cx = abs_x + render_clip_parse_len(s, elem_w);
            cy = abs_y + render_clip_parse_len(s, elem_h);
        }
        ClipShape* cs = (ClipShape*)scratch_calloc(scratch, sizeof(ClipShape));
        cs->type = CLIP_SHAPE_ELLIPSE;
        cs->ellipse = {cx, cy, rx, ry};
        return cs;
    }

    if (strncmp(value, "polygon(", 8) == 0) {
        const char* s = value + 8;
        const char* scan = s;
        int count = 0;
        while (*scan && *scan != ')') {
            while (*scan == ' ' || *scan == ',') scan++;
            if (*scan == ')') break;
            strtof(scan, (char**)&scan);
            while (*scan == ' ') scan++;
            if (*scan == '%') scan++;
            if (scan[0] == 'p' && scan[1] == 'x') scan += 2;
            strtof(scan, (char**)&scan);
            while (*scan == ' ') scan++;
            if (*scan == '%') scan++;
            if (scan[0] == 'p' && scan[1] == 'x') scan += 2;
            count++;
            while (*scan == ' ' || *scan == ',') scan++;
        }
        if (count < 3) return nullptr;

        float* vx = (float*)scratch_alloc(scratch, count * sizeof(float));
        float* vy = (float*)scratch_alloc(scratch, count * sizeof(float));
        for (int i = 0; i < count; i++) {
            vx[i] = render_clip_parse_len(s, elem_w) + abs_x;
            vy[i] = render_clip_parse_len(s, elem_h) + abs_y;
        }
        ClipShape* cs = (ClipShape*)scratch_calloc(scratch, sizeof(ClipShape));
        cs->type = CLIP_SHAPE_POLYGON;
        cs->polygon = {vx, vy, count};
        return cs;
    }

    return nullptr;
}
