#include "view.hpp"
#include "event.hpp"
#include "../lambda/module/radiant/radiant_input_value.hpp"

#include "../lib/str.h"

#include <string.h>

// Static input metadata is the single classification source for HTML input
// keywords. Unknown and missing keywords intentionally use the HTML text
// fallback while preserving the caller's original attribute string.
static const FormInputDescriptor kInputDescriptors[] = {
    {HTML_INPUT_TYPES[FORM_INPUT_KIND_TEXT].keyword, FORM_INPUT_KIND_TEXT, FORM_CONTROL_TEXT,
     FORM_INPUT_CAP_TEXT_CONTROL | FORM_INPUT_CAP_SINGLE_LINE, 0.0f, 0.0f},
    {HTML_INPUT_TYPES[FORM_INPUT_KIND_PASSWORD].keyword, FORM_INPUT_KIND_PASSWORD, FORM_CONTROL_TEXT,
     FORM_INPUT_CAP_TEXT_CONTROL | FORM_INPUT_CAP_SINGLE_LINE |
         FORM_INPUT_CAP_PASSWORD, 0.0f, 0.0f},
    {HTML_INPUT_TYPES[FORM_INPUT_KIND_EMAIL].keyword, FORM_INPUT_KIND_EMAIL, FORM_CONTROL_TEXT,
     FORM_INPUT_CAP_TEXT_CONTROL | FORM_INPUT_CAP_SINGLE_LINE, 0.0f, 0.0f},
    {HTML_INPUT_TYPES[FORM_INPUT_KIND_URL].keyword, FORM_INPUT_KIND_URL, FORM_CONTROL_TEXT,
     FORM_INPUT_CAP_TEXT_CONTROL | FORM_INPUT_CAP_SINGLE_LINE, 0.0f, 0.0f},
    {HTML_INPUT_TYPES[FORM_INPUT_KIND_SEARCH].keyword, FORM_INPUT_KIND_SEARCH, FORM_CONTROL_TEXT,
     FORM_INPUT_CAP_TEXT_CONTROL | FORM_INPUT_CAP_SINGLE_LINE, 0.0f, 0.0f},
    {HTML_INPUT_TYPES[FORM_INPUT_KIND_TEL].keyword, FORM_INPUT_KIND_TEL, FORM_CONTROL_TEXT,
     FORM_INPUT_CAP_TEXT_CONTROL | FORM_INPUT_CAP_SINGLE_LINE, 0.0f, 0.0f},
    {HTML_INPUT_TYPES[FORM_INPUT_KIND_NUMBER].keyword, FORM_INPUT_KIND_NUMBER, FORM_CONTROL_TEXT,
     FORM_INPUT_CAP_TEXT_CONTROL | FORM_INPUT_CAP_SINGLE_LINE, 0.0f, 0.0f},
    {HTML_INPUT_TYPES[FORM_INPUT_KIND_CHECKBOX].keyword, FORM_INPUT_KIND_CHECKBOX, FORM_CONTROL_CHECKBOX,
     FORM_INPUT_CAP_CHECKABLE, FormDefaults::CHECK_SIZE, FormDefaults::CHECK_SIZE},
    {HTML_INPUT_TYPES[FORM_INPUT_KIND_RADIO].keyword, FORM_INPUT_KIND_RADIO, FORM_CONTROL_RADIO,
     FORM_INPUT_CAP_CHECKABLE, FormDefaults::CHECK_SIZE, FormDefaults::CHECK_SIZE},
    {HTML_INPUT_TYPES[FORM_INPUT_KIND_BUTTON].keyword, FORM_INPUT_KIND_BUTTON, FORM_CONTROL_BUTTON,
     FORM_INPUT_CAP_BUTTON, 0.0f, 0.0f},
    {HTML_INPUT_TYPES[FORM_INPUT_KIND_SUBMIT].keyword, FORM_INPUT_KIND_SUBMIT, FORM_CONTROL_BUTTON,
     FORM_INPUT_CAP_BUTTON, 0.0f, 0.0f},
    {HTML_INPUT_TYPES[FORM_INPUT_KIND_RESET].keyword, FORM_INPUT_KIND_RESET, FORM_CONTROL_BUTTON,
     FORM_INPUT_CAP_BUTTON, 0.0f, 0.0f},
    {HTML_INPUT_TYPES[FORM_INPUT_KIND_IMAGE].keyword, FORM_INPUT_KIND_IMAGE, FORM_CONTROL_IMAGE,
     FORM_INPUT_CAP_REPLACED_IMAGE, FormDefaults::IMAGE_INPUT_WIDTH, FormDefaults::IMAGE_INPUT_HEIGHT},
    {HTML_INPUT_TYPES[FORM_INPUT_KIND_HIDDEN].keyword, FORM_INPUT_KIND_HIDDEN, FORM_CONTROL_HIDDEN,
     FORM_INPUT_CAP_HIDDEN, 0.0f, 0.0f},
    {HTML_INPUT_TYPES[FORM_INPUT_KIND_RANGE].keyword, FORM_INPUT_KIND_RANGE, FORM_CONTROL_RANGE,
     FORM_INPUT_CAP_RANGE, FormDefaults::RANGE_WIDTH, FormDefaults::RANGE_HEIGHT},
    {HTML_INPUT_TYPES[FORM_INPUT_KIND_FILE].keyword, FORM_INPUT_KIND_FILE, FORM_CONTROL_TEXT,
     0, 0.0f, 0.0f},
    {HTML_INPUT_TYPES[FORM_INPUT_KIND_DATE].keyword, FORM_INPUT_KIND_DATE, FORM_CONTROL_TEXT,
     FORM_INPUT_CAP_FIXED_INTRINSIC_SIZE, 121.33f, 17.33f},
    {HTML_INPUT_TYPES[FORM_INPUT_KIND_TIME].keyword, FORM_INPUT_KIND_TIME, FORM_CONTROL_TEXT,
     FORM_INPUT_CAP_FIXED_INTRINSIC_SIZE, 100.0f, 20.0f},
    {HTML_INPUT_TYPES[FORM_INPUT_KIND_DATETIME_LOCAL].keyword, FORM_INPUT_KIND_DATETIME_LOCAL, FORM_CONTROL_TEXT,
     0, 0.0f, 0.0f},
    {HTML_INPUT_TYPES[FORM_INPUT_KIND_MONTH].keyword, FORM_INPUT_KIND_MONTH, FORM_CONTROL_TEXT,
     FORM_INPUT_CAP_FIXED_INTRINSIC_SIZE, 151.33f, 17.33f},
    {HTML_INPUT_TYPES[FORM_INPUT_KIND_WEEK].keyword, FORM_INPUT_KIND_WEEK, FORM_CONTROL_TEXT,
     FORM_INPUT_CAP_FIXED_INTRINSIC_SIZE, 143.33f, 17.33f},
    {HTML_INPUT_TYPES[FORM_INPUT_KIND_COLOR].keyword, FORM_INPUT_KIND_COLOR, FORM_CONTROL_TEXT,
     FORM_INPUT_CAP_FIXED_INTRINSIC_SIZE, 44.0f, 23.0f},
};

const FormInputDescriptor* form_input_descriptor(const char* type) {
    static_assert(sizeof(kInputDescriptors) / sizeof(*kInputDescriptors) == FORM_INPUT_KIND_COUNT);
    return &kInputDescriptors[form_input_kind(type)];
}

FormControlType form_input_control_type(const char* type) {
    return form_input_descriptor(type)->control_type;
}

bool form_input_has_capability(const char* type, uint32_t capability) {
    return (form_input_descriptor(type)->capabilities & capability) != 0;
}

bool form_input_kind_is(const char* type, FormInputKind kind) {
    return form_input_kind(type) == kind;
}

bool form_input_intrinsic_size(const char* type, float* width, float* height) {
    const FormInputDescriptor* descriptor = form_input_descriptor(type);
    if (width) *width = descriptor->fixed_intrinsic_width;
    if (height) *height = descriptor->fixed_intrinsic_height;
    return descriptor->fixed_intrinsic_width > 0.0f ||
        descriptor->fixed_intrinsic_height > 0.0f;
}

static const char* form_direction_live_value(DomElement* element, void*) {
    // text values stay StateStore-owned; recascade reads must not initialize another owner (D4.5.1v4).
    DocState* state = element && element->doc ? (DocState*)element->doc->state : nullptr;
    const char* value = form_control_get_value(state, static_cast<View*>(element), nullptr);
    return value ? value : radiant_input_peek_live_value(element);
}

extern "C" int dom_css_element_directionality(void* element) {
    return dom_element_directionality((DomElement*)element, form_direction_live_value);
}
