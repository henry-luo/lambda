#ifndef LAMBDA_MODULE_NODE_CORE_NODE_PROCESS_HPP
#define LAMBDA_MODULE_NODE_CORE_NODE_PROCESS_HPP

#include "../../jube/jube.h"

int node_process_init(const JubeHostAPI* host);
void node_process_shutdown(void);
void node_process_runtime_attach(void* session);
void node_process_runtime_reset(void* session);
void node_process_runtime_detach(void* session);

#endif
