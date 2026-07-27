#pragma once

#include "../../jube/jube.h"

int node_url_init(const JubeHostAPI* host);
void node_url_shutdown(void);
void node_url_runtime_attach(void* session);
void node_url_runtime_reset(void* session);
void node_url_runtime_detach(void* session);
Item node_url_namespace(void);
