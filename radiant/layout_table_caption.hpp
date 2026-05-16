#pragma once

#include "view.hpp"

struct LayoutContext;

float relayout_table_caption(LayoutContext* lycon, ViewBlock* cap, float table_width);
float adjust_table_caption_width(ViewBlock* cap, float wrapper_content_width);
