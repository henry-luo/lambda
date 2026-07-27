#ifndef LAMBDA_MODULE_NODE_CORE_NODE_TIMERS_HPP
#define LAMBDA_MODULE_NODE_CORE_NODE_TIMERS_HPP

#include "../../jube/jube.h"

Item node_timers_promises_namespace(void);
Item node_timers_namespace(void);
int node_timers_init(const JubeHostAPI* host);
void node_timers_shutdown(void);
void node_timers_runtime_attach(void* session);
void node_timers_runtime_reset(void* session);
void node_timers_runtime_detach(void* session);

#endif
