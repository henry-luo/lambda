#pragma once

#include "view.hpp"

struct RenderContext;

bool render_media_rasterize_svg_picture(ImageSurface* surface, int target_width,
                                        int target_height);
void render_image_content(struct RenderContext* rdcon, ViewBlock* view);
void render_image_view(struct RenderContext* rdcon, ViewBlock* view);
void render_video_content(struct RenderContext* rdcon, ViewBlock* view);
bool render_media_is_webview_layer(ViewBlock* view);
void render_webview_layer_content(struct RenderContext* rdcon, ViewBlock* view);
