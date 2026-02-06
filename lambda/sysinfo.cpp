/**
 * sysinfo.cpp - System information provider for sys.* paths
 *
 * Implements cross-platform system information access through Lambda's
 * lazy path resolution mechanism.
 *
 * Architecture:
 * - sys.* paths resolve through cascading lazy loading
 * - Uses Input/MarkBuilder for proper Lambda data structure creation
 * - Caches results with TTL for performance
 */

#include "sysinfo.h"
#include "lambda-data.hpp"
#include "input/input.hpp"
#include "mark_builder.hpp"
#include "../lib/log.h"
#include "../lib/strbuf.h"

#include <cstdlib>
#include <cstring>
#include <ctime>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/utsname.h>
#include <unistd.h>
#include <pwd.h>
#endif

#ifdef __APPLE__
#include <sys/sysctl.h>
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <sys/mount.h>
#endif

#ifdef __linux__
#include <sys/sysinfo.h>
#endif

// Thread-local eval context (defined in runner.cpp)
extern __thread EvalContext* context;

// External functions
extern "C" Pool* eval_context_get_pool(EvalContext* ctx);

// Helper to convert ConstItem to Item (same memory layout)
static inline Item to_item(ConstItem ci) {
    return *(Item*)&ci;
}

// ============================================================================
// Cache structure and globals
// ============================================================================

struct SysinfoCache {
    Input* input;           // dedicated Input for sysinfo data
    Item root;              // cached root map {os, cpu, memory, ...}
    Item os_info;           // cached sys.os map
    Item cpu_info;          // cached sys.cpu map
    Item memory_info;       // cached sys.memory map
    Item proc_info;         // cached sys.proc map
    Item time_info;         // cached sys.time map
    Item lambda_info;       // cached sys.lambda map
    time_t root_time;
    time_t os_time;
    time_t cpu_time;
    time_t memory_time;
    time_t proc_time;
    time_t time_time;
    time_t lambda_time;
    bool initialized;
};

// Cache TTLs (in seconds)
static const int TTL_STATIC = 3600;   // 1 hour - static info (OS, CPU)
static const int TTL_MEMORY = 1;      // 1 second - dynamic
static const int TTL_PROC = 5;        // 5 seconds - semi-static
static const int TTL_TIME = 0;        // always fresh

static __thread SysinfoCache* g_cache = nullptr;

// Global storage for command line arguments (set once at startup)
static int g_argc = 0;
static char** g_argv = nullptr;

// ============================================================================
// Forward declarations
// ============================================================================

static bool cache_valid(time_t cached_at, int ttl);
static int collect_path_segments(Path* path, const char** segments, int max_segments);
static Item resolve_root(void);
static Item resolve_os(void);
static Item resolve_cpu(void);
static Item resolve_memory(void);
static Item resolve_proc(const char** segments, int count);
static Item resolve_time(void);
static Item resolve_lambda(void);
static Item resolve_home(void);
static Item resolve_temp(void);

// ============================================================================
// Initialization
// ============================================================================

extern "C" void sysinfo_set_args(int argc, char** argv) {
    g_argc = argc;
    g_argv = argv;
}

extern "C" void sysinfo_init(void) {
    if (g_cache && g_cache->initialized) return;
    
    if (!g_cache) {
        g_cache = (SysinfoCache*)calloc(1, sizeof(SysinfoCache));
    }
    if (g_cache) {
        // Create a dedicated Input for sysinfo using eval context's pool
        if (context) {
            Pool* pool = eval_context_get_pool(context);
            if (pool) {
                g_cache->input = Input::create(pool, nullptr, nullptr);
                log_info("sysinfo_init: created input %p", g_cache->input);
            }
        }
        g_cache->initialized = true;
        log_info("sysinfo_init: initialized");
    }
}

extern "C" void sysinfo_shutdown(void) {
    if (g_cache) {
        // Input will be freed when pool is destroyed
        free(g_cache);
        g_cache = nullptr;
        log_info("sysinfo_shutdown: complete");
    }
}

extern "C" void sysinfo_invalidate_cache(void) {
    if (g_cache) {
        g_cache->root = ItemNull;
        g_cache->os_info = ItemNull;
        g_cache->cpu_info = ItemNull;
        g_cache->memory_info = ItemNull;
        g_cache->proc_info = ItemNull;
        g_cache->time_info = ItemNull;
        g_cache->lambda_info = ItemNull;
        g_cache->root_time = 0;
        g_cache->os_time = 0;
        g_cache->cpu_time = 0;
        g_cache->memory_time = 0;
        g_cache->proc_time = 0;
        g_cache->time_time = 0;
        g_cache->lambda_time = 0;
    }
}

// ============================================================================
// Helpers
// ============================================================================

static bool cache_valid(time_t cached_at, int ttl) {
    if (ttl == 0) return false;  // always refresh
    if (cached_at == 0) return false;  // never cached
    time_t now = time(nullptr);
    return (now - cached_at) < ttl;
}

/**
 * Collect path segments from leaf to root.
 * Returns count of segments (excluding "sys" root).
 * segments[0] = first segment after "sys", etc.
 */
static int collect_path_segments(Path* path, const char** segments, int max_segments) {
    int count = 0;
    
    // Collect segments in reverse order (leaf to root)
    const char* temp[32];
    int temp_count = 0;
    
    Path* p = path;
    while (p && temp_count < 32) {
        const char* name = p->name;
        if (name && name[0] != '\0') {
            // Skip the "sys" root segment
            if (strcmp(name, "sys") != 0) {
                temp[temp_count++] = name;
            }
        }
        p = p->parent;
    }
    
    // Reverse to get root-to-leaf order
    for (int i = temp_count - 1; i >= 0 && count < max_segments; i--) {
        segments[count++] = temp[i];
    }
    
    return count;
}

/**
 * Get Input for sysinfo data creation.
 * Initializes cache if needed.
 */
static Input* get_input(void) {
    if (!g_cache || !g_cache->initialized) {
        sysinfo_init();
    }
    if (!g_cache) return nullptr;
    
    // Re-create Input if context changed
    if (!g_cache->input && context) {
        Pool* pool = eval_context_get_pool(context);
        if (pool) {
            g_cache->input = Input::create(pool, nullptr, nullptr);
        }
    }
    
    return g_cache->input;
}

// ============================================================================
// Main resolver
// ============================================================================

extern "C" Item sysinfo_resolve_path(Path* path) {
    if (!path) return ItemNull;
    
    log_debug("sysinfo_resolve_path: resolving path %p", path);
    
    // Collect path segments
    const char* segments[16];
    int seg_count = collect_path_segments(path, segments, 16);
    
    if (seg_count == 0) {
        // Just "sys" - return root map
        return resolve_root();
    }
    
    // First segment determines category
    const char* category = segments[0];
    
    if (strcmp(category, "os") == 0) {
        if (seg_count == 1) return resolve_os();
        // sys.os.* - get field from os map
        Item os = resolve_os();
        if (get_type_id(os) == LMD_TYPE_MAP) {
            ConstItem ci = os.map->get(segments[1]);
            return to_item(ci);
        }
    }
    else if (strcmp(category, "cpu") == 0) {
        if (seg_count == 1) return resolve_cpu();
        Item cpu = resolve_cpu();
        if (get_type_id(cpu) == LMD_TYPE_MAP) {
            ConstItem ci = cpu.map->get(segments[1]);
            return to_item(ci);
        }
    }
    else if (strcmp(category, "memory") == 0) {
        if (seg_count == 1) return resolve_memory();
        Item memory = resolve_memory();
        if (get_type_id(memory) == LMD_TYPE_MAP) {
            ConstItem ci = memory.map->get(segments[1]);
            return to_item(ci);
        }
    }
    else if (strcmp(category, "proc") == 0) {
        return resolve_proc(segments + 1, seg_count - 1);
    }
    else if (strcmp(category, "time") == 0) {
        if (seg_count == 1) return resolve_time();
        Item tm = resolve_time();
        if (get_type_id(tm) == LMD_TYPE_MAP) {
            ConstItem ci = tm.map->get(segments[1]);
            return to_item(ci);
        }
    }
    else if (strcmp(category, "lambda") == 0) {
        if (seg_count == 1) return resolve_lambda();
        Item lambda = resolve_lambda();
        if (get_type_id(lambda) == LMD_TYPE_MAP) {
            ConstItem ci = lambda.map->get(segments[1]);
            return to_item(ci);
        }
    }
    else if (strcmp(category, "home") == 0) {
        return resolve_home();
    }
    else if (strcmp(category, "temp") == 0) {
        return resolve_temp();
    }
    
    log_warn("sysinfo_resolve_path: unknown category '%s'", category);
    return ItemNull;
}

// ============================================================================
// Platform-specific helpers
// ============================================================================

static const char* get_os_name(void) {
#ifdef __APPLE__
    return "Darwin";
#elif defined(__linux__)
    return "Linux";
#elif defined(_WIN32)
    return "Windows";
#else
    return "Unknown";
#endif
}

static const char* get_os_version(void) {
#ifdef __APPLE__
    static char version[128] = {0};
    if (version[0] == 0) {
        size_t size = sizeof(version);
        if (sysctlbyname("kern.osrelease", version, &size, NULL, 0) != 0) {
            strcpy(version, "Unknown");
        }
    }
    return version;
#elif defined(_WIN32)
    return "Unknown";  // TODO: implement Windows version detection
#else
    static struct utsname info;
    static bool initialized = false;
    if (!initialized) {
        if (uname(&info) == 0) initialized = true;
    }
    return initialized ? info.release : "Unknown";
#endif
}

static const char* get_kernel_version(void) {
#ifdef __APPLE__
    static char kernel[256] = {0};
    if (kernel[0] == 0) {
        size_t size = sizeof(kernel);
        if (sysctlbyname("kern.version", kernel, &size, NULL, 0) != 0) {
            strcpy(kernel, "Unknown");
        }
        // Trim newline
        char* nl = strchr(kernel, '\n');
        if (nl) *nl = '\0';
    }
    return kernel;
#elif defined(_WIN32)
    return "Unknown";
#else
    static struct utsname info;
    static bool initialized = false;
    if (!initialized) {
        if (uname(&info) == 0) initialized = true;
    }
    return initialized ? info.version : "Unknown";
#endif
}

static const char* get_machine_arch(void) {
#ifdef __APPLE__
    static char machine[64] = {0};
    if (machine[0] == 0) {
        size_t size = sizeof(machine);
        if (sysctlbyname("hw.machine", machine, &size, NULL, 0) != 0) {
            strcpy(machine, "Unknown");
        }
    }
    return machine;
#elif defined(_WIN32)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    switch (si.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: return "x86_64";
        case PROCESSOR_ARCHITECTURE_ARM64: return "arm64";
        case PROCESSOR_ARCHITECTURE_INTEL: return "i386";
        default: return "Unknown";
    }
#else
    static struct utsname info;
    static bool initialized = false;
    if (!initialized) {
        if (uname(&info) == 0) initialized = true;
    }
    return initialized ? info.machine : "Unknown";
#endif
}

static const char* get_hostname(void) {
    static char hostname[256] = {0};
    if (hostname[0] == 0) {
#ifdef _WIN32
        DWORD size = sizeof(hostname);
        if (!GetComputerNameA(hostname, &size)) {
            strcpy(hostname, "Unknown");
        }
#else
        if (gethostname(hostname, sizeof(hostname)) != 0) {
            strcpy(hostname, "Unknown");
        }
#endif
    }
    return hostname;
}

static int get_cpu_cores(void) {
#ifdef __APPLE__
    int cores = 0;
    size_t size = sizeof(cores);
    if (sysctlbyname("hw.physicalcpu", &cores, &size, NULL, 0) == 0) {
        return cores;
    }
    return 1;
#elif defined(_WIN32)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwNumberOfProcessors;
#else
    return sysconf(_SC_NPROCESSORS_ONLN);
#endif
}

static int get_cpu_threads(void) {
#ifdef __APPLE__
    int threads = 0;
    size_t size = sizeof(threads);
    if (sysctlbyname("hw.logicalcpu", &threads, &size, NULL, 0) == 0) {
        return threads;
    }
    return 1;
#elif defined(_WIN32)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwNumberOfProcessors;
#else
    return sysconf(_SC_NPROCESSORS_ONLN);
#endif
}

static int64_t get_memory_total(void) {
#ifdef __APPLE__
    int64_t mem = 0;
    size_t size = sizeof(mem);
    if (sysctlbyname("hw.memsize", &mem, &size, NULL, 0) == 0) {
        return mem;
    }
    return 0;
#elif defined(_WIN32)
    MEMORYSTATUSEX mem;
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) {
        return mem.ullTotalPhys;
    }
    return 0;
#else
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        return (int64_t)si.totalram * si.mem_unit;
    }
    return 0;
#endif
}

static int64_t get_memory_free(void) {
#ifdef __APPLE__
    vm_statistics64_data_t vm_stat;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64, 
                          (host_info64_t)&vm_stat, &count) == KERN_SUCCESS) {
        int64_t page_size = 0;
        size_t size = sizeof(page_size);
        sysctlbyname("hw.pagesize", &page_size, &size, NULL, 0);
        return vm_stat.free_count * page_size;
    }
    return 0;
#elif defined(_WIN32)
    MEMORYSTATUSEX mem;
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) {
        return mem.ullAvailPhys;
    }
    return 0;
#else
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        return (int64_t)si.freeram * si.mem_unit;
    }
    return 0;
#endif
}

static double get_system_uptime(void) {
#ifdef __APPLE__
    struct timeval boottime;
    size_t size = sizeof(boottime);
    if (sysctlbyname("kern.boottime", &boottime, &size, NULL, 0) == 0) {
        time_t now = time(NULL);
        return difftime(now, boottime.tv_sec);
    }
    return 0.0;
#elif defined(_WIN32)
    return GetTickCount64() / 1000.0;
#else
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        return si.uptime;
    }
    return 0.0;
#endif
}

static const char* get_home_dir(void) {
#ifdef _WIN32
    static char home[MAX_PATH] = {0};
    if (home[0] == 0) {
        const char* userprofile = getenv("USERPROFILE");
        if (userprofile) {
            strncpy(home, userprofile, sizeof(home) - 1);
        }
    }
    return home;
#else
    const char* home = getenv("HOME");
    if (home) return home;
    
    struct passwd* pw = getpwuid(getuid());
    if (pw) return pw->pw_dir;
    
    return "/";
#endif
}

static const char* get_temp_dir(void) {
#ifdef _WIN32
    static char temp[MAX_PATH] = {0};
    if (temp[0] == 0) {
        GetTempPathA(sizeof(temp), temp);
    }
    return temp;
#else
    const char* temp = getenv("TMPDIR");
    if (temp) return temp;
    return "/tmp";
#endif
}

// ============================================================================
// Category resolvers using MarkBuilder
// ============================================================================

static Item resolve_root(void) {
    if (!g_cache) sysinfo_init();
    if (!g_cache) return ItemNull;
    
    // Root map is always valid (sub-maps have their own TTLs)
    if (get_type_id(g_cache->root) == LMD_TYPE_MAP) {
        return g_cache->root;
    }
    
    Input* input = get_input();
    if (!input) return ItemNull;
    
    MarkBuilder builder(input);
    MapBuilder root = builder.map();
    
    // Add category maps - resolve them directly for now
    root.put("os", resolve_os());
    root.put("cpu", resolve_cpu());
    root.put("memory", resolve_memory());
    root.put("proc", resolve_proc(nullptr, 0));
    root.put("time", resolve_time());
    root.put("lambda", resolve_lambda());
    root.put("home", resolve_home());
    root.put("temp", resolve_temp());
    
    g_cache->root = root.final();
    g_cache->root_time = time(nullptr);
    
    return g_cache->root;
}

static Item resolve_os(void) {
    if (!g_cache) sysinfo_init();
    if (!g_cache) return ItemNull;
    
    if (cache_valid(g_cache->os_time, TTL_STATIC) && 
        get_type_id(g_cache->os_info) == LMD_TYPE_MAP) {
        return g_cache->os_info;
    }
    
    Input* input = get_input();
    if (!input) return ItemNull;
    
    MarkBuilder builder(input);
    MapBuilder os = builder.map();
    
    os.put("name", get_os_name());
    os.put("version", get_os_version());
    os.put("kernel", get_kernel_version());
    os.put("machine", get_machine_arch());
    os.put("hostname", get_hostname());
    
#ifdef __APPLE__
    os.put("platform", "darwin");
#elif defined(__linux__)
    os.put("platform", "linux");
#elif defined(_WIN32)
    os.put("platform", "windows");
#else
    os.put("platform", "unknown");
#endif
    
    g_cache->os_info = os.final();
    g_cache->os_time = time(nullptr);
    
    log_debug("sysinfo_resolve_os: resolved os map");
    return g_cache->os_info;
}

static Item resolve_cpu(void) {
    if (!g_cache) sysinfo_init();
    if (!g_cache) return ItemNull;
    
    if (cache_valid(g_cache->cpu_time, TTL_STATIC) && 
        get_type_id(g_cache->cpu_info) == LMD_TYPE_MAP) {
        return g_cache->cpu_info;
    }
    
    Input* input = get_input();
    if (!input) return ItemNull;
    
    MarkBuilder builder(input);
    MapBuilder cpu = builder.map();
    
    cpu.put("cores", (int64_t)get_cpu_cores());
    cpu.put("threads", (int64_t)get_cpu_threads());
    cpu.put("arch", get_machine_arch());
    
    g_cache->cpu_info = cpu.final();
    g_cache->cpu_time = time(nullptr);
    
    log_debug("sysinfo_resolve_cpu: resolved cpu map");
    return g_cache->cpu_info;
}

static Item resolve_memory(void) {
    if (!g_cache) sysinfo_init();
    if (!g_cache) return ItemNull;
    
    if (cache_valid(g_cache->memory_time, TTL_MEMORY) && 
        get_type_id(g_cache->memory_info) == LMD_TYPE_MAP) {
        return g_cache->memory_info;
    }
    
    Input* input = get_input();
    if (!input) return ItemNull;
    
    MarkBuilder builder(input);
    MapBuilder mem = builder.map();
    
    int64_t total = get_memory_total();
    int64_t free_mem = get_memory_free();
    int64_t used = total - free_mem;
    
    mem.put("total", total);
    mem.put("free", free_mem);
    mem.put("used", used);
    
    g_cache->memory_info = mem.final();
    g_cache->memory_time = time(nullptr);
    
    log_debug("sysinfo_resolve_memory: resolved memory map");
    return g_cache->memory_info;
}

static Item resolve_proc(const char** segments, int count) {
    if (!g_cache) sysinfo_init();
    if (!g_cache) return ItemNull;
    
    Input* input = get_input();
    if (!input) return ItemNull;
    
    // sys.proc - return map with "self" sub-path
    if (count == 0) {
        if (cache_valid(g_cache->proc_time, TTL_PROC) && 
            get_type_id(g_cache->proc_info) == LMD_TYPE_MAP) {
            return g_cache->proc_info;
        }
        
        MarkBuilder builder(input);
        MapBuilder proc = builder.map();
        
        // Add self info directly for now
        MapBuilder self = builder.map();
        self.put("pid", (int64_t)getpid());
        
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd))) {
            self.put("cwd", cwd);
        }
        
        proc.put("self", self.final());
        
        g_cache->proc_info = proc.final();
        g_cache->proc_time = time(nullptr);
        return g_cache->proc_info;
    }
    
    // sys.proc.self
    if (count >= 1 && strcmp(segments[0], "self") == 0) {
        if (count == 1) {
            // Return self map
            MarkBuilder builder(input);
            MapBuilder self = builder.map();
            self.put("pid", (int64_t)getpid());
            
            char cwd[1024];
            if (getcwd(cwd, sizeof(cwd))) {
                self.put("cwd", cwd);
            }
            
            return self.final();
        }
        
        // sys.proc.self.pid, sys.proc.self.cwd, sys.proc.self.env
        if (count >= 2) {
            const char* field = segments[1];
            
            if (strcmp(field, "pid") == 0) {
                return Item{.item = i2it(getpid())};
            }
            
            if (strcmp(field, "cwd") == 0) {
                char cwd[1024];
                if (getcwd(cwd, sizeof(cwd))) {
                    MarkBuilder builder(input);
                    return builder.createStringItem(cwd);
                }
                return ItemNull;
            }
            
            if (strcmp(field, "args") == 0) {
                // sys.proc.self.args - return command line arguments as array
                MarkBuilder builder(input);
                ArrayBuilder args = builder.array();
                
                if (g_argv) {
                    for (int i = 0; i < g_argc; i++) {
                        args.append(builder.createStringItem(g_argv[i]));
                    }
                }
                
                return args.final();
            }
            
            if (strcmp(field, "env") == 0) {
                // sys.proc.self.env or sys.proc.self.env.VARNAME
                if (count == 2) {
                    // Return all env vars as a map
                    MarkBuilder builder(input);
                    MapBuilder env = builder.map();
                    
                    extern char** environ;
                    for (char** e = environ; *e; e++) {
                        char* eq = strchr(*e, '=');
                        if (eq) {
                            size_t name_len = eq - *e;
                            char name[256];
                            if (name_len < sizeof(name)) {
                                strncpy(name, *e, name_len);
                                name[name_len] = '\0';
                                env.put(name, eq + 1);
                            }
                        }
                    }
                    
                    return env.final();
                }
                else if (count >= 3) {
                    // sys.proc.self.env.VARNAME
                    const char* var = getenv(segments[2]);
                    if (var) {
                        MarkBuilder builder(input);
                        return builder.createStringItem(var);
                    }
                    return ItemNull;
                }
            }
        }
    }
    
    return ItemNull;
}

static Item resolve_time(void) {
    // Time info is always fresh (TTL_TIME = 0)
    Input* input = get_input();
    if (!input) return ItemNull;
    
    MarkBuilder builder(input);
    MapBuilder tm = builder.map();
    
    time_t now = time(nullptr);
    tm.put("now", (int64_t)now);
    
    double uptime = get_system_uptime();
    tm.put("uptime", uptime);
    
    log_debug("sysinfo_resolve_time: resolved time map");
    return tm.final();
}

static Item resolve_lambda(void) {
    if (!g_cache) sysinfo_init();
    if (!g_cache) return ItemNull;
    
    if (cache_valid(g_cache->lambda_time, TTL_STATIC) && 
        get_type_id(g_cache->lambda_info) == LMD_TYPE_MAP) {
        return g_cache->lambda_info;
    }
    
    Input* input = get_input();
    if (!input) return ItemNull;
    
    MarkBuilder builder(input);
    MapBuilder lambda = builder.map();
    
    lambda.put("version", "0.1.0");  // TODO: get from config
    
    g_cache->lambda_info = lambda.final();
    g_cache->lambda_time = time(nullptr);
    
    log_debug("sysinfo_resolve_lambda: resolved lambda map");
    return g_cache->lambda_info;
}

static Item resolve_home(void) {
    Input* input = get_input();
    if (!input) return ItemNull;
    
    const char* home = get_home_dir();
    if (!home || home[0] == '\0') return ItemNull;
    
    // Return as string path for now
    // TODO: Return as actual Path object
    MarkBuilder builder(input);
    return builder.createStringItem(home);
}

static Item resolve_temp(void) {
    Input* input = get_input();
    if (!input) return ItemNull;
    
    const char* temp = get_temp_dir();
    if (!temp || temp[0] == '\0') return ItemNull;
    
    // Return as string path for now
    MarkBuilder builder(input);
    return builder.createStringItem(temp);
}
