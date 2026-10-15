#pragma once
#include "../../runtime/transpiler.hpp"

Item radiant_obj_new(void);
Item radiant_string_item(const char* value);
void radiant_rooted_obj_set(Rooted<Item>& object, const char* key, Item value);
extern "C" Item fn_radiant_image_job(Item owner, Item request);
