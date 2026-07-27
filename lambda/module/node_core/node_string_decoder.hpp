#pragma once

#include "../../jube/jube.h"

int node_string_decoder_init(const JubeHostAPI* host);
void node_string_decoder_shutdown(void);
void node_string_decoder_runtime_attach(void* session);
void node_string_decoder_runtime_reset(void* session);
void node_string_decoder_runtime_detach(void* session);
Item node_string_decoder_namespace(void);
