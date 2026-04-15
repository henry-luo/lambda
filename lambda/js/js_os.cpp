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
    const char* cpu_model = "Unknown";
    int64_t cpu_speed = 0; // MHz

#ifdef __APPLE__
    size_t size = sizeof(num_cpus);
    sysctlbyname("hw.logicalcpu", &num_cpus, &size, NULL, 0);

    static char brand[256] = {0};
    if (brand[0] == 0) {
        size = sizeof(brand);
        sysctlbyname("machdep.cpu.brand_string", brand, &size, NULL, 0);
    }
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

    Item arr = js_array_new(0);
    for (int i = 0; i < num_cpus; i++) {
        Item cpu = js_new_object();
        js_property_set(cpu, make_string_item("model"), make_string_item(cpu_model));
        js_property_set(cpu, make_string_item("speed"), (Item){.item = i2it(cpu_speed)});
        Item times = js_new_object();
        if (cpu_info && i < (int)cpu_count) {
            processor_cpu_load_info_data_t* load =
                (processor_cpu_load_info_data_t*)cpu_info + i;
            int64_t user_t = (int64_t)load->cpu_ticks[CPU_STATE_USER] * 10;
            int64_t nice_t = (int64_t)load->cpu_ticks[CPU_STATE_NICE] * 10;
            int64_t sys_t  = (int64_t)load->cpu_ticks[CPU_STATE_SYSTEM] * 10;
            int64_t idle_t = (int64_t)load->cpu_ticks[CPU_STATE_IDLE] * 10;
            js_property_set(times, make_string_item("user"), (Item){.item = i2it(user_t)});
            js_property_set(times, make_string_item("nice"), (Item){.item = i2it(nice_t)});
            js_property_set(times, make_string_item("sys"),  (Item){.item = i2it(sys_t)});
            js_property_set(times, make_string_item("idle"), (Item){.item = i2it(idle_t)});
            js_property_set(times, make_string_item("irq"),  (Item){.item = i2it(0)});
        } else {
            js_property_set(times, make_string_item("user"), (Item){.item = i2it(0)});
            js_property_set(times, make_string_item("nice"), (Item){.item = i2it(0)});
            js_property_set(times, make_string_item("sys"),  (Item){.item = i2it(0)});
            js_property_set(times, make_string_item("idle"), (Item){.item = i2it(0)});
            js_property_set(times, make_string_item("irq"),  (Item){.item = i2it(0)});
        }
        js_property_set(cpu, make_string_item("times"), times);
        js_array_push(arr, cpu);
    }
    if (cpu_info) {
        vm_deallocate(mach_task_self(), (vm_address_t)cpu_info,
                      info_count * sizeof(natural_t));
    }
    return arr;

#elif defined(_WIN32)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    num_cpus = si.dwNumberOfProcessors;

    Item arr = js_array_new(0);
    for (int i = 0; i < num_cpus; i++) {
        Item cpu = js_new_object();
        js_property_set(cpu, make_string_item("model"), make_string_item("CPU"));
        js_property_set(cpu, make_string_item("speed"), (Item){.item = i2it(0)});
        Item times = js_new_object();
        js_property_set(times, make_string_item("user"), (Item){.item = i2it(0)});
        js_property_set(times, make_string_item("nice"), (Item){.item = i2it(0)});
        js_property_set(times, make_string_item("sys"),  (Item){.item = i2it(0)});
        js_property_set(times, make_string_item("idle"), (Item){.item = i2it(0)});
        js_property_set(times, make_string_item("irq"),  (Item){.item = i2it(0)});
        js_property_set(cpu, make_string_item("times"), times);
        js_array_push(arr, cpu);
    }
    return arr;

#else
    // Linux
    num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (num_cpus < 1) num_cpus = 1;

    // read CPU model from /proc/cpuinfo
    static char model_buf[256] = {0};
    if (model_buf[0] == 0) {
        FILE* f = fopen("/proc/cpuinfo", "r");
        if (f) {
            char line[512];
            while (fgets(line, sizeof(line), f)) {
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
            fclose(f);
        }
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

    Item arr = js_array_new(0);
    for (int i = 0; i < num_cpus; i++) {
        Item cpu = js_new_object();
        js_property_set(cpu, make_string_item("model"), make_string_item(cpu_model));
        js_property_set(cpu, make_string_item("speed"), (Item){.item = i2it(cpu_speed)});
        Item times = js_new_object();
        js_property_set(times, make_string_item("user"), (Item){.item = i2it(0)});
        js_property_set(times, make_string_item("nice"), (Item){.item = i2it(0)});
        js_property_set(times, make_string_item("sys"),  (Item){.item = i2it(0)});
        js_property_set(times, make_string_item("idle"), (Item){.item = i2it(0)});
        js_property_set(times, make_string_item("irq"),  (Item){.item = i2it(0)});
        js_property_set(cpu, make_string_item("times"), times);
        js_array_push(arr, cpu);
    }
    return arr;
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
        js_property_set(entry, make_string_item("address"), make_string_item(addr));
        js_property_set(entry, make_string_item("netmask"), make_string_item(netmask));
        js_property_set(entry, make_string_item("family"), make_string_item(fam_str));
        js_property_set(entry, make_string_item("mac"), make_string_item(mac));
        js_property_set(entry, make_string_item("internal"), (Item){.item = b2it(internal)});
        js_property_set(entry, make_string_item("cidr"),
            make_string_item(addr)); // will build full cidr below

        // build cidr string: "addr/prefix"
        char cidr_str[INET6_ADDRSTRLEN + 8];
        snprintf(cidr_str, sizeof(cidr_str), "%s/%d", addr, cidr);
        js_property_set(entry, make_string_item("cidr"), make_string_item(cidr_str));

        // get or create array for this interface name
        Item iface_key = make_string_item(ifa->ifa_name);
        Item iface_arr = js_property_get(result, iface_key);
        if (iface_arr.item == 0 || get_type_id(iface_arr) == LMD_TYPE_UNDEFINED) {
            iface_arr = js_array_new(0);
            js_property_set(result, iface_key, iface_arr);
        }
        js_array_push(iface_arr, entry);
    }

    freeifaddrs(ifap);
#endif
    return result;
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
