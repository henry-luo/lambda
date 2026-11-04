#pragma once
#include <stdint.h>

// fresh dedicated-process bootstrap: apply native containment before runtime
// initialization. The verified launcher admits grants and checks the label.
int lambda_safety_worker_run(int broker_fd, uint64_t session, const char* entry, const char* working_directory);
// Internal sealed-image entry only; the verified minimal bootstrap owns policy
// establishment. Kernel readback alone is insufficient to authenticate policy.
int lambda_safety_worker_run_preconfined(int broker_fd, uint64_t session, const char* entry, const char* working_directory);
