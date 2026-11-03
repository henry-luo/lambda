#pragma once
#include <stdint.h>

// Fresh dedicated-process runtime side of an already confined/admitted launch. This does not install
// OS containment or admit grants; the trusted launcher must do both first.
int lambda_safety_worker_run(int broker_fd, uint64_t session, const char* entry);
