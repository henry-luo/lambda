#pragma once
#include "../lambda-data.hpp"
#include "../../lib/image_process.h"

bool lambda_image_options(Item options, ImageProcessOptions* out);
bool lambda_image_source(Item pixels, ImageProcessSource* out);
Item lambda_image_result(const ImageProcessResult& result);
