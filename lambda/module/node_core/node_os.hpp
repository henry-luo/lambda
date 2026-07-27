#pragma once

#include "../../jube/jube.h"

int node_os_init(const JubeHostAPI* host);
void node_os_shutdown(void);
void node_os_runtime_attach(void* session);
void node_os_runtime_reset(void* session);
void node_os_runtime_detach(void* session);
Item node_os_namespace(void);
