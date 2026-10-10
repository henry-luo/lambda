// isolated DOM/box/retained-list targets have no native editing or layout host.
#include "../radiant/event.hpp"
#include "../radiant/layout.hpp"
#include "../lambda/module/radiant/radiant_input_value.hpp"
#include "../lib/log.h"
#include <stdlib.h>

[[noreturn]] static void unexpected_host_query(const char* name) {
    log_error("isolated DOM test: unexpected native host query %s", name);
    abort();
}

float layout_effective_zoom(View*) {
    unexpected_host_query("layout_effective_zoom");
}

bool layout_block_inline_axis_is_vertical(ViewBlock*) {
    unexpected_host_query("layout_block_inline_axis_is_vertical");
}

void view_get_layout_position(View*, TextRect*, float*, float*, View*) {
    unexpected_host_query("view_get_layout_position");
}

const char* form_control_get_value(DocState*, View*, uint32_t*) {
    unexpected_host_query("form_control_get_value");
}

extern "C" const char* radiant_input_peek_live_value(DomElement*) {
    unexpected_host_query("radiant_input_peek_live_value");
}
