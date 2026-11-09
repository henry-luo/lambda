#include "lib/confined_process.h"
#include "lib/process_sandbox.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>

// this target links only the minimal libSystem-based bootstrap mechanisms.
int main(int argc, char** argv) {
    if (argc != 4) return 2;
    char* end = NULL;
    errno = 0; long fd = strtol(argv[1], &end, 10);
    if (errno || !end || *end || fd < 3 || fd > INT_MAX) return 2;
    errno = 0; unsigned long long session = strtoull(argv[2], &end, 10);
    if (errno || !end || *end || !session || argv[2][0] == '-') return 2;
    char image[4096], directory[4096];
    if (!confined_process_worker_handles((int)fd) || !confined_process_worker_limits() ||
            !confined_process_worker_paths("../Frameworks/LambdaSafetyRuntime.dylib", image, directory)) return 2;
    typedef int (*WorkerEntry)(int, uint64_t, const char*, const char*);
    WorkerEntry entry = (WorkerEntry)process_sandbox_load_image(image, "lambda_safety_worker_entry_v1");
    return entry ? entry((int)fd, (uint64_t)session, argv[3], directory) : 2;
}
