#include "confined_process.h"
#include "ipc_stream.h"
#include "path_str.h"
#include <errno.h>
#ifdef __APPLE__
#include <fcntl.h>
#include <libproc.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

bool confined_process_worker_handles(int broker_fd) {
    if (broker_fd < 3) { errno = EINVAL; return false; }
    struct stat input, null_device, output;
    if (fstat(0, &input) || stat("/dev/null", &null_device)) return false;
    if (!S_ISCHR(input.st_mode) || input.st_dev != null_device.st_dev ||
            input.st_ino != null_device.st_ino || input.st_rdev != null_device.st_rdev) goto denied;
    for (int fd = 1; fd <= 2; fd++) {
        if (fstat(fd, &output)) return false;
        if (!S_ISFIFO(output.st_mode) || (fcntl(fd, F_GETFL) & O_ACCMODE) != O_WRONLY) goto denied;
    }
    if (!ipc_stream_is_connected_local(broker_fd)) return false;
    struct proc_taskinfo task;
    // No other bootstrap thread may race this descriptor snapshot or hold work
    // outside the boundary. Runtime threads start only after this check.
    if (proc_pidinfo(getpid(), PROC_PIDTASKINFO, 0, &task, sizeof(task)) != sizeof(task) ||
            task.pti_threadnum != 1) goto denied;
    struct proc_fdinfo descriptors[5];
    int size = proc_pidinfo(getpid(), PROC_PIDLISTFDS, 0, descriptors, sizeof(descriptors));
    if (size != 4 * sizeof(descriptors[0])) goto denied;
    for (unsigned i = 0; i < 4; i++) {
        int fd = descriptors[i].proc_fd;
        if (fd < 0 || (fd > 2 && fd != broker_fd)) goto denied;
        // Named FIFOs are vnodes; only the launcher's anonymous pipes qualify.
        uint32_t expected = fd == broker_fd ? PROX_FDTYPE_SOCKET : fd == 0 ? PROX_FDTYPE_VNODE : PROX_FDTYPE_PIPE;
        if (descriptors[i].proc_fdtype != expected) goto denied;
    }
    return true;
denied:
    errno = EACCES; return false;
}
bool confined_process_worker_limits(void) {
    static const struct { int resource; rlim_t value; } caps[] = {
        {RLIMIT_CPU, 10}, {RLIMIT_FSIZE, 1024 * 1024}, {RLIMIT_NOFILE, 64}, {RLIMIT_NPROC, 0}, {RLIMIT_CORE, 0}
    };
    for (size_t i = 0; i < sizeof(caps) / sizeof(caps[0]); i++) {
        struct rlimit limit = {caps[i].value, caps[i].value};
        if (setrlimit(caps[i].resource, &limit)) return false;
    }
    return true;
}
bool confined_process_worker_paths(const char* relative_image, char image[4096], char directory[4096]) {
    if (!relative_image || !*relative_image || relative_image[0] == '/' || !image || !directory) { errno = EINVAL; return false; }
    char executable[4096], joined[4096], canonical[4096];
    if (proc_pidpath(getpid(), executable, sizeof(executable)) <= 0 || !getcwd(directory, 4096)) return false;
    char* slash = strrchr(executable, '/');
    if (!slash) { errno = EINVAL; return false; }
    *slash = 0;
    if (path_str_join_posix_into(joined, sizeof(joined), executable, relative_image) < 0 ||
            path_str_normalize_lexical_posix(joined, image, 4096, false) < 0) { errno = ENAMETOOLONG; return false; }
    if (!realpath(image, canonical)) return false;
    // image selection is fixed by the sealed bootstrap, never redirected by aliases.
    if (strcmp(image, canonical)) { errno = EACCES; return false; }
    return true;
}
#else
bool confined_process_worker_handles(int broker_fd) { (void)broker_fd; errno = ENOTSUP; return false; }
bool confined_process_worker_limits(void) { errno = ENOTSUP; return false; }
bool confined_process_worker_paths(const char* relative_image, char image[4096], char directory[4096]) {
    (void)relative_image; (void)image; (void)directory; errno = ENOTSUP; return false;
}
#endif
