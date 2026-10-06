#pragma once

#include "../../jube/jube.h"

int node_readline_init(const JubeHostAPI* host);
void node_readline_shutdown(void);
void node_readline_runtime_attach(void* session);
void node_readline_runtime_reset(void* session);
void node_readline_runtime_detach(void* session);
Item node_readline_namespace(void);
Item node_readline_promises_namespace(void);
