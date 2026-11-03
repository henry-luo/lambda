#include "worker.h"

extern "C" __attribute__((visibility("default")))
int lambda_safety_worker_entry_v1(int broker_fd, uint64_t session, const char* entry, const char* working_directory) {
    return lambda_safety_worker_run_preconfined(broker_fd, session, entry, working_directory);
}
