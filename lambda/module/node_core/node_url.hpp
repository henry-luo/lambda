#pragma once

#include "../../jube/jube.h"

int node_url_init(const JubeHostAPI* host);
void node_url_shutdown(void);
void node_url_runtime_attach(void* session);
void node_url_runtime_reset(void* session);
void node_url_runtime_detach(void* session);
Item node_url_namespace(void);
Item node_url_search_params_prototype(void);

// shared query parsing/serialization for the global URL's live parameter object.
Item node_url_search_params_string(Item params);
Item node_url_search_params_replace_query(Item params, const char* query, int length);

extern "C" Item js_url_search_params_new(Item init);
