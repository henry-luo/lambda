#pragma once

#include "view.hpp"
#include <stdint.h>

CssEnum get_white_space_value(DomNode* node);
bool text_codepoint_has_zero_advance(uint32_t codepoint);
