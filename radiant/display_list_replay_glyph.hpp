#pragma once

#include "display_list.h"

void dl_replay_draw_glyph(ImageSurface* surface, const DlDrawGlyph* glyph);
void dl_replay_draw_glyph_at_offset(ImageSurface* surface, const DlDrawGlyph* glyph,
                                    float offset_x, float offset_y);
