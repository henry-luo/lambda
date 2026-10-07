#pragma once
#include "../../../lib/lambda_api.h"

#include "../../jube/jube.h"

int node_events_init(const JubeHostAPI* host);
void node_events_shutdown(void);
void node_events_runtime_attach(void* session);
void node_events_runtime_reset(void* session);
void node_events_runtime_detach(void* session);
LAMBDA_RT_API Item node_events_namespace(void);
