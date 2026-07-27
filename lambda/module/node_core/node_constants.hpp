#ifndef LAMBDA_MODULE_NODE_CORE_NODE_CONSTANTS_HPP
#define LAMBDA_MODULE_NODE_CORE_NODE_CONSTANTS_HPP

#include "../../jube/jube.h"

Item node_constants_namespace(void);
int node_constants_init(const JubeHostAPI* host);
void node_constants_shutdown(void);
void node_constants_runtime_attach(void* session);
void node_constants_runtime_reset(void* session);
void node_constants_runtime_detach(void* session);

#endif
