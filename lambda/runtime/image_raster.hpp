#pragma once
#include "../lambda-data.hpp"
#include "../../lib/raster.h"

const RasterBuffer* lambda_raster_buffer(Item value);
Item lambda_raster_wrap(RasterBuffer* buffer, Item color);
Item lambda_image_error(const char* message);
bool lambda_image_tag(Item item, const char* tag);
bool lambda_image_number(Item owner, const char* key, double fallback, double* out);
bool lambda_image_position(Item owner, int64_t* left, int64_t* top);
bool lambda_image_setting(Item owner, const char* key, const char* expected);
Item lambda_image_decode_psd(const uint8_t* bytes, size_t length);
Item lambda_image_encode_psd(Item source, Item composite, int64_t frame);
