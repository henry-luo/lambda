#pragma once

#include "../../jube/jube.h"

int node_path_init(const JubeHostAPI* host);
void node_path_shutdown(void);
void node_path_runtime_attach(void* session);
void node_path_runtime_reset(void* session);
void node_path_runtime_detach(void* session);
Item node_path_namespace(void);
Item node_path_win32_namespace(void);
