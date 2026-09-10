#include "view.hpp"

#include <string.h>

// Static input metadata is the single classification source for HTML input
// keywords. Unknown and missing keywords intentionally use the HTML text
// fallback while preserving the caller's original attribute string.
static const FormInputDescriptor kInputDescriptors[] = {
    {"text", FORM_INPUT_KIND_TEXT, FORM_CONTROL_TEXT,
     FORM_INPUT_CAP_TEXT_CONTROL | FORM_INPUT_CAP_SINGLE_LINE, 0.0f, 0.0f},
    {"password", FORM_INPUT_KIND_PASSWORD, FORM_CONTROL_TEXT,
     FORM_INPUT_CAP_TEXT_CONTROL | FORM_INPUT_CAP_SINGLE_LINE |
         FORM_INPUT_CAP_PASSWORD, 0.0f, 0.0f},
    {"email", FORM_INPUT_KIND_EMAIL, FORM_CONTROL_TEXT,
     FORM_INPUT_CAP_TEXT_CONTROL | FORM_INPUT_CAP_SINGLE_LINE, 0.0f, 0.0f},
    {"url", FORM_INPUT_KIND_URL, FORM_CONTROL_TEXT,
     FORM_INPUT_CAP_TEXT_CONTROL | FORM_INPUT_CAP_SINGLE_LINE, 0.0f, 0.0f},
    {"search", FORM_INPUT_KIND_SEARCH, FORM_CONTROL_TEXT,
     FORM_INPUT_CAP_TEXT_CONTROL | FORM_INPUT_CAP_SINGLE_LINE, 0.0f, 0.0f},
    {"tel", FORM_INPUT_KIND_TEL, FORM_CONTROL_TEXT,
     FORM_INPUT_CAP_TEXT_CONTROL | FORM_INPUT_CAP_SINGLE_LINE, 0.0f, 0.0f},
    {"number", FORM_INPUT_KIND_NUMBER, FORM_CONTROL_TEXT,
     FORM_INPUT_CAP_TEXT_CONTROL | FORM_INPUT_CAP_SINGLE_LINE, 0.0f, 0.0f},
    {"checkbox", FORM_INPUT_KIND_CHECKBOX, FORM_CONTROL_CHECKBOX,
     FORM_INPUT_CAP_CHECKABLE, FormDefaults::CHECK_SIZE, FormDefaults::CHECK_SIZE},
    {"radio", FORM_INPUT_KIND_RADIO, FORM_CONTROL_RADIO,
     FORM_INPUT_CAP_CHECKABLE, FormDefaults::CHECK_SIZE, FormDefaults::CHECK_SIZE},
    {"button", FORM_INPUT_KIND_BUTTON, FORM_CONTROL_BUTTON,
     FORM_INPUT_CAP_BUTTON, 0.0f, 0.0f},
    {"submit", FORM_INPUT_KIND_SUBMIT, FORM_CONTROL_BUTTON,
     FORM_INPUT_CAP_BUTTON, 0.0f, 0.0f},
    {"reset", FORM_INPUT_KIND_RESET, FORM_CONTROL_BUTTON,
     FORM_INPUT_CAP_BUTTON, 0.0f, 0.0f},
    {"image", FORM_INPUT_KIND_IMAGE, FORM_CONTROL_IMAGE,
     FORM_INPUT_CAP_REPLACED_IMAGE, FormDefaults::IMAGE_INPUT_WIDTH, FormDefaults::IMAGE_INPUT_HEIGHT},
    {"hidden", FORM_INPUT_KIND_HIDDEN, FORM_CONTROL_HIDDEN,
     FORM_INPUT_CAP_HIDDEN, 0.0f, 0.0f},
    {"range", FORM_INPUT_KIND_RANGE, FORM_CONTROL_RANGE,
     FORM_INPUT_CAP_RANGE, FormDefaults::RANGE_WIDTH, FormDefaults::RANGE_HEIGHT},
    {"file", FORM_INPUT_KIND_FILE, FORM_CONTROL_TEXT,
     0, 0.0f, 0.0f},
    {"date", FORM_INPUT_KIND_DATE, FORM_CONTROL_TEXT,
     FORM_INPUT_CAP_FIXED_INTRINSIC_SIZE, 121.33f, 17.33f},
    {"time", FORM_INPUT_KIND_TIME, FORM_CONTROL_TEXT,
     FORM_INPUT_CAP_FIXED_INTRINSIC_SIZE, 100.0f, 20.0f},
    {"datetime-local", FORM_INPUT_KIND_DATETIME_LOCAL, FORM_CONTROL_TEXT,
     0, 0.0f, 0.0f},
    {"month", FORM_INPUT_KIND_MONTH, FORM_CONTROL_TEXT,
     FORM_INPUT_CAP_FIXED_INTRINSIC_SIZE, 151.33f, 17.33f},
    {"week", FORM_INPUT_KIND_WEEK, FORM_CONTROL_TEXT,
     FORM_INPUT_CAP_FIXED_INTRINSIC_SIZE, 143.33f, 17.33f},
    {"color", FORM_INPUT_KIND_COLOR, FORM_CONTROL_TEXT,
     FORM_INPUT_CAP_FIXED_INTRINSIC_SIZE, 44.0f, 23.0f},
};

const FormInputDescriptor* form_input_descriptor(const char* type) {
    if (!type || !*type) return &kInputDescriptors[0];
    size_t count = sizeof(kInputDescriptors) / sizeof(kInputDescriptors[0]);
    for (size_t i = 0; i < count; i++) {
        if (strcasecmp(type, kInputDescriptors[i].keyword) == 0) {
            return &kInputDescriptors[i];
        }
    }
    return &kInputDescriptors[0];
}

FormInputKind form_input_kind(const char* type) {
    return form_input_descriptor(type)->kind;
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
