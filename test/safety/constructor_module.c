#include "constructor_fixture.h"

__attribute__((visibility("default")))
int lambda_safety_worker_entry_v1(int broker_fd, uint64_t session, const char* entry, const char* directory) {
    (void)broker_fd; (void)session; (void)entry; (void)directory;
    char report[192];
    int size = safety_constructor_report(report, sizeof(report));
    int result = size > 0 && (size_t)size < sizeof(report) && write(1, report, size) == size ? 0 : 2;
    safety_constructor_close(); return result;
}
