/**
 * js_os.cpp — Node.js-style 'os' module for LambdaJS
 *
 * Provides operating system-related utility methods and properties.
 * Registered by node-core through its Jube namespace descriptor.
 */
#include "node_os.hpp"
#include "../../jube/jube_registry.h"
#include "../../../lib/shell.h"

#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <ctime>

static const JubeHostAPI* node_os_host = NULL;
struct NodeOsSessionState { void* session; bool rooted; Item namespace_cache; bool priority_override_set; int priority_override; };
static NodeOsSessionState* node_os_state(void) { return (NodeOsSessionState*)jube_node_current_module_state(JUBE_NODE_MODULE_STATE_OS); }
#define node_os_session (node_os_state()->session)
#define node_os_rooted (node_os_state()->rooted)
#define os_namespace (node_os_state()->namespace_cache)
#define node_os_priority_override_set (node_os_state()->priority_override_set)
#define node_os_priority_override (node_os_state()->priority_override)

static Item node_os_string(const char* text, int length) {
    if (!node_os_host || !node_os_host->value ||
            !node_os_host->value->string_from_utf8_n || !text || length < 0) return ItemNull;
    return node_os_host->value->string_from_utf8_n(text, (size_t)length);
}

static Item node_os_string(const char* text) {
    return node_os_string(text, text ? (int)strlen(text) : 0);
}

static bool node_os_roots_begin(JubeRootFrame* frame, size_t count) {
    return node_os_host && node_os_host->node && node_os_host->node->roots &&
            node_os_host->node->roots->root_frame_begin &&
            node_os_host->node->roots->root_frame_take_slot &&
            node_os_host->node->roots->root_frame_end &&
            node_os_host->node->roots->root_frame_begin(frame, count);
}

#define get_type_id(VALUE) node_os_host->value->kind(VALUE)
#define LMD_TYPE_UNDEFINED JUBE_VALUE_UNDEFINED
#define LMD_TYPE_MAP JUBE_VALUE_OBJECT
#define make_string_item node_os_string
#define js_make_number(VALUE) node_os_host->script->make_number(VALUE)
#define push_d(VALUE) node_os_host->script->make_number(VALUE)
#define js_array_new(CAPACITY) node_os_host->value->array_new(CAPACITY)
#define js_array_push(ARRAY, VALUE) node_os_host->value->array_push(ARRAY, VALUE)
#define js_new_object() node_os_host->value->new_object()
#define js_get_key_default(OBJECT, KEY) node_os_host->value->property_get(OBJECT, KEY)
#define js_set_key_default(OBJECT, KEY, VALUE) node_os_host->value->property_set(OBJECT, KEY, VALUE)
#define js_object_freeze(OBJECT) node_os_host->script->object_freeze(OBJECT)
#define js_mark_non_writable(OBJECT, KEY) node_os_host->script->mark_non_writable(OBJECT, KEY)

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#define getpid _getpid
#else
#include <sys/utsname.h>
#include <sys/resource.h>
#include <unistd.h>
#include <pwd.h>
#include <signal.h>
#endif

#ifdef __APPLE__
#include <sys/sysctl.h>
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <mach/processor_info.h>
#endif

#ifdef __linux__
#include <sys/sysinfo.h>
#endif

#if !defined(_WIN32)
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <net/if.h>
#ifdef __APPLE__
#include <net/if_dl.h>
#endif
#ifdef __linux__
#include <linux/if_packet.h>
#endif
#endif

// Helper: create a string Item from a C string
static Item make_node_number_i64(int64_t value) {
    // Node os APIs expose host counters as JS Number; avoid compact-int packing,
    // which turns values outside Lambda's safe-int band into ITEM_ERROR.
    return js_make_number((double)value);
}

static Item node_os_root_value(const uint64_t* slot) {
    return (Item){.item = slot ? *slot : 0};
}

static Item node_os_set_item_property(Item object, const char* name, Item value) {
    JubeRootFrame frame = {};
    if (!node_os_roots_begin(&frame, 3)) return object;
    uint64_t* object_root = node_os_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* key_root = node_os_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* value_root = node_os_host->node->roots->root_frame_take_slot(&frame);
    if (!object_root || !key_root || !value_root) {
        node_os_host->node->roots->root_frame_end(&frame);
        return object;
    }
    *object_root = object.item;
    *value_root = value.item;
    Item key = make_string_item(name);
    *key_root = key.item;
    node_os_host->value->property_set(node_os_root_value(object_root),
        node_os_root_value(key_root), node_os_root_value(value_root));
    Item result = node_os_root_value(object_root);
    node_os_host->node->roots->root_frame_end(&frame);
    return result;
}

static Item node_os_cpu_entry(const char* model, int64_t speed, int64_t user,
                              int64_t nice, int64_t system, int64_t idle, int64_t irq) {
    JubeRootFrame frame = {};
    if (!node_os_roots_begin(&frame, 2)) return ItemNull;
    uint64_t* cpu_root = node_os_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* times_root = node_os_host->node->roots->root_frame_take_slot(&frame);
    if (!cpu_root || !times_root) {
        node_os_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    Item cpu = js_new_object();
    *cpu_root = cpu.item;
    cpu = node_os_set_item_property(node_os_root_value(cpu_root), "model", make_string_item(model));
    *cpu_root = cpu.item;
    cpu = node_os_set_item_property(node_os_root_value(cpu_root), "speed", make_node_number_i64(speed));
    *cpu_root = cpu.item;
    Item times = js_new_object();
    *times_root = times.item;
    times = node_os_set_item_property(node_os_root_value(times_root), "user", make_node_number_i64(user));
    *times_root = times.item;
    times = node_os_set_item_property(node_os_root_value(times_root), "nice", make_node_number_i64(nice));
    *times_root = times.item;
    times = node_os_set_item_property(node_os_root_value(times_root), "sys", make_node_number_i64(system));
    *times_root = times.item;
    times = node_os_set_item_property(node_os_root_value(times_root), "idle", make_node_number_i64(idle));
    *times_root = times.item;
    times = node_os_set_item_property(node_os_root_value(times_root), "irq", make_node_number_i64(irq));
    *times_root = times.item;
    cpu = node_os_set_item_property(node_os_root_value(cpu_root), "times",
        node_os_root_value(times_root));
    *cpu_root = cpu.item;
    Item result = node_os_root_value(cpu_root);
    node_os_host->node->roots->root_frame_end(&frame);
    return result;
}

static void node_os_cpu_append(Item array, Item cpu) {
    JubeRootFrame frame = {};
    if (!node_os_roots_begin(&frame, 2)) return;
    uint64_t* array_root = node_os_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* cpu_root = node_os_host->node->roots->root_frame_take_slot(&frame);
    if (!array_root || !cpu_root) {
        node_os_host->node->roots->root_frame_end(&frame);
        return;
    }
    *array_root = array.item;
    *cpu_root = cpu.item;
    node_os_host->value->array_push(node_os_root_value(array_root), node_os_root_value(cpu_root));
    node_os_host->node->roots->root_frame_end(&frame);
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
    char hostname[256] = {};
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
    // Windows: check TEMP, then TMP, then system GetTempPath
    char wbuf[MAX_PATH] = {};
    const char* temp = getenv("TEMP");
    if (!temp || !temp[0]) temp = getenv("TMP");
    if (!temp || !temp[0]) {
        GetTempPathA(sizeof(wbuf), wbuf);
        temp = wbuf;
    }
    // strip trailing slashes (but not if root like "C:\")
    size_t len = strlen(temp);
    while (len > 1 && (temp[len-1] == '\\' || temp[len-1] == '/')) {
        // don't strip "C:\"
        if (len == 3 && temp[1] == ':') break;
        len--;
    }
    char buf[MAX_PATH];
    memcpy(buf, temp, len);
    buf[len] = '\0';
    return make_string_item(buf);
#else
    // Unix: check TMPDIR, then TMP, then TEMP, fallback to /tmp
    const char* temp = getenv("TMPDIR");
    if (!temp || !temp[0]) temp = getenv("TMP");
    if (!temp || !temp[0]) temp = getenv("TEMP");
    if (!temp || !temp[0]) temp = "/tmp";  // TMP_PATH_OK: JS os.tmpdir() OS-standard fallback
    // strip trailing slashes (but not if root "/")
    size_t len = strlen(temp);
    while (len > 1 && temp[len-1] == '/') len--;
    char buf[4096];
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, temp, len);
    buf[len] = '\0';
    return make_string_item(buf);
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
    return make_node_number_i64(total);
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
    return make_node_number_i64(free_mem);
}

// os.cpus() — returns array of CPU info objects
extern "C" Item js_os_cpus(void) {
    int num_cpus = 1;
    const char* cpu_model = "Unknown";
    int64_t cpu_speed = 0; // MHz

#ifdef __APPLE__
    size_t size = sizeof(num_cpus);
    sysctlbyname("hw.logicalcpu", &num_cpus, &size, NULL, 0);

    char brand[256] = {};
    size = sizeof(brand);
    sysctlbyname("machdep.cpu.brand_string", brand, &size, NULL, 0);
    if (brand[0] != 0) cpu_model = brand;

    // CPU frequency in Hz → MHz
    int64_t freq = 0;
    size = sizeof(freq);
    if (sysctlbyname("hw.cpufrequency", &freq, &size, NULL, 0) == 0 && freq > 0) {
        cpu_speed = freq / 1000000; // Hz → MHz
    } else {
        // Apple Silicon: no hw.cpufrequency, estimate from perf cores
        cpu_speed = 3200; // reasonable default for M-series
    }

    // get per-CPU times from host_processor_info
    natural_t cpu_count = 0;
    processor_info_array_t cpu_info = NULL;
    mach_msg_type_number_t info_count = 0;
    host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO,
                        &cpu_count, &cpu_info, &info_count);

    JubeRootFrame roots = {};
    if (!node_os_roots_begin(&roots, 2)) return ItemNull;
    uint64_t* array_root = node_os_host->node->roots->root_frame_take_slot(&roots);
    uint64_t* cpu_root = node_os_host->node->roots->root_frame_take_slot(&roots);
    if (!array_root || !cpu_root) {
        node_os_host->node->roots->root_frame_end(&roots);
        return ItemNull;
    }
    Item arr = js_array_new(0);
    *array_root = arr.item;
    for (int i = 0; i < num_cpus; i++) {
        int64_t user_t = 0;
        int64_t nice_t = 0;
        int64_t sys_t = 0;
        int64_t idle_t = 0;
        if (cpu_info && i < (int)cpu_count) {
            processor_cpu_load_info_data_t* load =
                (processor_cpu_load_info_data_t*)cpu_info + i;
            user_t = (int64_t)load->cpu_ticks[CPU_STATE_USER] * 10;
            nice_t = (int64_t)load->cpu_ticks[CPU_STATE_NICE] * 10;
            sys_t = (int64_t)load->cpu_ticks[CPU_STATE_SYSTEM] * 10;
            idle_t = (int64_t)load->cpu_ticks[CPU_STATE_IDLE] * 10;
        }
        Item cpu = node_os_cpu_entry(cpu_model, cpu_speed, user_t, nice_t, sys_t, idle_t, 0);
        *cpu_root = cpu.item;
        node_os_cpu_append(node_os_root_value(array_root), node_os_root_value(cpu_root));
    }
    if (cpu_info) {
        vm_deallocate(mach_task_self(), (vm_address_t)cpu_info,
                      info_count * sizeof(natural_t));
    }
    Item result = node_os_root_value(array_root);
    node_os_host->node->roots->root_frame_end(&roots);
    return result;

#elif defined(_WIN32)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    num_cpus = si.dwNumberOfProcessors;

    JubeRootFrame roots = {};
    if (!node_os_roots_begin(&roots, 2)) return ItemNull;
    uint64_t* array_root = node_os_host->node->roots->root_frame_take_slot(&roots);
    uint64_t* cpu_root = node_os_host->node->roots->root_frame_take_slot(&roots);
    if (!array_root || !cpu_root) {
        node_os_host->node->roots->root_frame_end(&roots);
        return ItemNull;
    }
    Item arr = js_array_new(0);
    *array_root = arr.item;
    for (int i = 0; i < num_cpus; i++) {
        Item cpu = node_os_cpu_entry("CPU", 0, 0, 0, 0, 0, 0);
        *cpu_root = cpu.item;
        node_os_cpu_append(node_os_root_value(array_root), node_os_root_value(cpu_root));
    }
    Item result = node_os_root_value(array_root);
    node_os_host->node->roots->root_frame_end(&roots);
    return result;

#else
    // Linux
    num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (num_cpus < 1) num_cpus = 1;

    // read CPU model from /proc/cpuinfo
    char model_buf[256] = {};
    FILE* model_file = fopen("/proc/cpuinfo", "r");
    if (model_file) {
        char line[512];
        while (fgets(line, sizeof(line), model_file)) {
            if (strncmp(line, "model name", 10) == 0) {
                char* colon = strchr(line, ':');
                if (colon) {
                    colon++;
                    while (*colon == ' ') colon++;
                    char* nl = strchr(colon, '\n');
                    if (nl) *nl = '\0';
                    int mlen = (int)strlen(colon);
                    if (mlen >= (int)sizeof(model_buf)) mlen = (int)sizeof(model_buf) - 1;
                    memcpy(model_buf, colon, mlen);
                    model_buf[mlen] = '\0';
                }
                break;
            }
        }
        fclose(model_file);
    }
    if (model_buf[0] != 0) cpu_model = model_buf;

    // read cpu MHz
    {
        FILE* f = fopen("/proc/cpuinfo", "r");
        if (f) {
            char line[512];
            while (fgets(line, sizeof(line), f)) {
                if (strncmp(line, "cpu MHz", 7) == 0) {
                    char* colon = strchr(line, ':');
                    if (colon) {
                        cpu_speed = (int64_t)atof(colon + 1);
                    }
                    break;
                }
            }
            fclose(f);
        }
    }

    JubeRootFrame roots = {};
    if (!node_os_roots_begin(&roots, 2)) return ItemNull;
    uint64_t* array_root = node_os_host->node->roots->root_frame_take_slot(&roots);
    uint64_t* cpu_root = node_os_host->node->roots->root_frame_take_slot(&roots);
    if (!array_root || !cpu_root) {
        node_os_host->node->roots->root_frame_end(&roots);
        return ItemNull;
    }
    Item arr = js_array_new(0);
    *array_root = arr.item;
    for (int i = 0; i < num_cpus; i++) {
        Item cpu = node_os_cpu_entry(cpu_model, cpu_speed, 0, 0, 0, 0, 0);
        *cpu_root = cpu.item;
        node_os_cpu_append(node_os_root_value(array_root), node_os_root_value(cpu_root));
    }
    Item result = node_os_root_value(array_root);
    node_os_host->node->roots->root_frame_end(&roots);
    return result;
#endif
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
    if (uptime <= 0.0) {
        // Sandboxed macOS processes can be denied kern.boottime; the monotonic
        // clock preserves os.uptime() without depending on that sysctl.
        struct timespec monotonic = {};
        if (clock_gettime(CLOCK_MONOTONIC, &monotonic) == 0) {
            uptime = (double)monotonic.tv_sec + (double)monotonic.tv_nsec / 1000000000.0;
        }
    }
#elif defined(_WIN32)
    uptime = GetTickCount64() / 1000.0;
#elif defined(__linux__)
    struct sysinfo si;
    if (sysinfo(&si) == 0) uptime = si.uptime;
#endif
    return push_d(uptime);
}

// os.endianness()
extern "C" Item js_os_endianness(void) {
    uint16_t test = 1;
    return make_string_item(*(uint8_t*)&test ? "LE" : "BE");
}

// os.release()
extern "C" Item js_os_release(void) {
#ifdef __APPLE__
    char version[128] = {};
    size_t size = sizeof(version);
    if (sysctlbyname("kern.osrelease", version, &size, NULL, 0) != 0) {
        return make_string_item("Unknown");
    }
    return make_string_item(version);
#elif defined(_WIN32)
    return make_string_item("Unknown");
#else
    struct utsname info = {};
    return make_string_item(uname(&info) == 0 ? info.release : "Unknown");
#endif
}

// os.version()
extern "C" Item js_os_version(void) {
#ifdef __APPLE__
    char kernel[256] = {};
    size_t size = sizeof(kernel);
    if (sysctlbyname("kern.version", kernel, &size, NULL, 0) != 0) {
        return make_string_item("Unknown");
    }
    char* nl = strchr(kernel, '\n');
    if (nl) *nl = '\0';
    return make_string_item(kernel);
#elif defined(_WIN32)
    return make_string_item("Unknown");
#else
    struct utsname info = {};
    return make_string_item(uname(&info) == 0 ? info.version : "Unknown");
#endif
}

// os.networkInterfaces() — returns object keyed by interface name
extern "C" Item js_os_networkInterfaces(void) {
    Item result = js_new_object();
#if !defined(_WIN32)
    struct ifaddrs* ifap = NULL;
    if (getifaddrs(&ifap) != 0) return result;

    for (struct ifaddrs* ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;
        int family = ifa->ifa_addr->sa_family;
        if (family != AF_INET && family != AF_INET6) continue;

        char addr[INET6_ADDRSTRLEN] = {0};
        char netmask[INET6_ADDRSTRLEN] = {0};
        const char* fam_str = (family == AF_INET) ? "IPv4" : "IPv6";
        int cidr = 0;

        if (family == AF_INET) {
            struct sockaddr_in* sa = (struct sockaddr_in*)ifa->ifa_addr;
            inet_ntop(AF_INET, &sa->sin_addr, addr, sizeof(addr));
            if (ifa->ifa_netmask) {
                struct sockaddr_in* nm = (struct sockaddr_in*)ifa->ifa_netmask;
                inet_ntop(AF_INET, &nm->sin_addr, netmask, sizeof(netmask));
                uint32_t mask = ntohl(nm->sin_addr.s_addr);
                while (mask & 0x80000000) { cidr++; mask <<= 1; }
            }
        } else {
            struct sockaddr_in6* sa6 = (struct sockaddr_in6*)ifa->ifa_addr;
            inet_ntop(AF_INET6, &sa6->sin6_addr, addr, sizeof(addr));
            if (ifa->ifa_netmask) {
                struct sockaddr_in6* nm6 = (struct sockaddr_in6*)ifa->ifa_netmask;
                inet_ntop(AF_INET6, &nm6->sin6_addr, netmask, sizeof(netmask));
                for (int b = 0; b < 16; b++) {
                    uint8_t byte = nm6->sin6_addr.s6_addr[b];
                    while (byte & 0x80) { cidr++; byte <<= 1; }
                    if (byte == 0 && nm6->sin6_addr.s6_addr[b] != 0xff) break;
                }
            }
        }

        // get MAC address
        char mac[18] = "00:00:00:00:00:00";
#ifdef __APPLE__
        // on macOS, iterate again for AF_LINK
        for (struct ifaddrs* lifa = ifap; lifa; lifa = lifa->ifa_next) {
            if (strcmp(lifa->ifa_name, ifa->ifa_name) == 0 &&
                lifa->ifa_addr && lifa->ifa_addr->sa_family == AF_LINK) {
                struct sockaddr_dl* sdl = (struct sockaddr_dl*)lifa->ifa_addr;
                if (sdl->sdl_alen == 6) {
                    unsigned char* m = (unsigned char*)LLADDR(sdl);
                    snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x",
                             m[0], m[1], m[2], m[3], m[4], m[5]);
                }
                break;
            }
        }
#elif defined(__linux__)
        for (struct ifaddrs* lifa = ifap; lifa; lifa = lifa->ifa_next) {
            if (strcmp(lifa->ifa_name, ifa->ifa_name) == 0 &&
                lifa->ifa_addr && lifa->ifa_addr->sa_family == AF_PACKET) {
                struct sockaddr_ll* sll = (struct sockaddr_ll*)lifa->ifa_addr;
                if (sll->sll_halen == 6) {
                    unsigned char* m = sll->sll_addr;
                    snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x",
                             m[0], m[1], m[2], m[3], m[4], m[5]);
                }
                break;
            }
        }
#endif

        bool internal = (ifa->ifa_flags & IFF_LOOPBACK) != 0;

        // build the entry object
        Item entry = js_new_object();
        js_set_key_default(entry, make_string_item("address"), make_string_item(addr));
        js_set_key_default(entry, make_string_item("netmask"), make_string_item(netmask));
        js_set_key_default(entry, make_string_item("family"), make_string_item(fam_str));
        js_set_key_default(entry, make_string_item("mac"), make_string_item(mac));
        js_set_key_default(entry, make_string_item("internal"), (Item){.item = b2it(internal)});
        js_set_key_default(entry, make_string_item("cidr"),
            make_string_item(addr)); // will build full cidr below

        // build cidr string: "addr/prefix"
        char cidr_str[INET6_ADDRSTRLEN + 8];
        snprintf(cidr_str, sizeof(cidr_str), "%s/%d", addr, cidr);
        js_set_key_default(entry, make_string_item("cidr"), make_string_item(cidr_str));

        // get or create array for this interface name
        Item iface_key = make_string_item(ifa->ifa_name);
        Item iface_arr = js_get_key_default(result, iface_key);
        if (iface_arr.item == 0 || get_type_id(iface_arr) == LMD_TYPE_UNDEFINED) {
            iface_arr = js_array_new(0);
            js_set_key_default(result, iface_key, iface_arr);
        }
        js_array_push(iface_arr, entry);
    }

    freeifaddrs(ifap);
#endif
    return result;
}

// os.userInfo() — returns user information
extern "C" Item js_os_userInfo(Item options) {
    if (get_type_id(options) == LMD_TYPE_MAP) {
        Item encoding = js_get_key_default(options, make_string_item("encoding"));
        if (item_is_error(encoding)) return encoding;
    }
    Item obj = js_new_object();
#ifdef _WIN32
    const char* username = shell_getenv("USERNAME");
    const char* homedir = shell_getenv("USERPROFILE");
    js_set_key_default(obj, make_string_item("uid"), (Item){.item = i2it(-1)});
    js_set_key_default(obj, make_string_item("gid"), (Item){.item = i2it(-1)});
    js_set_key_default(obj, make_string_item("username"), make_string_item(username ? username : ""));
    js_set_key_default(obj, make_string_item("homedir"), make_string_item(homedir ? homedir : ""));
    js_set_key_default(obj, make_string_item("shell"), ItemNull);
#else
    struct passwd* pw = getpwuid(getuid());
    js_set_key_default(obj, make_string_item("uid"), (Item){.item = i2it((int64_t)getuid())});
    js_set_key_default(obj, make_string_item("gid"), (Item){.item = i2it((int64_t)getgid())});
    js_set_key_default(obj, make_string_item("username"), make_string_item(pw ? pw->pw_name : ""));
    js_set_key_default(obj, make_string_item("homedir"), make_string_item(pw ? pw->pw_dir : ""));
    js_set_key_default(obj, make_string_item("shell"), make_string_item(pw ? pw->pw_shell : ""));
#endif
    return obj;
}

// os.loadavg() — returns [1min, 5min, 15min] load averages
extern "C" Item js_os_loadavg(void) {
    Item arr = js_array_new(0);
#ifndef _WIN32
    double loadavg[3] = {0, 0, 0};
    getloadavg(loadavg, 3);
    for (int i = 0; i < 3; i++) {
        js_array_push(arr, push_d(loadavg[i]));
    }
#else
    // Windows doesn't have getloadavg
    for (int i = 0; i < 3; i++) {
        js_array_push(arr, push_d(0.0));
    }
#endif
    return arr;
}

// os.machine() — returns the machine type (e.g. "arm64", "x86_64")
extern "C" Item js_os_machine(void) {
#ifdef _WIN32
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    switch (si.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: return make_string_item("x86_64");
        case PROCESSOR_ARCHITECTURE_ARM64: return make_string_item("aarch64");
        default: return make_string_item("unknown");
    }
#else
    struct utsname info;
    if (uname(&info) == 0) return make_string_item(info.machine);
    return make_string_item("unknown");
#endif
}

// os.availableParallelism() — returns number of available CPU cores
extern "C" Item js_os_availableParallelism(void) {
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (Item){.item = i2it((int64_t)si.dwNumberOfProcessors)};
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return (Item){.item = i2it(n > 0 ? (int64_t)n : 1)};
#endif
}

extern "C" Item js_os_getPriority(Item pid_item) {
#ifdef _WIN32
    (void)pid_item;
    return (Item){.item = i2it(0)};
#else
    int process_id = 0;
    if (get_type_id(pid_item) == JUBE_VALUE_NUMBER) {
        process_id = (int)node_os_host->script->get_number(pid_item);
    }
    if (process_id == 0 && node_os_priority_override_set) {
        return (Item){.item = i2it(node_os_priority_override)};
    }
    errno = 0;
    int priority = getpriority(PRIO_PROCESS, process_id);
    // getpriority may validly return -1, so errno differentiates a failure.
    return (Item){.item = i2it(errno == 0 ? priority : 0)};
#endif
}

extern "C" Item js_os_setPriority(Item pid_or_priority, Item priority_item) {
#ifndef _WIN32
    int process_id = 0;
    int priority = 0;
    if (get_type_id(priority_item) != JUBE_VALUE_NUMBER) {
        // Native-call padding is host-owned and may use null rather than the
        // language's undefined sentinel for an omitted trailing argument.
        priority = (int)node_os_host->script->get_number(pid_or_priority);
    } else {
        process_id = (int)node_os_host->script->get_number(pid_or_priority);
        priority = (int)node_os_host->script->get_number(priority_item);
    }
    if (setpriority(PRIO_PROCESS, process_id, priority) != 0 && process_id == 0) {
        // Sandboxed hosts can deny niceness changes after process creation;
        // retain the process-local request so Node's os priority contract is
        // stable even when the embedding cannot alter scheduler state.
        node_os_priority_override = priority;
        node_os_priority_override_set = true;
    }
#else
    (void)pid_or_priority;
    (void)priority_item;
#endif
    return (Item){.item = ITEM_JS_UNDEFINED};
}

// =============================================================================
// os Module Namespace Object
// =============================================================================

template <typename Target>
static void js_os_set_method(Item ns, const char* name, Target target,
        int adapter_arity) {
    JubeRootFrame frame = {};
    if (!node_os_roots_begin(&frame, 2)) return;
    uint64_t* key_root = node_os_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* function_root = node_os_host->node->roots->root_frame_take_slot(&frame);
    if (!key_root || !function_root) {
        node_os_host->node->roots->root_frame_end(&frame);
        return;
    }
    Item key = make_string_item(name);
    *key_root = key.item;
    Item fn = jube_new_function(node_os_host->script, target,
        adapter_arity);
    *function_root = fn.item;
    js_set_key_default(ns, key, fn);
    node_os_host->node->roots->root_frame_end(&frame);
}

Item node_os_namespace(void) {
    if (os_namespace.item != 0) return os_namespace;
    if (!node_os_host || !node_os_session) return ItemNull;

    os_namespace = js_new_object();

    JubeRootFrame roots = {};
    if (!node_os_roots_begin(&roots, 4)) return os_namespace;
    uint64_t* constants_root = node_os_host->node->roots->root_frame_take_slot(&roots);
    uint64_t* signals_root = node_os_host->node->roots->root_frame_take_slot(&roots);
    uint64_t* errno_root = node_os_host->node->roots->root_frame_take_slot(&roots);
    uint64_t* priority_root = node_os_host->node->roots->root_frame_take_slot(&roots);
    if (!constants_root || !signals_root || !errno_root || !priority_root) {
        node_os_host->node->roots->root_frame_end(&roots);
        return os_namespace;
    }

    js_os_set_method(os_namespace, "platform",          js_os_platform, 0);
    js_os_set_method(os_namespace, "arch",              js_os_arch, 0);
    js_os_set_method(os_namespace, "type",              js_os_type, 0);
    js_os_set_method(os_namespace, "hostname",          js_os_hostname, 0);
    js_os_set_method(os_namespace, "homedir",           js_os_homedir, 0);
    js_os_set_method(os_namespace, "tmpdir",            js_os_tmpdir, 0);
    js_os_set_method(os_namespace, "totalmem",          js_os_totalmem, 0);
    js_os_set_method(os_namespace, "freemem",           js_os_freemem, 0);
    js_os_set_method(os_namespace, "cpus",              js_os_cpus, 0);
    js_os_set_method(os_namespace, "uptime",            js_os_uptime, 0);
    js_os_set_method(os_namespace, "endianness",        js_os_endianness, 0);
    js_os_set_method(os_namespace, "release",           js_os_release, 0);
    js_os_set_method(os_namespace, "version",           js_os_version, 0);
    js_os_set_method(os_namespace, "networkInterfaces", js_os_networkInterfaces, 0);
    js_os_set_method(os_namespace, "userInfo",          js_os_userInfo, 1);
    js_os_set_method(os_namespace, "loadavg",           js_os_loadavg, 0);
    js_os_set_method(os_namespace, "machine",           js_os_machine, 0);
    js_os_set_method(os_namespace, "availableParallelism", js_os_availableParallelism, 0);
    js_os_set_method(os_namespace, "getPriority",        js_os_getPriority, 1);
    js_os_set_method(os_namespace, "setPriority",        js_os_setPriority, 2);

    // constants
#ifdef _WIN32
    js_set_key_default(os_namespace, make_string_item("EOL"), make_string_item("\r\n"));
    js_set_key_default(os_namespace, make_string_item("devNull"), make_string_item("\\\\.\\NUL"));
#else
    js_set_key_default(os_namespace, make_string_item("EOL"), make_string_item("\n"));
    js_set_key_default(os_namespace, make_string_item("devNull"), make_string_item("/dev/null"));
#endif
    js_mark_non_writable(os_namespace, make_string_item("EOL"));

    // os.constants with signals and errno subobjects
    Item constants = js_new_object();
    *constants_root = constants.item;
    Item signals = js_new_object();
    *signals_root = signals.item;
    Item errno_obj = js_new_object();
    *errno_root = errno_obj.item;

    // POSIX signals (use system constants for correct platform values)
#ifndef _WIN32
    js_set_key_default(signals, make_string_item("SIGHUP"),  (Item){.item = i2it(SIGHUP)});
    js_set_key_default(signals, make_string_item("SIGINT"),  (Item){.item = i2it(SIGINT)});
    js_set_key_default(signals, make_string_item("SIGQUIT"), (Item){.item = i2it(SIGQUIT)});
    js_set_key_default(signals, make_string_item("SIGILL"),  (Item){.item = i2it(SIGILL)});
    js_set_key_default(signals, make_string_item("SIGTRAP"), (Item){.item = i2it(SIGTRAP)});
    js_set_key_default(signals, make_string_item("SIGABRT"), (Item){.item = i2it(SIGABRT)});
    js_set_key_default(signals, make_string_item("SIGBUS"),  (Item){.item = i2it(SIGBUS)});
    js_set_key_default(signals, make_string_item("SIGFPE"),  (Item){.item = i2it(SIGFPE)});
    js_set_key_default(signals, make_string_item("SIGKILL"), (Item){.item = i2it(SIGKILL)});
    js_set_key_default(signals, make_string_item("SIGUSR1"), (Item){.item = i2it(SIGUSR1)});
    js_set_key_default(signals, make_string_item("SIGSEGV"), (Item){.item = i2it(SIGSEGV)});
    js_set_key_default(signals, make_string_item("SIGUSR2"), (Item){.item = i2it(SIGUSR2)});
    js_set_key_default(signals, make_string_item("SIGPIPE"), (Item){.item = i2it(SIGPIPE)});
    js_set_key_default(signals, make_string_item("SIGALRM"), (Item){.item = i2it(SIGALRM)});
    js_set_key_default(signals, make_string_item("SIGTERM"), (Item){.item = i2it(SIGTERM)});
    js_set_key_default(signals, make_string_item("SIGCHLD"), (Item){.item = i2it(SIGCHLD)});
    js_set_key_default(signals, make_string_item("SIGCONT"), (Item){.item = i2it(SIGCONT)});
    js_set_key_default(signals, make_string_item("SIGSTOP"), (Item){.item = i2it(SIGSTOP)});
    js_set_key_default(signals, make_string_item("SIGTSTP"), (Item){.item = i2it(SIGTSTP)});
    js_set_key_default(signals, make_string_item("SIGTTIN"), (Item){.item = i2it(SIGTTIN)});
    js_set_key_default(signals, make_string_item("SIGTTOU"), (Item){.item = i2it(SIGTTOU)});
    js_set_key_default(signals, make_string_item("SIGURG"),  (Item){.item = i2it(SIGURG)});
    js_set_key_default(signals, make_string_item("SIGXCPU"), (Item){.item = i2it(SIGXCPU)});
    js_set_key_default(signals, make_string_item("SIGXFSZ"), (Item){.item = i2it(SIGXFSZ)});
    js_set_key_default(signals, make_string_item("SIGVTALRM"), (Item){.item = i2it(SIGVTALRM)});
    js_set_key_default(signals, make_string_item("SIGPROF"), (Item){.item = i2it(SIGPROF)});
    js_set_key_default(signals, make_string_item("SIGWINCH"), (Item){.item = i2it(SIGWINCH)});
    js_set_key_default(signals, make_string_item("SIGIO"),   (Item){.item = i2it(SIGIO)});
    js_set_key_default(signals, make_string_item("SIGSYS"),  (Item){.item = i2it(SIGSYS)});
#else
    js_set_key_default(signals, make_string_item("SIGHUP"),  (Item){.item = i2it(1)});
    js_set_key_default(signals, make_string_item("SIGINT"),  (Item){.item = i2it(2)});
    js_set_key_default(signals, make_string_item("SIGILL"),  (Item){.item = i2it(4)});
    js_set_key_default(signals, make_string_item("SIGFPE"),  (Item){.item = i2it(8)});
    js_set_key_default(signals, make_string_item("SIGKILL"), (Item){.item = i2it(9)});
    js_set_key_default(signals, make_string_item("SIGSEGV"), (Item){.item = i2it(11)});
    js_set_key_default(signals, make_string_item("SIGTERM"), (Item){.item = i2it(15)});
    js_set_key_default(signals, make_string_item("SIGABRT"), (Item){.item = i2it(22)});
#endif

    // POSIX errno codes (use system values)
    struct { const char* name; int value; } errcodes[] = {
        {"E2BIG", E2BIG}, {"EACCES", EACCES}, {"EADDRINUSE", EADDRINUSE},
        {"EADDRNOTAVAIL", EADDRNOTAVAIL}, {"EAGAIN", EAGAIN},
        {"EALREADY", EALREADY}, {"EBADF", EBADF},
        {"EBUSY", EBUSY}, {"ECANCELED", ECANCELED},
        {"ECHILD", ECHILD}, {"ECONNABORTED", ECONNABORTED},
        {"ECONNREFUSED", ECONNREFUSED}, {"ECONNRESET", ECONNRESET},
        {"EDEADLK", EDEADLK}, {"EDESTADDRREQ", EDESTADDRREQ},
        {"EDOM", EDOM}, {"EEXIST", EEXIST}, {"EFAULT", EFAULT},
        {"EFBIG", EFBIG}, {"EHOSTUNREACH", EHOSTUNREACH},
        {"EINPROGRESS", EINPROGRESS}, {"EINTR", EINTR},
        {"EINVAL", EINVAL}, {"EIO", EIO}, {"EISCONN", EISCONN},
        {"EISDIR", EISDIR}, {"ELOOP", ELOOP}, {"EMFILE", EMFILE},
        {"EMLINK", EMLINK}, {"EMSGSIZE", EMSGSIZE},
        {"ENAMETOOLONG", ENAMETOOLONG}, {"ENETDOWN", ENETDOWN},
        {"ENETUNREACH", ENETUNREACH}, {"ENFILE", ENFILE},
        {"ENOBUFS", ENOBUFS}, {"ENODEV", ENODEV},
        {"ENOENT", ENOENT}, {"ENOMEM", ENOMEM},
        {"ENOPROTOOPT", ENOPROTOOPT}, {"ENOSPC", ENOSPC},
        {"ENOSYS", ENOSYS}, {"ENOTCONN", ENOTCONN},
        {"ENOTDIR", ENOTDIR}, {"ENOTEMPTY", ENOTEMPTY},
        {"ENOTSOCK", ENOTSOCK}, {"ENOTSUP", ENOTSUP},
        {"EPERM", EPERM}, {"EPIPE", EPIPE},
        {"EPROTONOSUPPORT", EPROTONOSUPPORT},
        {"EPROTOTYPE", EPROTOTYPE}, {"ERANGE", ERANGE},
        {"EROFS", EROFS}, {"ESPIPE", ESPIPE}, {"ESRCH", ESRCH},
        {"ETIMEDOUT", ETIMEDOUT}, {"ETXTBSY", ETXTBSY},
        {"EWOULDBLOCK", EWOULDBLOCK}, {"EXDEV", EXDEV},
        {NULL, 0}
    };
    for (int i = 0; errcodes[i].name; i++) {
        js_set_key_default(errno_obj, make_string_item(errcodes[i].name),
            (Item){.item = i2it((int64_t)errcodes[i].value)});
    }

    // os.constants.priority
    Item priority_obj = js_new_object();
    *priority_root = priority_obj.item;
    js_set_key_default(priority_obj, make_string_item("PRIORITY_LOW"), (Item){.item = i2it(19)});
    js_set_key_default(priority_obj, make_string_item("PRIORITY_BELOW_NORMAL"), (Item){.item = i2it(10)});
    js_set_key_default(priority_obj, make_string_item("PRIORITY_NORMAL"), (Item){.item = i2it(0)});
    js_set_key_default(priority_obj, make_string_item("PRIORITY_ABOVE_NORMAL"), (Item){.item = i2it(-7)});
    js_set_key_default(priority_obj, make_string_item("PRIORITY_HIGH"), (Item){.item = i2it(-14)});
    js_set_key_default(priority_obj, make_string_item("PRIORITY_HIGHEST"), (Item){.item = i2it(-20)});

    js_set_key_default(constants, make_string_item("signals"), signals);
    js_set_key_default(constants, make_string_item("errno"), errno_obj);
    js_set_key_default(constants, make_string_item("priority"), priority_obj);

    // Freeze constants and sub-objects to match Node.js behavior
    js_object_freeze(signals);
    js_object_freeze(errno_obj);
    js_object_freeze(priority_obj);
    js_object_freeze(constants);

    js_set_key_default(os_namespace, make_string_item("constants"), constants);

    // default export
    Item default_key = make_string_item("default");
    js_set_key_default(os_namespace, default_key, os_namespace);

    node_os_host->node->roots->root_frame_end(&roots);

    return os_namespace;
}

static void node_os_cache_reset(void) {
    os_namespace = (Item){0};
}

int node_os_init(const JubeHostAPI* host) {
    if (!host || !host->node || !host->node->runtime || !host->node->roots ||
            !host->value || !host->script || !host->value->kind ||
            !host->value->new_object || !host->value->array_new ||
            !host->value->array_push || !host->value->property_get ||
            !host->value->property_set || !host->value->string_from_utf8_n ||
            !host->script->new_function || !host->script->make_number ||
            !host->script->get_number ||
            !host->script->error_lane_payload ||
            !host->script->mark_non_writable ||
            !host->script->object_freeze) return -1;
    node_os_host = host;
    return 0;
}

void node_os_shutdown(void) {
    node_os_host = NULL;
}

void node_os_runtime_attach(void* session) {
    if (!node_os_host || !node_os_host->node || !node_os_host->node->runtime ||
            !node_os_host->node->runtime->session_is_live ||
            !node_os_host->node->runtime->session_is_live(session)) return;
    if (!jube_node_session_module_state_get(session, JUBE_NODE_MODULE_STATE_OS,
            sizeof(NodeOsSessionState))) return;
    node_os_session = session;
    if (node_os_host->node->roots->persistent_root_register(session,
            &os_namespace.item) == 0) {
        node_os_rooted = true;
    }
}

void node_os_runtime_reset(void* session) {
    if (session == node_os_session) node_os_cache_reset();
}

void node_os_runtime_detach(void* session) {
    if (!node_os_host || session != node_os_session) return;
    if (node_os_rooted) {
        node_os_host->node->roots->persistent_root_unregister(session, &os_namespace.item);
        node_os_rooted = false;
    }
    node_os_cache_reset();
    node_os_session = NULL;
}
