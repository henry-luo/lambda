#include "render.hpp"

#define RENDER_PATH_KAPPA 0.5522847498f

RdtPath* render_path_create_rounded_rect(Rect rect, const Corner* radius) {
    RdtPath* path = rdt_path_new();
    if (!radius) {
        rdt_path_add_rect(path, rect.x, rect.y, rect.width, rect.height, 0, 0);
        return path;
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

    rdt_path_close(path);
    return path;
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

