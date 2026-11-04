#include "../../lib/process_sandbox.h"
#include <arpa/inet.h>
#include <errno.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <mach/mach.h>
#include <libproc.h>
#include <signal.h>
#include <servers/bootstrap.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/event.h>
#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <uv.h>
#include <IOKit/IOKitLib.h>
#include <sys/wait.h>
#include <unistd.h>

static int open_error(const char* path, int flags) {
    int fd = open(path, flags, 0600);
    int result = fd < 0 ? errno : 0;
    if (fd >= 0) close(fd);
    return result;
}
static int lookup_preferences(void) {
    mach_port_t port = MACH_PORT_NULL;
    int result = bootstrap_look_up(bootstrap_port, "com.apple.cfprefsd.agent", &port);
    if (port != MACH_PORT_NULL) mach_port_deallocate(mach_task_self(), port);
    return result;
}
static int network_error(int operation, uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return errno;
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = operation ? 0 : htons(port);
    int status = operation ? bind(fd, (struct sockaddr*)&address, sizeof(address)) :
        connect(fd, (struct sockaddr*)&address, sizeof(address));
    int result = status ? errno : 0;
    close(fd); return result;
}
static io_service_t get_service(const char* name) {
    return IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching(name));
}

// standalone native positive controls and attacks against the production policy.
// no file grant or runtime callback participates in these attempted syscalls.
int main(int argc, char** argv) {
    if (argc != 10) return 2;
    bool confined = !strcmp(argv[1], "confined");
    if (!confined && strcmp(argv[1], "control")) return 2;
    // Retain a service right before entering, as a framework initializer might.
    // Device restrictions must apply to subsequent calls through that right.
    bool fresh = !strcmp(argv[2], "fresh");
    io_service_t platform_service = fresh ? 0 : get_service("IOPlatformExpertDevice");
    io_service_t power_service = fresh ? 0 : get_service("IOPMrootDomain");
    int held_file = fresh ? -1 : open(argv[5], O_RDWR);
    if (!fresh && held_file < 0) return 4;
    int before = process_sandbox_is_applied(getpid());
    if (confined && !process_sandbox_enter_offline()) return 3;
    if (fresh) {
        platform_service = get_service("IOPlatformExpertDevice");
        power_service = get_service("IOPMrootDomain");
    }
    if (!strcmp(argv[2], "exec")) {
        if (held_file >= 0) close(held_file);
        execl("/usr/bin/true", "true", (char*)NULL);
        return errno == EPERM || errno == EACCES ? 77 : 4;
    }
    int applied = process_sandbox_is_applied(getpid());
    int held_read_error = -1, held_map_error = -1;
    if (held_file >= 0) {
        // Existing file capabilities survive policy installation. The real
        // worker must reject them before any untrusted code can use them.
        char byte;
        held_read_error = pread(held_file, &byte, 1, 0) == 1 ? 0 : errno;
        void* mapping = mmap(NULL, 1, PROT_READ | PROT_WRITE, MAP_SHARED, held_file, 0);
        held_map_error = mapping == MAP_FAILED ? errno : 0;
        if (mapping != MAP_FAILED) munmap(mapping, 1);
        close(held_file);
    }
    int relax_status = -1;
    if (confined) {
        // an attacker can call Darwin directly after compromise. A permissive
        // second policy must not restore access denied by the first one.
        typedef int (*ApplyPolicy)(const char*, uint64_t, const char* const*, char**);
        void* library = dlopen("/usr/lib/libsandbox.1.dylib", RTLD_NOW | RTLD_LOCAL);
        ApplyPolicy apply = library ? (ApplyPolicy)dlsym(library, "sandbox_init_with_parameters") : NULL;
        if (!apply) return 4;
        relax_status = apply("(version 1)(allow default)", 0, NULL, NULL);
    }
    int read_error = open_error(argv[3], O_RDONLY);
    int write_error = open_error(argv[3], O_WRONLY);
    int create_error = open_error(argv[4], O_WRONLY | O_CREAT | O_EXCL);
    int other_error = open_error(argv[5], O_RDONLY);
    int symlink_error = open_error(argv[6], O_RDONLY);
    int hardlink_error = open_error(argv[7], O_RDONLY);
    int connect_error = network_error(0, (uint16_t)atoi(argv[8]));
    int bind_error = network_error(1, 0);
    int mach_error = lookup_preferences();
    pid_t child = fork(); int fork_error = child < 0 ? errno : 0;
    if (!child) _exit(0);
    if (child > 0) waitpid(child, NULL, 0);
    int system_error = open_error("/System/Library/CoreServices/SystemVersion.plist", O_RDONLY);
    struct proc_bsdinfo parent = {0};
    int process_info = proc_pidinfo(getppid(), PROC_PIDTBSDINFO, 0, &parent, sizeof(parent));
    int process_error = process_info == sizeof(parent) ? 0 : (errno ? errno : EIO);
    int signal_status = kill(getppid(), 0); // permission check only; deliver no signal
    int signal_error = signal_status ? errno : 0;
    CFTypeRef model = platform_service ? IORegistryEntryCreateCFProperty(platform_service,
        CFSTR("model"), kCFAllocatorDefault, 0) : NULL;
    int device_error = model ? 0 : EACCES;
    if (model) CFRelease(model);
    CFTypeRef identifier = platform_service ? IORegistryEntryCreateCFProperty(platform_service,
        CFSTR("IOPlatformUUID"), kCFAllocatorDefault, 0) : NULL;
    int identifier_error = identifier ? 0 : EACCES;
    if (identifier) CFRelease(identifier); // never serialize the machine identifier
    io_connect_t connection = MACH_PORT_NULL;
    int device_open = power_service ? IOServiceOpen(power_service, mach_task_self(), 0, &connection) : kIOReturnNotFound;
    if (connection) IOServiceClose(connection);
    if (power_service) IOObjectRelease(power_service);
    if (platform_service) IOObjectRelease(platform_service);
    int queue = kqueue(); int queue_error = queue < 0 ? errno : 0;
    if (queue >= 0) close(queue);
    int trace_error = ptrace(PT_TRACE_ME, 0, NULL, 0) ? errno : 0;
    // Exercise the direct libc syscall entry too; no ptrace wrapper authorizes it.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    int trace_raw_error = syscall(SYS_ptrace, PT_TRACE_ME, 0, NULL, 0) ? errno : 0;
#pragma clang diagnostic pop
    uv_loop_t loop = {0}; int loop_error = uv_loop_init(&loop);
    if (!loop_error) uv_loop_close(&loop);
    int ipc_error = write(atoi(argv[9]), "Y", 1) == 1 ? 0 : errno;
    char report[768];
    int size = snprintf(report, sizeof(report),
        "{\"before\":%d,\"applied\":%d,\"read_errno\":%d,\"write_errno\":%d,\"create_errno\":%d,"
        "\"other_errno\":%d,\"symlink_errno\":%d,\"hardlink_errno\":%d,\"connect_errno\":%d,"
        "\"bind_errno\":%d,\"mach_errno\":%d,\"fork_errno\":%d,\"system_errno\":%d,\"ipc_errno\":%d,\"relax_status\":%d,"
        "\"kqueue_errno\":%d,\"uv_loop_error\":%d,\"process_info_errno\":%d,\"public_model_errno\":%d,"
        "\"device_identifier_errno\":%d,\"device_open_status\":%d,\"signal_errno\":%d,\"trace_errno\":%d,\"trace_raw_errno\":%d,"
        "\"held_read_errno\":%d,\"held_map_errno\":%d}\n",
        before, applied, read_error, write_error, create_error, other_error, symlink_error,
        hardlink_error, connect_error, bind_error, mach_error, fork_error, system_error, ipc_error, relax_status,
        queue_error, loop_error, process_error, device_error, identifier_error, device_open, signal_error, trace_error, trace_raw_error,
        held_read_error, held_map_error);
    if (size < 0 || (size_t)size >= sizeof(report)) return 2;
    return write(STDOUT_FILENO, report, (size_t)size) == size ? 0 : 2;
}
