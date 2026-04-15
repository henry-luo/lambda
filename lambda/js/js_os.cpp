/**
 * js_os.cpp — Node.js-style 'os' module for LambdaJS
 *
 * Provides operating system-related utility methods and properties.
 * Registered as built-in module 'os' via js_module_get().
 */
#include "js_runtime.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/mem.h"
#include "../../lib/shell.h"

#include <cstring>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#define getpid _getpid
#else
#include <sys/utsname.h>
#include <unistd.h>
#include <pwd.h>
#endif

#ifdef __APPLE__
#include <sys/sysctl.h>
#include <mach/mach.h>
#include <mach/mach_host.h>
#endif

#ifdef __linux__
#include <sys/sysinfo.h>
#endif

// Helper: make JS undefined value
static inline Item make_js_undefined() {
    return (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
}

// Helper: create a string Item from a C string
static Item make_string_item(const char* str, int len) {
    if (!str) return ItemNull;
    String* s = heap_create_name(str, (size_t)len);
    return (Item){.item = s2it(s)};
}

static Item make_string_item(const char* str) {
    if (!str) return ItemNull;
    return make_string_item(str, (int)strlen(str));
}

// =============================================================================
// OS Information Functions
// =============================================================================

// os.platform() — returns Node.js-style platform string
extern "C" Item js_os_platform(void) {
#ifdef __APPLE__
    return make_string_item("darwin");
#elif defined(__linux__)
    return make_string_item("linux");
#elif defined(_WIN32)
    return make_string_item("win32");
#else
    return make_string_item("unknown");
#endif
}

// os.arch() — returns Node.js-style architecture string
extern "C" Item js_os_arch(void) {
#if defined(__aarch64__) || defined(_M_ARM64)
    return make_string_item("arm64");
#elif defined(__x86_64__) || defined(_M_X64)
    return make_string_item("x64");
#elif defined(__i386__) || defined(_M_IX86)
    return make_string_item("ia32");
#elif defined(__arm__) || defined(_M_ARM)
    return make_string_item("arm");
#else
    return make_string_item("unknown");
#endif
}

// os.type() — returns OS type (same as uname().sysname)
extern "C" Item js_os_type(void) {
#ifdef __APPLE__
    return make_string_item("Darwin");
#elif defined(__linux__)
    return make_string_item("Linux");
#elif defined(_WIN32)
    return make_string_item("Windows_NT");
#else
    return make_string_item("Unknown");
#endif
}

// os.hostname()
extern "C" Item js_os_hostname(void) {
    static char hostname[256] = {0};
    if (hostname[0] == 0) {
#ifdef _WIN32
        DWORD size = sizeof(hostname);
        if (!GetComputerNameA(hostname, &size)) {
            return make_string_item("unknown");
        }
#else
        if (gethostname(hostname, sizeof(hostname)) != 0) {
            return make_string_item("unknown");
        }
#endif
    }
    return make_string_item(hostname);
}

// os.homedir()
extern "C" Item js_os_homedir(void) {
#ifdef _WIN32
    const char* home = shell_getenv("USERPROFILE");
    if (home) return make_string_item(home);
    return make_string_item("");
#else
    const char* home = shell_getenv("HOME");
    if (home) return make_string_item(home);
    struct passwd* pw = getpwuid(getuid());
    if (pw) return make_string_item(pw->pw_dir);
    return make_string_item("");
#endif
}

// os.tmpdir()
extern "C" Item js_os_tmpdir(void) {
#ifdef _WIN32
    static char temp[MAX_PATH] = {0};
    if (temp[0] == 0) {
        GetTempPathA(sizeof(temp), temp);
    }
    return make_string_item(temp);
#else
    const char* temp = shell_getenv("TMPDIR");
    if (temp) return make_string_item(temp);
    return make_string_item("/tmp");
#endif
}

// os.totalmem()
extern "C" Item js_os_totalmem(void) {
    int64_t total = 0;
#ifdef __APPLE__
    size_t size = sizeof(total);
    sysctlbyname("hw.memsize", &total, &size, NULL, 0);
#elif defined(_WIN32)
    MEMORYSTATUSEX mem;
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) total = mem.ullTotalPhys;
#elif defined(__linux__)
    struct sysinfo si;
    if (sysinfo(&si) == 0) total = (int64_t)si.totalram * si.mem_unit;
#endif
    return (Item){.item = i2it(total)};
}

// os.freemem()
extern "C" Item js_os_freemem(void) {
    int64_t free_mem = 0;
#ifdef __APPLE__
    vm_statistics64_data_t vm_stat;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                          (host_info64_t)&vm_stat, &count) == KERN_SUCCESS) {
        int64_t page_size = 0;
        size_t size = sizeof(page_size);
        sysctlbyname("hw.pagesize", &page_size, &size, NULL, 0);
        free_mem = vm_stat.free_count * page_size;
    }
#elif defined(_WIN32)
    MEMORYSTATUSEX mem;
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) free_mem = mem.ullAvailPhys;
#elif defined(__linux__)
    struct sysinfo si;
    if (sysinfo(&si) == 0) free_mem = (int64_t)si.freeram * si.mem_unit;
#endif
    return (Item){.item = i2it(free_mem)};
}

// os.cpus() — returns array of CPU info objects
extern "C" Item js_os_cpus(void) {
    int num_cpus = 1;
#ifdef __APPLE__
    size_t size = sizeof(num_cpus);
    sysctlbyname("hw.logicalcpu", &num_cpus, &size, NULL, 0);
#elif defined(_WIN32)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    num_cpus = si.dwNumberOfProcessors;
#else
    num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
#endif

    Item arr = js_array_new(0);
    for (int i = 0; i < num_cpus; i++) {
        Item cpu = js_new_object();
        js_property_set(cpu, make_string_item("model"), make_string_item("CPU"));
        js_property_set(cpu, make_string_item("speed"), (Item){.item = i2it(0)});
        Item times = js_new_object();
        js_property_set(times, make_string_item("user"), (Item){.item = i2it(0)});
        js_property_set(times, make_string_item("nice"), (Item){.item = i2it(0)});
        js_property_set(times, make_string_item("sys"), (Item){.item = i2it(0)});
        js_property_set(times, make_string_item("idle"), (Item){.item = i2it(0)});
        js_property_set(times, make_string_item("irq"), (Item){.item = i2it(0)});
        js_property_set(cpu, make_string_item("times"), times);
        js_array_push(arr, cpu);
    }
    return arr;
}

// os.uptime()
extern "C" Item js_os_uptime(void) {
    double uptime = 0.0;
#ifdef __APPLE__
    struct timeval boottime;
    size_t size = sizeof(boottime);
    if (sysctlbyname("kern.boottime", &boottime, &size, NULL, 0) == 0) {
        time_t now = time(NULL);
        uptime = difftime(now, boottime.tv_sec);
    }
#elif defined(_WIN32)
    uptime = GetTickCount64() / 1000.0;
#elif defined(__linux__)
    struct sysinfo si;
    if (sysinfo(&si) == 0) uptime = si.uptime;
#endif
    return (Item){.item = d2it(uptime)};
}

// os.endianness()
extern "C" Item js_os_endianness(void) {
    uint16_t test = 1;
    return make_string_item(*(uint8_t*)&test ? "LE" : "BE");
}

// os.release()
extern "C" Item js_os_release(void) {
#ifdef __APPLE__
    static char version[128] = {0};
    if (version[0] == 0) {
        size_t size = sizeof(version);
        if (sysctlbyname("kern.osrelease", version, &size, NULL, 0) != 0) {
            return make_string_item("Unknown");
        }
    }
    return make_string_item(version);
#elif defined(_WIN32)
    return make_string_item("Unknown");
#else
    static struct utsname info;
    static bool initialized = false;
    if (!initialized) {
        if (uname(&info) == 0) initialized = true;
    }
    return make_string_item(initialized ? info.release : "Unknown");
#endif
}

// os.version()
extern "C" Item js_os_version(void) {
#ifdef __APPLE__
    static char kernel[256] = {0};
    if (kernel[0] == 0) {
        size_t size = sizeof(kernel);
        if (sysctlbyname("kern.version", kernel, &size, NULL, 0) != 0) {
            return make_string_item("Unknown");
        }
        char* nl = strchr(kernel, '\n');
        if (nl) *nl = '\0';
    }
    return make_string_item(kernel);
#elif defined(_WIN32)
    return make_string_item("Unknown");
#else
    static struct utsname info;
    static bool initialized = false;
    if (!initialized) {
        if (uname(&info) == 0) initialized = true;
    }
    return make_string_item(initialized ? info.version : "Unknown");
#endif
}

// os.networkInterfaces() — stub returning empty object
extern "C" Item js_os_networkInterfaces(void) {
    return js_new_object();
}

// os.userInfo() — returns user information
extern "C" Item js_os_userInfo(void) {
    Item obj = js_new_object();
#ifdef _WIN32
    const char* username = shell_getenv("USERNAME");
    const char* homedir = shell_getenv("USERPROFILE");
    js_property_set(obj, make_string_item("uid"), (Item){.item = i2it(-1)});
    js_property_set(obj, make_string_item("gid"), (Item){.item = i2it(-1)});
    js_property_set(obj, make_string_item("username"), make_string_item(username ? username : ""));
    js_property_set(obj, make_string_item("homedir"), make_string_item(homedir ? homedir : ""));
    js_property_set(obj, make_string_item("shell"), ItemNull);
#else
    struct passwd* pw = getpwuid(getuid());
    js_property_set(obj, make_string_item("uid"), (Item){.item = i2it((int64_t)getuid())});
    js_property_set(obj, make_string_item("gid"), (Item){.item = i2it((int64_t)getgid())});
    js_property_set(obj, make_string_item("username"), make_string_item(pw ? pw->pw_name : ""));
    js_property_set(obj, make_string_item("homedir"), make_string_item(pw ? pw->pw_dir : ""));
    js_property_set(obj, make_string_item("shell"), make_string_item(pw ? pw->pw_shell : ""));
#endif
    return obj;
}

// os.loadavg() — returns [1min, 5min, 15min] load averages
extern "C" Item js_os_loadavg(void) {
    Item arr = js_array_new(3);
#ifndef _WIN32
    double loadavg[3] = {0, 0, 0};
    getloadavg(loadavg, 3);
    for (int i = 0; i < 3; i++) {
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = loadavg[i];
        js_array_push(arr, (Item){.item = d2it(fp)});
    }
#else
    // Windows doesn't have getloadavg
    for (int i = 0; i < 3; i++) {
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = 0.0;
        js_array_push(arr, (Item){.item = d2it(fp)});
    }
#endif
    return arr;
}

// =============================================================================
// os Module Namespace Object
// =============================================================================

static Item os_namespace = {0};

static void js_os_set_method(Item ns, const char* name, void* func_ptr, int param_count) {
    Item key = make_string_item(name);
    Item fn = js_new_function(func_ptr, param_count);
    js_property_set(ns, key, fn);
}

extern "C" Item js_get_os_namespace(void) {
    if (os_namespace.item != 0) return os_namespace;

    os_namespace = js_new_object();

    js_os_set_method(os_namespace, "platform",          (void*)js_os_platform, 0);
    js_os_set_method(os_namespace, "arch",              (void*)js_os_arch, 0);
    js_os_set_method(os_namespace, "type",              (void*)js_os_type, 0);
    js_os_set_method(os_namespace, "hostname",          (void*)js_os_hostname, 0);
    js_os_set_method(os_namespace, "homedir",           (void*)js_os_homedir, 0);
    js_os_set_method(os_namespace, "tmpdir",            (void*)js_os_tmpdir, 0);
    js_os_set_method(os_namespace, "totalmem",          (void*)js_os_totalmem, 0);
    js_os_set_method(os_namespace, "freemem",           (void*)js_os_freemem, 0);
    js_os_set_method(os_namespace, "cpus",              (void*)js_os_cpus, 0);
    js_os_set_method(os_namespace, "uptime",            (void*)js_os_uptime, 0);
    js_os_set_method(os_namespace, "endianness",        (void*)js_os_endianness, 0);
    js_os_set_method(os_namespace, "release",           (void*)js_os_release, 0);
    js_os_set_method(os_namespace, "version",           (void*)js_os_version, 0);
    js_os_set_method(os_namespace, "networkInterfaces", (void*)js_os_networkInterfaces, 0);
    js_os_set_method(os_namespace, "userInfo",          (void*)js_os_userInfo, 0);
    js_os_set_method(os_namespace, "loadavg",           (void*)js_os_loadavg, 0);

    // constants
#ifdef _WIN32
    js_property_set(os_namespace, make_string_item("EOL"), make_string_item("\r\n"));
#else
    js_property_set(os_namespace, make_string_item("EOL"), make_string_item("\n"));
#endif

    // default export
    Item default_key = make_string_item("default");
    js_property_set(os_namespace, default_key, os_namespace);

    return os_namespace;
}

extern "C" void js_os_reset(void) {
    os_namespace = (Item){0};
}
