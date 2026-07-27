#ifndef LAMBDA_MODULE_NODE_CORE_NODE_WORKERS_HPP
#define LAMBDA_MODULE_NODE_CORE_NODE_WORKERS_HPP

#include "../../jube/jube.h"

Item node_workers_namespace(void);
int node_workers_init(const JubeHostAPI* host);
void node_workers_shutdown(void);
void node_workers_runtime_attach(void* session);
void node_workers_runtime_reset(void* session);
void node_workers_runtime_detach(void* session);

#endif
