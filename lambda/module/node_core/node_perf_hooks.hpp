#ifndef LAMBDA_MODULE_NODE_CORE_NODE_PERF_HOOKS_HPP
#define LAMBDA_MODULE_NODE_CORE_NODE_PERF_HOOKS_HPP

#include "../../jube/jube.h"

Item node_perf_hooks_namespace(void);
int node_perf_hooks_init(const JubeHostAPI* host);
void node_perf_hooks_shutdown(void);
void node_perf_hooks_runtime_attach(void* session);
void node_perf_hooks_runtime_reset(void* session);
void node_perf_hooks_runtime_detach(void* session);

#endif
