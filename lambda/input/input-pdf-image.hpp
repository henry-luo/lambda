#pragma once
#include "input.hpp"

// shared portable image transport; pixel bytes are top-down RGBA8.
String* pdf_rgba_image_data_uri(Input* owner, const uint8_t* pixels, int width, int height);
