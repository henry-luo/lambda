#pragma once

#include "radiant/input_value.hpp"
#include "../../lambda-data.hpp"

struct DomDocument;
struct DomElement;

#ifdef __cplusplus
extern "C" {
#endif

Item radiant_input_files(DomElement* element);
void radiant_input_set_files(DomElement* element, Item files);
#ifdef __cplusplus
}
#endif
