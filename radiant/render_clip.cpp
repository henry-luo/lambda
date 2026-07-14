#include "render.hpp"

#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/css_style.hpp"
#include "../lib/tagged.hpp"
#include "../lib/log.h"

#include <stddef.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static RdtPath* render_clip_create_rounded_rect_path(float x, float y, float w, float h,
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

static RdtPath* render_clip_create_shape_path(ClipShape* shape) {
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

static void render_clip_free_shape(ScratchArena* scratch, ClipShape* shape) {
    if (!scratch || !shape) return;
    if (shape->type == CLIP_SHAPE_POLYGON) {
        scratch_free(scratch, shape->polygon.vy);
        scratch_free(scratch, shape->polygon.vx);
    }
    scratch_free(scratch, shape);
}

static ClipShape* render_clip_clone_shape(ScratchArena* scratch, const ClipShape* shape) {
    if (!scratch || !shape) return nullptr;
    ClipShape* copy = (ClipShape*)scratch_calloc(scratch, sizeof(ClipShape));
    if (!copy) return nullptr;
    *copy = *shape;
    return copy;
}

static float render_clip_parse_len(const char*& s, float ref) {
    while (*s == ' ' || *s == ',') s++;
    float val = strtof(s, (char**)&s);
    while (*s == ' ') s++;
    if (*s == '%') { s++; return val / 100.0f * ref; }
    if (s[0] == 'p' && s[1] == 'x') s += 2;
    return val;
}

static void render_clip_parse_center(const char*& s, float elem_w, float elem_h,
                                     float abs_x, float abs_y, float* cx, float* cy) {
    *cx = abs_x + elem_w * 0.5f;
    *cy = abs_y + elem_h * 0.5f;
    while (*s == ' ') s++;
    if (strncmp(s, "at", 2) != 0) return;

    s += 2;
    *cx = abs_x + render_clip_parse_len(s, elem_w);
    *cy = abs_y + render_clip_parse_len(s, elem_h);
}

static bool render_clip_parse_polygon_len(const char*& s, float ref, float* out_value) {
    while (*s == ' ' || *s == ',') s++;
    const char* start = s;
    char* end = nullptr;
    float val = strtof(s, &end);
    if (end == start) {
        return false;
    }
    s = end;
    while (*s == ' ') s++;
    if (*s == '%') {
        s++;
        *out_value = val / 100.0f * ref;
        return true;
    }
    if (s[0] == 'p' && s[1] == 'x') {
        s += 2;
        *out_value = val;
        return true;
    }
    if (isalpha((unsigned char)*s) || *s == '(') {
        return false;
    }
    *out_value = val;
    return true;
}

static void render_clip_skip_wsp_comma(const char** s) {
    while (**s && (isspace((unsigned char)**s) || **s == ',')) (*s)++;
}

static bool render_clip_peek_number(const char* s) {
    render_clip_skip_wsp_comma(&s);
    return *s == '-' || *s == '+' || *s == '.' || isdigit((unsigned char)*s);
}

static float render_clip_parse_number(const char** s) {
    render_clip_skip_wsp_comma(s);
    char* end = nullptr;
    float value = strtof(*s, &end);
    if (end == *s) {
        if (**s) (*s)++;
        return 0.0f;
    }
    *s = end;
    return value;
}

static RdtPath* render_clip_parse_path_function(const char* value,
                                                float abs_x, float abs_y) {
    if (!value || strncmp(value, "path(", 5) != 0) return nullptr;
    const char* s = value + 5;
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s != '"' && *s != '\'') return nullptr;
    char quote = *s++;

    RdtPath* path = rdt_path_new();
    float cur_x = 0.0f, cur_y = 0.0f;
    float start_x = 0.0f, start_y = 0.0f;
    char last_cmd = 0;
    bool any_draw = false;

    while (*s && *s != quote) {
        render_clip_skip_wsp_comma(&s);
        if (!*s || *s == quote) break;

        char cmd = *s;
        if (isalpha((unsigned char)cmd)) {
            s++;
            last_cmd = cmd;
        } else {
            if (!last_cmd || !render_clip_peek_number(s)) {
                rdt_path_free(path);
                return nullptr;
            }
            cmd = last_cmd;
            if (cmd == 'M') cmd = 'L';
            if (cmd == 'm') cmd = 'l';
        }

        bool relative = islower((unsigned char)cmd);
        cmd = (char)toupper((unsigned char)cmd);

        switch (cmd) {
            case 'M': {
                float x = render_clip_parse_number(&s);
                float y = render_clip_parse_number(&s);
                if (relative) { x += cur_x; y += cur_y; }
                cur_x = start_x = x;
                cur_y = start_y = y;
                rdt_path_move_to(path, abs_x + x, abs_y + y);
                while (render_clip_peek_number(s)) {
                    x = render_clip_parse_number(&s);
                    y = render_clip_parse_number(&s);
                    if (relative) { x += cur_x; y += cur_y; }
                    cur_x = x; cur_y = y;
                    rdt_path_line_to(path, abs_x + x, abs_y + y);
                    any_draw = true;
                }
                break;
            }
            case 'L': {
                while (render_clip_peek_number(s)) {
                    float x = render_clip_parse_number(&s);
                    float y = render_clip_parse_number(&s);
                    if (relative) { x += cur_x; y += cur_y; }
                    cur_x = x; cur_y = y;
                    rdt_path_line_to(path, abs_x + x, abs_y + y);
                    any_draw = true;
                }
                break;
            }
            case 'H': {
                while (render_clip_peek_number(s)) {
                    float x = render_clip_parse_number(&s);
                    if (relative) x += cur_x;
                    cur_x = x;
                    rdt_path_line_to(path, abs_x + cur_x, abs_y + cur_y);
                    any_draw = true;
                }
                break;
            }
            case 'V': {
                while (render_clip_peek_number(s)) {
                    float y = render_clip_parse_number(&s);
                    if (relative) y += cur_y;
                    cur_y = y;
                    rdt_path_line_to(path, abs_x + cur_x, abs_y + cur_y);
                    any_draw = true;
                }
                break;
            }
            case 'C': {
                while (render_clip_peek_number(s)) {
                    float x1 = render_clip_parse_number(&s);
                    float y1 = render_clip_parse_number(&s);
                    float x2 = render_clip_parse_number(&s);
                    float y2 = render_clip_parse_number(&s);
                    float x = render_clip_parse_number(&s);
                    float y = render_clip_parse_number(&s);
                    if (relative) {
                        x1 += cur_x; y1 += cur_y;
                        x2 += cur_x; y2 += cur_y;
                        x += cur_x; y += cur_y;
                    }
                    rdt_path_cubic_to(path, abs_x + x1, abs_y + y1,
                                      abs_x + x2, abs_y + y2,
                                      abs_x + x, abs_y + y);
                    cur_x = x; cur_y = y;
                    any_draw = true;
                }
                break;
            }
            case 'Z':
                rdt_path_close(path);
                cur_x = start_x;
                cur_y = start_y;
                any_draw = true;
                break;
            default:
                rdt_path_free(path);
                return nullptr;
        }
    }

    if (!any_draw) {
        rdt_path_free(path);
        return nullptr;
    }
    return path;
}

static ClipShape* render_clip_parse_css_shape(ScratchArena* scratch, const char* value,
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
        float cx, cy;
        render_clip_parse_center(s, elem_w, elem_h, abs_x, abs_y, &cx, &cy);
        ClipShape* cs = (ClipShape*)scratch_calloc(scratch, sizeof(ClipShape));
        cs->type = CLIP_SHAPE_CIRCLE;
        cs->circle = {cx, cy, r};
        return cs;
    }

    if (strncmp(value, "ellipse(", 8) == 0) {
        const char* s = value + 8;
        float rx = render_clip_parse_len(s, elem_w);
        float ry = render_clip_parse_len(s, elem_h);
        float cx, cy;
        render_clip_parse_center(s, elem_w, elem_h, abs_x, abs_y, &cx, &cy);
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
            float dummy_x = 0.0f, dummy_y = 0.0f;
            // Unsupported units/functions such as calc() and rem leave strtof
            // unable to advance; abort the optional clip-path instead of
            // spinning forever while rendering the page.
            if (!render_clip_parse_polygon_len(scan, elem_w, &dummy_x) ||
                !render_clip_parse_polygon_len(scan, elem_h, &dummy_y)) {
                return nullptr;
            }
            count++;
            while (*scan == ' ' || *scan == ',') scan++;
        }
        if (count < 3) return nullptr;

        float* vx = (float*)scratch_alloc(scratch, count * sizeof(float));
        float* vy = (float*)scratch_alloc(scratch, count * sizeof(float));
        for (int i = 0; i < count; i++) {
            if (!render_clip_parse_polygon_len(s, elem_w, &vx[i]) ||
                !render_clip_parse_polygon_len(s, elem_h, &vy[i])) {
                scratch_free(scratch, vy);
                scratch_free(scratch, vx);
                return nullptr;
            }
            vx[i] += abs_x;
            vy[i] += abs_y;
        }
        ClipShape* cs = (ClipShape*)scratch_calloc(scratch, sizeof(ClipShape));
        cs->type = CLIP_SHAPE_POLYGON;
        cs->polygon = {vx, vy, count};
        return cs;
    }

    return nullptr;
}

static bool render_clip_push_shape_scope(RenderContext* rdcon, RenderClipScope* scope,
                                         ClipShape* shape) {
    if (!rdcon || !scope || !shape) {
        return false;
    }
    RdtPath* clip_path = render_clip_create_shape_path(shape);
    if (!clip_path) {
        return false;
    }
    rc_push_clip(rdcon, clip_path, nullptr);
    rdt_path_free(clip_path);

    scope->shape = shape;
    scope->active = true;
    if (rdcon->clip_shape_depth < RDT_MAX_CLIP_SHAPES) {
        rdcon->clip_shapes[rdcon->clip_shape_depth++] = shape;
        scope->pushed_shape = true;
    } else {
        log_warn("[RAD_CAP_RENDER_CLIP_SHAPES] dropping retained clip shape beyond depth %d",
                 RDT_MAX_CLIP_SHAPES);
    }
    return true;
}

RenderClipScope render_clip_push_css_scope(RenderContext* rdcon, ViewBlock* block,
                                           float parent_x, float parent_y, float scale) {
    RenderClipScope scope = {};
    if (!rdcon || !block) {
        return scope;
    }
    DomElement* element = lam::dom_require_element(lam::view_dom_node(block));
    CssDeclaration* clip_decl = dom_element_get_specified_value(element, CSS_PROPERTY_CLIP_PATH);
    if (!clip_decl || !clip_decl->value_text || clip_decl->value_text_len <= 0) {
        return scope;
    }
    const char* clip_str = clip_decl->value_text;
    if (!clip_str || strncmp(clip_str, "none", 4) == 0) {
        return scope;
    }

    float elem_w = block->width * scale;
    float elem_h = block->height * scale;
    float abs_x = parent_x + block->x * scale;
    float abs_y = parent_y + block->y * scale;
    RdtPath* css_path = render_clip_parse_path_function(clip_str, abs_x, abs_y);
    if (css_path) {
        rc_push_clip(rdcon, css_path, nullptr);
        rdt_path_free(css_path);
        scope.active = true;
        log_debug("[CLIP] CSS clip-path path(): %s on element %s", clip_str, block->node_name());
        return scope;
    }

    ClipShape* css_shape = render_clip_parse_css_shape(&rdcon->scratch, clip_str, elem_w, elem_h, abs_x, abs_y);
    if (!css_shape) {
        return scope;
    }

    scope.owns_shape = true;
    if (!render_clip_push_shape_scope(rdcon, &scope, css_shape)) {
        render_clip_free_shape(&rdcon->scratch, css_shape);
        scope = {};
        return scope;
    }
    log_debug("[CLIP] CSS clip-path: %s on element %s", clip_str, block->node_name());
    return scope;
}

RenderClipScope render_clip_push_rect_scope(RenderContext* rdcon, const Bound* clip) {
    RenderClipScope scope = {};
    if (!rdcon || !clip) {
        return scope;
    }
    float w = clip->right - clip->left;
    float h = clip->bottom - clip->top;
    if (w <= 0 || h <= 0) {
        return scope;
    }

    ClipShape inline_shape = {};
    inline_shape.type = CLIP_SHAPE_INSET;
    inline_shape.inset = {clip->left, clip->top, w, h, 0, 0};
    ClipShape* shape = render_clip_clone_shape(&rdcon->scratch, &inline_shape);
    if (!shape) {
        return scope;
    }
    scope.owns_shape = true;
    if (!render_clip_push_shape_scope(rdcon, &scope, shape)) {
        render_clip_free_shape(&rdcon->scratch, shape);
        scope = {};
        return scope;
    }
    log_debug("[CLIP] pushed rect clip: (%.0f,%.0f) %.0fx%.0f",
        clip->left, clip->top, w, h);
    return scope;
}

RenderClipScope render_clip_push_overflow_scope(RenderContext* rdcon) {
    RenderClipScope scope = {};
    if (!rdcon || !rdcon->block.has_clip_radius) {
        return scope;
    }
    Bound* clip = &rdcon->block.clip;
    Corner* cr = &rdcon->block.clip_radius;
    float cw = clip->right - clip->left;
    float ch = clip->bottom - clip->top;
    if (cw <= 0 || ch <= 0) {
        return scope;
    }

    ClipShape inline_shape = {};
    inline_shape.type = CLIP_SHAPE_ROUNDED_RECT;
    inline_shape.rounded_rect = {clip->left, clip->top, cw, ch,
        cr->top_left, cr->top_right, cr->bottom_right, cr->bottom_left};
    ClipShape* shape = render_clip_clone_shape(&rdcon->scratch, &inline_shape);
    if (!shape) {
        return scope;
    }
    scope.owns_shape = true;
    if (!render_clip_push_shape_scope(rdcon, &scope, shape)) {
        render_clip_free_shape(&rdcon->scratch, shape);
        scope = {};
        return scope;
    }

    // Clear the flag so child elements do not redundantly push the same clip.
    rdcon->block.has_clip_radius = false;
    log_debug("[CLIP] pushed overflow vector clip: (%.0f,%.0f) %.0fx%.0f r=[%.0f,%.0f,%.0f,%.0f]",
        clip->left, clip->top, cw, ch,
        cr->top_left, cr->top_right, cr->bottom_right, cr->bottom_left);
    return scope;
}

void render_clip_pop_scope(RenderContext* rdcon, RenderClipScope* scope) {
    if (!rdcon || !scope || !scope->active) {
        return;
    }
    rc_pop_clip(rdcon);
    if (scope->pushed_shape && rdcon->clip_shape_depth > 0) {
        rdcon->clip_shape_depth--;
    }
    if (scope->owns_shape) {
        render_clip_free_shape(&rdcon->scratch, scope->shape);
    }
    *scope = {};
}
