#ifndef LAMBDA_MODULE_NODE_CORE_NODE_TTY_HPP
#define LAMBDA_MODULE_NODE_CORE_NODE_TTY_HPP

#include "../../jube/jube.h"

Item node_tty_namespace(void);
int node_tty_init(const JubeHostAPI* host);
void node_tty_shutdown(void);
void node_tty_runtime_attach(void* session);
void node_tty_runtime_reset(void* session);
void node_tty_runtime_detach(void* session);

#endif
