#pragma once
#include "input.hpp"
#include "../../lib/byte_builder.h"

// shared portable image transport; pixel bytes are top-down RGBA8.
String* pdf_rgba_image_data_uri(Input* owner, const uint8_t* pixels, int width, int height);

// encoded image filters return unpacked samples; the PDF colour space still owns interpretation.
bool pdf_decode_fax_samples(const uint8_t* data, size_t length, int width, int height,
    int k, bool end_of_line, bool byte_aligned, bool black_is_one, ByteBuilder* samples);
bool pdf_decode_jbig2_samples(const uint8_t* data, size_t length, int width, int height,
    ByteBuilder* samples);
bool pdf_decode_jpx_samples(const uint8_t* data, size_t length, int width, int height,
    bool indexed, int* components, int* bits, ByteBuilder* samples);
