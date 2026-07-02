#pragma once

#include "view.hpp"

int ui_context_init(UiContext* uicon, bool headless);
void ui_context_cleanup(UiContext* uicon);
void ui_context_create_surface(UiContext* uicon, int pixel_width, int pixel_height);

void layout_html_doc(UiContext* uicon, DomDocument* doc, bool is_reflow);
void view_pool_destroy(ViewTree* tree);
void view_pool_release_detached_subtree(DomNode* root);
void image_cache_cleanup(UiContext* uicon);

void calculate_content_bounds(View* view, int* max_x, int* max_y);
