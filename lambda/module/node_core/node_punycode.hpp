#pragma once

#include "../../jube/jube.h"

int node_punycode_init(const JubeHostAPI* host);
void node_punycode_shutdown(void);
void node_punycode_runtime_attach(void* session);
void node_punycode_runtime_reset(void* session);
void node_punycode_runtime_detach(void* session);
Item node_punycode_namespace(void);
