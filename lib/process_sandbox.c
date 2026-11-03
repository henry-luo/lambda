#include "process_sandbox.h"
#include <errno.h>
#include <string.h>

#ifdef __APPLE__
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <unistd.h>

typedef int (*SandboxApply)(const char*, uint64_t, const char* const*, char**);
typedef int (*SandboxCheck)(pid_t, const char*, int, ...);
typedef void (*SandboxFree)(char*);
static SandboxApply sandbox_apply;
static SandboxCheck sandbox_check;
static SandboxFree sandbox_free;
static enum { PROFILE_NONE, PROFILE_OFFLINE, PROFILE_IMAGE } entered;
static pthread_once_t symbols_once = PTHREAD_ONCE_INIT;

static void load_sandbox_symbols(void) {
    // the private Darwin interface is version-qualified by native tests. Never
    // substitute an unrestricted worker if a symbol/profile stops working.
    void* library = dlopen("/usr/lib/libsandbox.1.dylib", RTLD_NOW | RTLD_LOCAL);
    if (!library) return;
    sandbox_apply = (SandboxApply)dlsym(library, "sandbox_init_with_parameters");
    sandbox_check = (SandboxCheck)dlsym(library, "sandbox_check");
    sandbox_free = (SandboxFree)dlsym(library, "sandbox_free_error");
    // retain the system image for the lifetime of these function pointers.
}
static bool sandbox_symbols(void) {
    if (pthread_once(&symbols_once, load_sandbox_symbols) ||
            !sandbox_apply || !sandbox_check || !sandbox_free) { errno = ENOTSUP; return false; }
    return true;
}
bool process_sandbox_is_applied(int process_id) {
    return process_id > 0 && sandbox_symbols() && sandbox_check((pid_t)process_id, NULL, 0) == 1;
}
static const char offline_profile[] =
        "(version 1)(deny default)"
        // Darwin does not include every operation in `default`; explicitly
        // close process inspection and mappings. The legacy iokit* group also
        // omits get-properties; it needs its own denial, including held rights.
        "(deny file-map-executable dynamic-code-generation iokit* iokit-get-properties process-info* nvram*)"
        "(allow process-info* (target self))"
        "(allow file-read* file-map-executable (subpath \"/System/Library\") (subpath \"/usr/lib\"))"
        // libuv's EVFILT_USER wakeup uses an open of / as a placeholder fd.
        // this literal permits the directory only, never any descendant.
        "(allow file-read* (literal \"/\"))"
        "(allow file-read-data (literal \"/dev/urandom\"))"
        // Revalidate stdin against the immutable device object, including when
        // a native qualification fixture has already installed this policy.
        "(allow file-read-metadata (literal \"/dev/null\"))"
        "(deny file-link user-preference-read user-preference-write)"
        // PT_TRACE_ME checks the parent's MAC label, not this worker's label.
        // Reject the syscall itself before that cross-process authorization.
        "(deny syscall-unix (syscall-number SYS_ptrace))"
        "(deny network* process-exec process-fork mach-lookup mach-register mach-priv-host-port)";

static bool enter_profile(const char* profile, const char* const* parameters) {
    if (!sandbox_symbols()) return false;
    if (entered || sandbox_check(getpid(), NULL, 0) != 0) { errno = EACCES; return false; }
    char* error = NULL;
    int result = sandbox_apply(profile, 0, parameters, &error);
    if (error) sandbox_free(error);
    if (result || !process_sandbox_is_applied(getpid())) { errno = EACCES; return false; }
    return true;
}
bool process_sandbox_enter_offline(void) {
    if (entered == PROFILE_OFFLINE) return process_sandbox_is_applied(getpid());
    if (!enter_profile(offline_profile, NULL)) return false;
    entered = PROFILE_OFFLINE; return true;
}
void* process_sandbox_load_image(const char* image, const char* symbol) {
    if (!image || image[0] != '/' || !symbol || !*symbol) { errno = EINVAL; return NULL; }
    // Only this fixed runtime image gains read/map authority; package/app code
    // cannot choose the parameter or relax a policy already installed here.
    static const char permission[] = "(allow file-read* file-map-executable (literal (param \"RUNTIME_IMAGE\")))";
    char profile[sizeof(offline_profile) + sizeof(permission) - 1];
    memcpy(profile, offline_profile, sizeof(offline_profile) - 1);
    memcpy(profile + sizeof(offline_profile) - 1, permission, sizeof(permission));
    const char* parameters[] = {"RUNTIME_IMAGE", image, NULL};
    if (!enter_profile(profile, parameters)) return NULL;
    entered = PROFILE_IMAGE;
    void* library = dlopen(image, RTLD_NOW | RTLD_LOCAL);
    void* entry = library ? dlsym(library, symbol) : NULL;
    // Keep the image alive through all evaluator/scheduler teardown.
    if (!entry) errno = EACCES;
    return entry;
}

#else
bool process_sandbox_enter_offline(void) { errno = ENOTSUP; return false; }
void* process_sandbox_load_image(const char* image, const char* symbol) {
    (void)image; (void)symbol; errno = ENOTSUP; return NULL;
}
bool process_sandbox_is_applied(int process_id) { (void)process_id; errno = ENOTSUP; return false; }
#endif
