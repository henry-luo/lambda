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
#include "../lambda-data.hpp"
#include "lambda/input/input.hpp"
#include "lambda/io/mark_builder.hpp"
#include "context_capsule.h"
#include "transpiler.hpp"
#include "io_resource.hpp"
#include "doc_context.hpp"
#include "lib/log.h"
#include "lib/mem_factory.h"
#include "lib/strbuf.h"
#include "lib/str.h"
#include "lib/file.h"
#include "lib/shell.h"
#include "lib/url.h"

#include <cstdlib>
#include <cstring>
#include <ctime>

#ifdef _WIN32
#include <windows.h>
#include <process.h>  // for _getpid
#define getpid _getpid
#else
#include <sys/utsname.h>
#include <unistd.h>    // for getpid, getuid
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
extern "C" Pool* eval_context_get_pool(void);

// Helper to convert ConstItem to Item (same memory layout)
static inline Item to_item(ConstItem ci) {
    return *(Item*)&ci;
}

// ============================================================================
// Cache structure and globals
// ============================================================================

struct SysinfoCache {
    Input* input;           // dedicated Input for sysinfo data
    Item os_info;           // cached sys.os map
    Item cpu_info;          // cached sys.cpu map
    Item memory_info;       // cached sys.memory map
    Item proc_info;         // cached sys.proc map
    Item lambda_info;       // cached sys.lambda map
    time_t os_time;
    time_t cpu_time;
    time_t memory_time;
    time_t proc_time;
    time_t lambda_time;
};

// Cache TTLs (in seconds)
static const int TTL_STATIC = 3600;   // 1 hour - static info (OS, CPU)
static const int TTL_MEMORY = 1;      // 1 second - dynamic
static const int TTL_PROC = 5;        // 5 seconds - semi-static

static void sysinfo_cache_destroy(void* capsule);

static void* sysinfo_cache_construct(EvalContext* owner) {
    if (!owner || context != owner) return nullptr;
    Pool* pool = eval_context_get_pool();
    if (!pool) return nullptr;
    SysinfoCache* cache = (SysinfoCache*)pool_calloc(pool, sizeof(SysinfoCache));
    if (!cache) return nullptr;
    cache->input = Input::create(pool, nullptr, nullptr);
    if (!cache->input || !cache->input->arena || !cache->input->name_pool ||
            !cache->input->type_list) {
        sysinfo_cache_destroy(cache);
        return nullptr;
    }
    return cache;
}

static void sysinfo_cache_destroy(void* capsule) {
    SysinfoCache* cache = (SysinfoCache*)capsule;
    if (!cache || !cache->input) return;
    // D5.2.5: release the Input's whole context; its pool cleanup is idempotent.
    input_release_document_resources(cache->input);
    cache->input = nullptr;
}

static const ContextCapsuleOps sysinfo_cache_ops = {
    "sysinfo", CONTEXT_CAPSULE_LIFETIME_REALM, 0,
    sysinfo_cache_construct, nullptr, sysinfo_cache_destroy
};

static SysinfoCache* sysinfo_cache(bool create) {
    if (!context) return nullptr;
    return (SysinfoCache*)(create
        ? context_capsule_ensure(context, CONTEXT_CAPSULE_SYSINFO, &sysinfo_cache_ops)
        : context_capsule(context, CONTEXT_CAPSULE_SYSINFO));
}

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
static Item resolve_user(const char** segments, int count);

// ============================================================================
// Initialization
// ============================================================================

extern "C" void sysinfo_set_argv(int argc, char** argv) {
    g_argc = argc;
    g_argv = argv;
}

extern "C" int sysinfo_get_argc(void) { return g_argc; }
extern "C" char** sysinfo_get_argv(void) { return g_argv; }

extern "C" void sysinfo_init(void) {
    sysinfo_cache(true);
}

extern "C" void sysinfo_shutdown(void) {
    if (context) context_capsule_drop(context, CONTEXT_CAPSULE_SYSINFO);
}

extern "C" void sysinfo_invalidate_cache(void) {
    SysinfoCache* cache = sysinfo_cache(false);
    if (cache) {
        cache->os_info = ItemNull;
        cache->cpu_info = ItemNull;
        cache->memory_info = ItemNull;
        cache->proc_info = ItemNull;
        cache->lambda_info = ItemNull;
        cache->os_time = 0;
        cache->cpu_time = 0;
        cache->memory_time = 0;
        cache->proc_time = 0;
        cache->lambda_time = 0;
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
    SysinfoCache* cache = sysinfo_cache(true);
    return cache ? cache->input : nullptr;
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
    else if (strcmp(category, "user") == 0) {
        return resolve_user(segments + 1, seg_count - 1);
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
            str_copy(version, sizeof(version), "Unknown", 7);
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
            str_copy(kernel, sizeof(kernel), "Unknown", 7);
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
            str_copy(machine, sizeof(machine), "Unknown", 7);
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
            str_copy(hostname, sizeof(hostname), "Unknown", 7);
        }
#else
        if (gethostname(hostname, sizeof(hostname)) != 0) {
            str_copy(hostname, sizeof(hostname), "Unknown", 7);
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
    // Each evaluation snapshots the current profile, not the first process value.
    const char* userprofile = shell_getenv("USERPROFILE");
    return userprofile ? userprofile : "";
#else
    const char* home = shell_getenv("HOME");
    if (home) return home;

    struct passwd* pw = getpwuid(getuid());
    if (pw) return pw->pw_dir;

    return "/";
#endif
}

static const char* get_temp_dir(void) {
#ifdef _WIN32
    // Keep the Windows API buffer thread-local and refresh it for each snapshot.
    static thread_local char temp[4096];
    DWORD length = GetTempPathA(sizeof(temp), temp);
    return length && length < sizeof(temp) ? temp : nullptr;
#else
    const char* temp = shell_getenv("TMPDIR");
    if (temp) return temp;
    return "/tmp";  // TMP_PATH_OK: sysinfo reports the OS-standard tmpdir
#endif
}

extern "C" void lambda_context_capture_directories(void) {
    if (!context || context->context_directories_initialized) return;
    const char* home = get_home_dir();
    const char* temporary = get_temp_dir();
    if (home && strlen(home) < sizeof(context->home_directory))
        str_copy(context->home_directory, sizeof(context->home_directory), home, strlen(home));
    if (temporary && strlen(temporary) < sizeof(context->temporary_directory))
        str_copy(context->temporary_directory, sizeof(context->temporary_directory), temporary, strlen(temporary));
    context->context_directories_initialized = true;
}

static bool context_path_separator(char ch, bool windows) {
    return ch == '/' || (windows && ch == '\\');
}

static Path* context_append_components(Pool* pool, Path* path,
        const char* value, bool windows) {
    while (path && *value) {
        while (context_path_separator(*value, windows)) value++;
        const char* start = value;
        while (*value && !context_path_separator(*value, windows)) value++;
        size_t length = value - start;
        if (!length || (length == 1 && start[0] == '.')) continue;
        if (length == 2 && start[0] == '.' && start[1] == '.')
            path = path_select_parent(pool, path);
        else
            path = path_extend_len(pool, path, start, length);
    }
    return path;
}

// A UNC server is a file authority, never a local directory component.
extern "C" Path* lambda_context_windows_unc_path(Pool* pool, const char* source) {
    if (!pool || !source || !context_path_separator(source[0], true) ||
            !source[1] || !context_path_separator(source[1], true)) return nullptr;
    const char* host = source + 2;
    const char* end = host;
    while (*end && !context_path_separator(*end, true)) end++;
    if (end == host || !*end || (end - host == 1 && (*host == '?' || *host == '.')))
        return nullptr;
    char* hostname = pool_dup_n(pool, host, end - host);
    if (!hostname) return nullptr;
    char* current = nullptr;
    if (str_ieq_cstr(hostname, "localhost")) {
        // The `localhost` provider is local, so a UNC share needs the host name.
        current = shell_get_hostname();
        if (!current || !*current || strcmp(current, "localhost") == 0) {
            mem_free(current);
            return nullptr;
        }
        hostname = current;
    }
    Path* root = path_new_authority(pool, PATH_SCHEME_FILE, hostname);
    mem_free(current);
    if (!root) return nullptr;
    return context_append_components(pool, root, end, true);
}

// S2.4.8 captures directory addresses without probing their targets.
extern "C" Path* lambda_context_path(Pool* pool, int kind, const char* source) {
    if (kind == LAMBDA_CONTEXT_PATH_DOCUMENT) {
        Document* doc = doc_context_get_supplied();
        return doc ? doc->path : nullptr;
    }
    lambda_context_capture_directories();
    char cwd[4096];
    const char* value = kind == LAMBDA_CONTEXT_PATH_SCRIPT ? source :
        kind == LAMBDA_CONTEXT_PATH_HOME ? (context ? context->home_directory : get_home_dir()) :
        kind == LAMBDA_CONTEXT_PATH_TEMP ? (context ? context->temporary_directory : get_temp_dir()) :
        context && context->working_directory[0] ? context->working_directory :
        file_getcwd_into(cwd, sizeof(cwd)) ? cwd : nullptr;
    if (!pool || !value || !*value) return nullptr;
    Path* path = nullptr;
    char* url_path = nullptr;
    if (kind == LAMBDA_CONTEXT_PATH_SCRIPT && strncmp(value, "file:", 5) == 0) {
        // A source URL carries an authority and encoded path, not native components.
        Url* url = url_parse(value);
        if (!url || !url->is_valid || url->scheme != URL_SCHEME_FILE) {
            if (url) url_destroy(url);
            return nullptr;
        }
        const char* host = url_get_hostname(url);
        bool named = host && *host && strcmp(host, "localhost") != 0;
        path = path_new_authority(pool, PATH_SCHEME_FILE, named ? host : "localhost");
        if (named) {
            const char* pathname = url_get_pathname(url);
            size_t length = 0;
            if (pathname) url_path = url_decode_component(pathname, strlen(pathname), &length);
            if (url_path && memchr(url_path, '\0', length)) {
                mem_free(url_path);
                url_path = nullptr;
            }
        } else {
            url_path = url_to_local_path(url);
        }
        url_destroy(url);
        if (!path || !url_path) { mem_free(url_path); return nullptr; }
        value = url_path;
    }
    if (!path) path = path_new_authority(pool, PATH_SCHEME_FILE, "localhost");
#ifdef _WIN32
    if (!url_path && context_path_separator(value[0], true) && value[1] &&
            context_path_separator(value[1], true))
        return lambda_context_windows_unc_path(pool, value);
    bool windows = true;
    bool absolute = context_path_separator(value[0], windows) || (value[0] && value[1] == ':');
#else
    bool windows = false;
    bool absolute = context_path_separator(value[0], windows);
#endif
    if (!absolute) {
        path = lambda_context_path(pool, LAMBDA_CONTEXT_PATH_CWD, nullptr);
        if (!path) { mem_free(url_path); return nullptr; }
    }
#ifdef _WIN32
    if (value[0] && value[1] == ':') {
        path = path_extend_len(pool, path, value, 1);
        value += 2;
    }
#endif
    path = context_append_components(pool, path, value, windows);
    mem_free(url_path);
    return path;
}

static Item user_field_at(void* data, int64_t index) {
    (void)data;
    const char* field = index == 0 ? "name" : index == 1 ? "home" : nullptr;
    return field ? resolve_user(&field, 1) : ItemNull;
}

static Item user_key_at(void* data, int64_t index) {
    (void)data;
    const char* name = index == 0 ? "name" : index == 1 ? "home" : nullptr;
    return name ? (Item){.item = y2it(name_key_symbol(name, strlen(name)))} : ItemNull;
}

static Item user_get(void* data, Item key) {
    (void)data;
    if (!is_text_type_id(get_type_id(key))) return ItemNull;
    const char* name = key.get_chars();
    size_t length = key.get_len();
    if (!name) return ItemNull;
    if (length == 4 && memcmp(name, "name", 4) == 0) return user_field_at(data, 0);
    if (length == 4 && memcmp(name, "home", 4) == 0) return user_field_at(data, 1);
    return ItemNull;
}

static int64_t user_count(void* data) { (void)data; return 2; }

static SymbolKeyList* user_keys(void* data) {
    (void)data;
    SymbolKeyList* keys = symbol_key_list_new(2);
    if (!keys) return nullptr;
    symbol_key_list_append(keys, name_key_symbol("name", 4));
    symbol_key_list_append(keys, name_key_symbol("home", 4));
    return keys;
}

static VMapVtable user_vtable = {user_get, io_resource_set, user_count, user_keys,
    user_key_at, user_field_at, io_resource_destroy, io_resource_trace};

static Item resolve_user(const char** segments, int count) {
    if (!count) {
        SysinfoCache* cache = sysinfo_cache(true);
        if (!cache) return ItemNull;
        VMap* user = io_resource_handle_new(&user_vtable, nullptr, cache);
        if (!user) return ItemError;
        // COW snapshots the virtual fields before any Lambda mutation.
        user->cow_state |= COW_STATE_SHARED;
        return (Item){.vmap = user};
    }
    if (strcmp(segments[0], "name") == 0 && count == 1) {
        Input* input = get_input();
        if (!input) return ItemNull;
#ifdef _WIN32
        char name[256];
        DWORD size = sizeof(name);
        if (!GetUserNameA(name, &size)) return ItemNull;
#else
        struct passwd* account = getpwuid(getuid());
        if (!account || !account->pw_name) return ItemNull;
        const char* name = account->pw_name;
#endif
        MarkBuilder builder(input);
        return builder.createStringItem(name);
    }
    if (strcmp(segments[0], "home") == 0) {
        Pool* pool = eval_context_get_pool();
        Path* path = lambda_context_path(pool, LAMBDA_CONTEXT_PATH_HOME, nullptr);
        for (int i = 1; path && i < count; i++) path = path_extend(pool, path, segments[i]);
        return path ? fn_force((Item){.path = path}) : ItemNull;
    }
    return ItemNull;
}

// ============================================================================
// Category resolvers using MarkBuilder
// ============================================================================

// Builder calls copy OS and process source strings into the sysinfo Input.
static Item create_cwd_item(MarkBuilder& builder) {
    char cwd[4096];
    return file_getcwd_into(cwd, sizeof(cwd))
        ? builder.createStringItem(cwd) : ItemNull;
}

static Item resolve_root(void) {
    SysinfoCache* cache = sysinfo_cache(true);
    if (!cache) return ItemNull;

    Input* input = get_input();
    if (!input) return ItemNull;

    MarkBuilder builder(input);
    MapBuilder root = builder.map();

    // Rebuild the root so time and TTL-based child snapshots stay current.
    root.put("os", resolve_os());
    root.put("cpu", resolve_cpu());
    root.put("memory", resolve_memory());
    root.put("proc", resolve_proc(nullptr, 0));
    root.put("time", resolve_time());
    root.put("lambda", resolve_lambda());
    root.put("home", resolve_home());
    root.put("temp", resolve_temp());

    return root.final();
}

static Item resolve_os(void) {
    SysinfoCache* cache = sysinfo_cache(true);
    if (!cache) return ItemNull;

    if (cache_valid(cache->os_time, TTL_STATIC) &&
        get_type_id(cache->os_info) == LMD_TYPE_MAP) {
        return cache->os_info;
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

    cache->os_info = os.final();
    cache->os_time = time(nullptr);

    log_debug("sysinfo_resolve_os: resolved os map");
    return cache->os_info;
}

static Item resolve_cpu(void) {
    SysinfoCache* cache = sysinfo_cache(true);
    if (!cache) return ItemNull;

    if (cache_valid(cache->cpu_time, TTL_STATIC) &&
        get_type_id(cache->cpu_info) == LMD_TYPE_MAP) {
        return cache->cpu_info;
    }

    Input* input = get_input();
    if (!input) return ItemNull;

    MarkBuilder builder(input);
    MapBuilder cpu = builder.map();

    cpu.put("cores", (int64_t)get_cpu_cores());
    cpu.put("threads", (int64_t)get_cpu_threads());
    cpu.put("arch", get_machine_arch());

    cache->cpu_info = cpu.final();
    cache->cpu_time = time(nullptr);

    log_debug("sysinfo_resolve_cpu: resolved cpu map");
    return cache->cpu_info;
}

static Item resolve_memory(void) {
    SysinfoCache* cache = sysinfo_cache(true);
    if (!cache) return ItemNull;

    if (cache_valid(cache->memory_time, TTL_MEMORY) &&
        get_type_id(cache->memory_info) == LMD_TYPE_MAP) {
        return cache->memory_info;
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

    cache->memory_info = mem.final();
    cache->memory_time = time(nullptr);

    log_debug("sysinfo_resolve_memory: resolved memory map");
    return cache->memory_info;
}

static char** process_environment(void) {
#ifdef _WIN32
    return _environ;
#else
    extern char** environ;
    return environ;
#endif
}

// The environment is a Lambda map, so direct path reads use the same key equality.
static const char* process_environment_value(const char* name) {
    if (!name || !*name) return nullptr;
    size_t length = strlen(name);
    const char* value = nullptr;
    // Map construction replaces earlier entries when a name occurs twice.
    for (char** entry = process_environment(); entry && *entry; entry++) {
        if (strncmp(*entry, name, length) == 0 && (*entry)[length] == '=')
            value = *entry + length + 1;
    }
    return value;
}

static Item resolve_proc(const char** segments, int count) {
    SysinfoCache* cache = sysinfo_cache(true);
    if (!cache) return ItemNull;

    Input* input = get_input();
    if (!input) return ItemNull;

    // sys.proc - return map with "self" sub-path
    if (count == 0) {
        if (cache_valid(cache->proc_time, TTL_PROC) &&
            get_type_id(cache->proc_info) == LMD_TYPE_MAP) {
            return cache->proc_info;
        }

        MarkBuilder builder(input);
        MapBuilder proc = builder.map();

        const char* self_path[] = {"self"};
        proc.put("self", resolve_proc(self_path, 1));

        cache->proc_info = proc.final();
        cache->proc_time = time(nullptr);
        return cache->proc_info;
    }

    // sys.proc.self
    if (count >= 1 && strcmp(segments[0], "self") == 0) {
        if (count == 1) {
            // Return self map
            MarkBuilder builder(input);
            MapBuilder self = builder.map();
            self.put("pid", (int64_t)getpid());

            Item cwd = create_cwd_item(builder);
            if (get_type_id(cwd) == LMD_TYPE_STRING) self.put("cwd", cwd);

            // A bound process object must expose the same fields as direct paths.
            const char* argv_path[] = {"self", "argv"};
            const char* env_path[] = {"self", "env"};
            self.put("argv", resolve_proc(argv_path, 2));
            self.put("env", resolve_proc(env_path, 2));

            return self.final();
        }

        // sys.proc.self.pid, sys.proc.self.cwd, sys.proc.self.env
        if (count >= 2) {
            const char* field = segments[1];

            if (strcmp(field, "pid") == 0) {
                MarkBuilder builder(input);
                return builder.createInt(getpid());
            }

            if (strcmp(field, "cwd") == 0) {
                MarkBuilder builder(input);
                return create_cwd_item(builder);
            }

            if (strcmp(field, "argv") == 0) {
                // expose the compacted process argument vector under its public name.
                MarkBuilder builder(input);
                ArrayBuilder argv = builder.array();

                if (g_argv) {
                    for (int i = 0; i < g_argc; i++) {
                        argv.append(builder.createStringItem(g_argv[i]));
                    }
                }

                return argv.final();
            }

            if (strcmp(field, "env") == 0) {
                // sys.proc.self.env or sys.proc.self.env.VARNAME
                if (count == 2) {
                    // Return all env vars as a map
                    MarkBuilder builder(input);
                    MapBuilder env = builder.map();

                    for (char** e = process_environment(); e && *e; e++) {
                        char* eq = strchr(*e, '=');
                        if (eq && eq != *e) {
                            // Pool the complete key so bound and direct reads agree.
                            String* name = builder.createName(*e, eq - *e);
                            env.put(name, builder.createStringItem(eq + 1));
                        }
                    }

                    return env.final();
                }
                else if (count >= 3) {
                    // sys.proc.self.env.VARNAME
                    const char* var = process_environment_value(segments[2]);
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
    SysinfoCache* cache = sysinfo_cache(true);
    if (!cache) return ItemNull;

    if (cache_valid(cache->lambda_time, TTL_STATIC) &&
        get_type_id(cache->lambda_info) == LMD_TYPE_MAP) {
        return cache->lambda_info;
    }

    Input* input = get_input();
    if (!input) return ItemNull;

    MarkBuilder builder(input);
    MapBuilder lambda = builder.map();

    lambda.put("version", "0.1.0");  // TODO: get from config
    // Keep package resources aligned with the import resolver's asset root.
    lambda.put("home", host_path(HOST_PATH_HOME));

    cache->lambda_info = lambda.final();
    cache->lambda_time = time(nullptr);

    log_debug("sysinfo_resolve_lambda: resolved lambda map");
    return cache->lambda_info;
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
