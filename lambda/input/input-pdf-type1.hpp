#pragma once

#include "input.hpp"

// decode embedded Type 1 outlines to 1000-unit, y-up SVG paths keyed by PDF byte code.
Item pdf_type1_glyph_paths(Input* input, String* program, int clear_length, Item encoding);
