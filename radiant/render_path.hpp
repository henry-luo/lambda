#pragma once

#include "view.hpp"
#include "rdt_vector.hpp"

struct RenderContext;
typedef struct RenderContext RenderContext;

RdtPath* render_path_create_rounded_rect(Rect rect, const Corner* radius);
RdtPath* render_path_create_clip_path(RenderContext* rdcon);

