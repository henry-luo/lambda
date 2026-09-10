#include "render.hpp"

#define RENDER_PATH_KAPPA 0.5522847498f

RdtPath* render_path_create_rounded_rect(Rect rect, const Corner* radius) {
    RdtPath* path = rdt_path_new();
    render_path_append_rounded_rect(path, rect, radius, true);
    return path;
}

void render_path_append_rounded_rect(RdtPath* path, Rect rect,
                                     const Corner* radius, bool clockwise) {
    if (!path) return;
    if (!radius) {
        if (clockwise) {
            rdt_path_add_rect(path, rect.x, rect.y, rect.width, rect.height, 0, 0);
        } else {
            rdt_path_move_to(path, rect.x, rect.y);
            rdt_path_line_to(path, rect.x, rect.y + rect.height);
            rdt_path_line_to(path, rect.x + rect.width, rect.y + rect.height);
            rdt_path_line_to(path, rect.x + rect.width, rect.y);
            rdt_path_close(path);
        }
        return;
    }

    float x = rect.x;
    float y = rect.y;
    float w = rect.width;
    float h = rect.height;

    float rx_tl = radius->top_left;
    float rx_tr = radius->top_right;
    float rx_br = radius->bottom_right;
    float rx_bl = radius->bottom_left;
    float ry_tl = radius->top_left_y;
    float ry_tr = radius->top_right_y;
    float ry_br = radius->bottom_right_y;
    float ry_bl = radius->bottom_left_y;

    if (clockwise) {
        rdt_path_move_to(path, x + rx_tl, y);
        rdt_path_line_to(path, x + w - rx_tr, y);
        if (rx_tr > 0.0f || ry_tr > 0.0f) {
            rdt_path_cubic_to(path,
                x + w - rx_tr + rx_tr * RENDER_PATH_KAPPA, y,
                x + w, y + ry_tr - ry_tr * RENDER_PATH_KAPPA,
                x + w, y + ry_tr);
        }

        rdt_path_line_to(path, x + w, y + h - ry_br);
        if (rx_br > 0.0f || ry_br > 0.0f) {
            rdt_path_cubic_to(path,
                x + w, y + h - ry_br + ry_br * RENDER_PATH_KAPPA,
                x + w - rx_br + rx_br * RENDER_PATH_KAPPA, y + h,
                x + w - rx_br, y + h);
        }

        rdt_path_line_to(path, x + rx_bl, y + h);
        if (rx_bl > 0.0f || ry_bl > 0.0f) {
            rdt_path_cubic_to(path,
                x + rx_bl - rx_bl * RENDER_PATH_KAPPA, y + h,
                x, y + h - ry_bl + ry_bl * RENDER_PATH_KAPPA,
                x, y + h - ry_bl);
        }

        rdt_path_line_to(path, x, y + ry_tl);
        if (rx_tl > 0.0f || ry_tl > 0.0f) {
            rdt_path_cubic_to(path,
                x, y + ry_tl - ry_tl * RENDER_PATH_KAPPA,
                x + rx_tl - rx_tl * RENDER_PATH_KAPPA, y,
                x + rx_tl, y);
        }
    } else {
        rdt_path_move_to(path, x + rx_tl, y);
        if (rx_tl > 0.0f || ry_tl > 0.0f) {
            rdt_path_cubic_to(path,
                x + rx_tl - rx_tl * RENDER_PATH_KAPPA, y,
                x, y + ry_tl - ry_tl * RENDER_PATH_KAPPA,
                x, y + ry_tl);
        }
        rdt_path_line_to(path, x, y + h - ry_bl);
        if (rx_bl > 0.0f || ry_bl > 0.0f) {
            rdt_path_cubic_to(path,
                x, y + h - ry_bl + ry_bl * RENDER_PATH_KAPPA,
                x + rx_bl - rx_bl * RENDER_PATH_KAPPA, y + h,
                x + rx_bl, y + h);
        }
        rdt_path_line_to(path, x + w - rx_br, y + h);
        if (rx_br > 0.0f || ry_br > 0.0f) {
            rdt_path_cubic_to(path,
                x + w - rx_br + rx_br * RENDER_PATH_KAPPA, y + h,
                x + w, y + h - ry_br + ry_br * RENDER_PATH_KAPPA,
                x + w, y + h - ry_br);
        }
        rdt_path_line_to(path, x + w, y + ry_tr);
        if (rx_tr > 0.0f || ry_tr > 0.0f) {
            rdt_path_cubic_to(path,
                x + w, y + ry_tr - ry_tr * RENDER_PATH_KAPPA,
                x + w - rx_tr + rx_tr * RENDER_PATH_KAPPA, y,
                x + w - rx_tr, y);
        }
    }

    rdt_path_close(path);
}

Corner render_path_uniform_corner(float top_left, float top_right,
                                  float bottom_right, float bottom_left) {
    Corner radius = {};
    radius.horizontal[0] = radius.vertical[0] = top_left;
    radius.horizontal[1] = radius.vertical[1] = top_right;
    radius.horizontal[2] = radius.vertical[2] = bottom_right;
    radius.horizontal[3] = radius.vertical[3] = bottom_left;
    return radius;
}

static bool render_path_svg_visit(void* context, RdtPathCommand command,
                                  const float* args, int arg_count) {
    StrBuf* out = (StrBuf*)context;
    if (!out) return false;
    if (command == RDT_PATH_CLOSE) {
        strbuf_append_str(out, " Z");
        return true;
    }
    if (!args) return false;
    switch (command) {
        case RDT_PATH_CLOSE:
            return true;
        case RDT_PATH_MOVE:
            if (arg_count < 2) return false;
            strbuf_append_format(out, "M%.2f,%.2f", args[0], args[1]);
            return true;
        case RDT_PATH_LINE:
            if (arg_count < 2) return false;
            strbuf_append_format(out, " L%.2f,%.2f", args[0], args[1]);
            return true;
        case RDT_PATH_CUBIC:
            if (arg_count < 6) return false;
            strbuf_append_format(out, " C%.2f,%.2f %.2f,%.2f %.2f,%.2f",
                args[0], args[1], args[2], args[3], args[4], args[5]);
            return true;
        case RDT_PATH_RECT:
            if (arg_count < 4) return false;
            strbuf_append_format(out, "M%.2f,%.2f L%.2f,%.2f L%.2f,%.2f L%.2f,%.2f Z",
                args[0], args[1], args[0] + args[2], args[1],
                args[0] + args[2], args[1] + args[3], args[0], args[1] + args[3]);
            return true;
        case RDT_PATH_QUAD:
        case RDT_PATH_CIRCLE:
            return false;
    }
    return false;
}

void render_path_append_svg_rounded_rect(StrBuf* out, Rect rect,
                                         const Corner* radius) {
    if (!out) return;
    RdtPath* path = render_path_create_rounded_rect(rect, radius);
    if (!path) return;
    rdt_path_visit(path, render_path_svg_visit, out);
    rdt_path_free(path);
}

RdtPath* render_path_create_clip_path(RenderContext* rdcon) {
    if (!rdcon) {
        return nullptr;
    }

    float clip_x = rdcon->block.clip.left;
    float clip_y = rdcon->block.clip.top;
    float clip_w = rdcon->block.clip.right - rdcon->block.clip.left;
    float clip_h = rdcon->block.clip.bottom - rdcon->block.clip.top;
    Rect clip_rect = {clip_x, clip_y, clip_w, clip_h};

    if (rdcon->block.has_clip_radius) {
        Corner clip_radius = rdcon->block.clip_radius;
        constrain_corner_radii(&clip_radius, clip_w, clip_h);
        return render_path_create_rounded_rect(clip_rect, &clip_radius);
    }

    return render_path_create_rounded_rect(clip_rect, nullptr);
}
