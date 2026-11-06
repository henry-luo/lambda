#pragma once

#include "../../core/data.hpp"

bool lambda_editing_dom_host_register();

extern "C" bool radiant_dom_set_design_mode(void* document, const char* value);
extern "C" bool radiant_dom_exec_command(void* document, const char* command, const char* value);
extern "C" Item radiant_dom_query_command(void* document, const char* query_kind, const char* command);
