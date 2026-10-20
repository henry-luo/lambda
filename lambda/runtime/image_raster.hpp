#pragma once
#include "../lambda-data.hpp"
#include "../../lib/raster.h"

const RasterBuffer* lambda_raster_buffer(Item value);
bool lambda_raster_compare_samples(Item a,Item b,Item key,bool* equal);
Item lambda_raster_wrap(RasterBuffer* buffer, Item color);
Item lambda_image_error(const char* message);
bool lambda_image_tag(Item item, const char* tag);
bool lambda_image_number(Item owner, const char* key, double fallback, double* out);
bool lambda_image_position(Item owner, int64_t* left, int64_t* top);
bool lambda_image_rect(Item value, int64_t* rect);
bool lambda_image_setting(Item owner, const char* key, const char* expected);
bool lambda_image_color(Item value,uint8_t channels,uint8_t* samples);
bool lambda_image_artwork(Item source, Item assets, RasterOwner& output, size_t available, const char** failure);
bool lambda_image_styles(Item owner,const RasterBuffer** pixels,RasterOwner& output,
    int* offset,size_t available,const char** failure);
Item lambda_image_decode_psd(const uint8_t* bytes, size_t length);
Item lambda_image_encode_psd(Item source, Item composite, int64_t frame);
