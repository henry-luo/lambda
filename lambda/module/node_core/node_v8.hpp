#ifndef LAMBDA_MODULE_NODE_CORE_NODE_V8_HPP
#define LAMBDA_MODULE_NODE_CORE_NODE_V8_HPP

#include "../../jube/jube.h"

Item node_v8_namespace(void);
int node_v8_init(const JubeHostAPI* host);
void node_v8_shutdown(void);
void node_v8_runtime_attach(void* session);
void node_v8_runtime_reset(void* session);
void node_v8_runtime_detach(void* session);

#endif
