#include "worker.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    if (argc != 4) return 2;
    char* end = nullptr;
    errno = 0; long fd = strtol(argv[1], &end, 10);
    if (errno || !end || *end || fd < 3 || fd > INT_MAX) return 2;
    errno = 0; unsigned long long session = strtoull(argv[2], &end, 10);
    if (errno || !end || *end || !session || argv[2][0] == '-') return 2;
    return lambda_safety_worker_run((int)fd, (uint64_t)session, argv[3]);
}
