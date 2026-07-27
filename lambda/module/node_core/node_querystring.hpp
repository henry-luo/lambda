#pragma once

#include "../../jube/jube.h"

int node_querystring_init(const JubeHostAPI* host);
void node_querystring_shutdown(void);
void node_querystring_runtime_attach(void* session);
void node_querystring_runtime_reset(void* session);
void node_querystring_runtime_detach(void* session);
Item node_querystring_namespace(void);
