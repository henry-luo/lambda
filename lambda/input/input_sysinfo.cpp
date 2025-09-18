#include <cstdlib>
#include <cstring>
#include <ctime>
#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>
#include <iphlpapi.h>
#else
#include <sys/utsname.h>
#include <unistd.h>
#endif
#include "input.h"
#include "input-common.h"
#include "../lambda-data.hpp"
#include "../../lib/hashmap.h"
#include "../../lib/log.h"
#include "../../lib/string.h"
#include "../../lib/datetime.h"

#ifdef __APPLE__
#include <sys/sysctl.h>
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <sys/mount.h>
#endif

// Cache entry structure for system information
typedef struct SysInfoCacheEntry {
    String* url_key;
    Input* input;
    time_t created_at;
    time_t last_accessed;
    size_t memory_size;
    struct SysInfoCacheEntry* next;
    struct SysInfoCacheEntry* prev;
} SysInfoCacheEntry;

// System information manager
typedef struct SysInfoManager {
    time_t last_update;
    int cache_ttl_seconds;
    HashMap* cached_results;
    SysInfoCacheEntry* lru_head;
    SysInfoCacheEntry* lru_tail;
    size_t current_memory_size;
    size_t max_memory_size;
    size_t max_entries;
    bool initialized;
} SysInfoManager;

// Global system information manager
static SysInfoManager* g_sysinfo_manager = nullptr;

// Initialize the system information manager
SysInfoManager* sysinfo_manager_create(void) {
    SysInfoManager* manager = (SysInfoManager*)malloc(sizeof(SysInfoManager));
    if (!manager) {
        return nullptr;
    }
    
    memset(manager, 0, sizeof(SysInfoManager));
    
    // Initialize cache
    manager->cached_results = hashmap_new(sizeof(SysInfoCacheEntry), 32, 0, 0, NULL, NULL, NULL, NULL);
    if (!manager->cached_results) {
        free(manager);
        return nullptr;
    }
    
    // Set default configuration
    manager->cache_ttl_seconds = 5;  // 5 seconds default TTL
    manager->max_memory_size = 10 * 1024 * 1024;  // 10MB
    manager->max_entries = 1000;
    manager->initialized = true;
    
    log_info("System information manager initialized successfully");
    return manager;
}

// Destroy the system information manager
void sysinfo_manager_destroy(SysInfoManager* manager) {
    if (!manager) return;
    
    if (manager->cached_results) {
        hashmap_free(manager->cached_results);
    }
    
    // Clean up LRU cache entries - simplified for Phase 1
    // Memory cleanup handled by pool destruction
    
    free(manager);
    log_info("System information manager destroyed");
}

// Get or create the global system information manager
static SysInfoManager* get_sysinfo_manager(void) {
    if (!g_sysinfo_manager) {
        g_sysinfo_manager = sysinfo_manager_create();
    }
    return g_sysinfo_manager;
}

// Get system uptime using native APIs
static double get_system_uptime() {
#ifdef __APPLE__
    struct timeval boottime;
    size_t size = sizeof(boottime);
    if (sysctlbyname("kern.boottime", &boottime, &size, NULL, 0) == 0) {
        time_t now;
        time(&now);
        return difftime(now, boottime.tv_sec);
    }
#endif
    return 0.0;
}

// Get OS version string using native APIs
static const char* get_os_version() {
#ifdef __APPLE__
    static char version_str[256] = {0};
    if (version_str[0] == 0) {
        size_t size = sizeof(version_str);
        if (sysctlbyname("kern.version", version_str, &size, NULL, 0) == 0) {
            // Extract just the version number from the full kernel version string
            char* newline = strchr(version_str, '\n');
            if (newline) *newline = '\0';
            
            // Find the version number (e.g., "Darwin Kernel Version 23.2.0")
            char* version_start = strstr(version_str, "Version ");
            if (version_start) {
                version_start += 8; // Skip "Version "
                char* version_end = strchr(version_start, ':');
                if (version_end) *version_end = '\0';
                memmove(version_str, version_start, strlen(version_start) + 1);
            }
        } else {
            strcpy(version_str, "Unknown");
        }
    }
    return version_str;
#else
    return "Unknown";
#endif
}

// Create system information element using native APIs
static Element* create_system_info_element(SysInfoManager* manager, Input* input) {
    if (!manager || !input) {
        log_error("Invalid system information manager or input");
        return nullptr;
    }
    
    // Get system information using platform-specific APIs
#ifdef _WIN32
    // Windows system information
    OSVERSIONINFOW osvi;
    ZeroMemory(&osvi, sizeof(OSVERSIONINFOW));
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOW);
    
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    
    // For Windows, we'll use basic info since uname is not available
    const char* sysname = "Windows";
    char release[64];
    snprintf(release, sizeof(release), "%lu.%lu", osvi.dwMajorVersion, osvi.dwMinorVersion);
    
    const char* machine;
    switch (sysinfo.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64:
            machine = "x86_64";
            break;
        case PROCESSOR_ARCHITECTURE_INTEL:
            machine = "i386";
            break;
        case PROCESSOR_ARCHITECTURE_ARM64:
            machine = "arm64";
            break;
        default:
            machine = "unknown";
            break;
    }
    
    char nodename[256];
    DWORD nodename_size = sizeof(nodename);
    if (!GetComputerNameA(nodename, &nodename_size)) {
        strcpy(nodename, "unknown");
    }
#else
    // Unix/Linux system information using uname
    struct utsname uname_info;
    if (uname(&uname_info) != 0) {
        log_error("Failed to get system information via uname");
        return nullptr;
    }
    
    const char* sysname = uname_info.sysname;
    const char* release = uname_info.release;
    const char* machine = uname_info.machine;
    const char* nodename = uname_info.nodename;
#endif
    
    // Create system element
    Element* system_elem = input_create_element(input, "system");
    if (!system_elem) {
        log_error("Failed to create system element");
        return nullptr;
    }
    
    // Add timestamp
    time_t current_time = time(nullptr);
    if (current_time != -1) {
        char timestamp_str[32];
        snprintf(timestamp_str, sizeof(timestamp_str), "%ld", current_time);
        input_add_attribute_to_element(input, system_elem, "timestamp", timestamp_str);
    }
    
    // Add OS information
    Element* os_elem = input_create_element(input, "os");
    if (os_elem) {
        input_add_attribute_to_element(input, os_elem, "name", sysname);
        input_add_attribute_to_element(input, os_elem, "version", get_os_version());
        input_add_attribute_to_element(input, os_elem, "kernel", release);
        input_add_attribute_to_element(input, os_elem, "machine", machine);
        input_add_attribute_to_element(input, os_elem, "nodename", nodename);
        input_add_attribute_item_to_element(input, system_elem, "os", {.item = (uint64_t)os_elem});
    }
    
    // Add hostname information
    char hostname[256];
#ifdef _WIN32
    DWORD hostname_size = sizeof(hostname);
    if (GetComputerNameA(hostname, &hostname_size)) {
#else
    if (gethostname(hostname, sizeof(hostname)) == 0) {
#endif
        Element* hostname_elem = input_create_element(input, "hostname");
        if (hostname_elem) {
            input_add_attribute_to_element(input, hostname_elem, "value", hostname);
            input_add_attribute_item_to_element(input, system_elem, "hostname", {.item = (uint64_t)hostname_elem});
        }
    }
    
    // Add uptime information
    double uptime_seconds = get_system_uptime();
    if (uptime_seconds > 0) {
        Element* uptime_elem = input_create_element(input, "uptime");
        if (uptime_elem) {
            char uptime_str[32];
            snprintf(uptime_str, sizeof(uptime_str), "%.2f", uptime_seconds);
            input_add_attribute_to_element(input, uptime_elem, "seconds", uptime_str);
            
            // Convert to human-readable format
            int days = (int)(uptime_seconds / 86400);
            int hours = (int)((uptime_seconds - days * 86400) / 3600);
            int minutes = (int)((uptime_seconds - days * 86400 - hours * 3600) / 60);
            
            char days_str[16], hours_str[16], minutes_str[16];
            snprintf(days_str, sizeof(days_str), "%d", days);
            snprintf(hours_str, sizeof(hours_str), "%d", hours);
            snprintf(minutes_str, sizeof(minutes_str), "%d", minutes);
            
            input_add_attribute_to_element(input, uptime_elem, "days", days_str);
            input_add_attribute_to_element(input, uptime_elem, "hours", hours_str);
            input_add_attribute_to_element(input, uptime_elem, "minutes", minutes_str);
            input_add_attribute_item_to_element(input, system_elem, "uptime", {.item = (uint64_t)uptime_elem});
        }
    }
    
    // Add architecture information
    Element* arch_elem = input_create_element(input, "architecture");
    if (arch_elem) {
        input_add_attribute_to_element(input, arch_elem, "value", machine);
        input_add_attribute_item_to_element(input, system_elem, "architecture", {.item = (uint64_t)arch_elem});
    }
    
    // Add platform information
    Element* platform_elem = input_create_element(input, "platform");
    if (platform_elem) {
#ifdef __APPLE__
        input_add_attribute_to_element(input, platform_elem, "value", "darwin");
#elif defined(__linux__)
        input_add_attribute_to_element(input, platform_elem, "value", "linux");
#elif defined(_WIN32)
        input_add_attribute_to_element(input, platform_elem, "value", "windows");
#else
        input_add_attribute_to_element(input, platform_elem, "value", "unknown");
#endif
        input_add_attribute_item_to_element(input, system_elem, "platform", {.item = (uint64_t)platform_elem});
    }
    
    log_info("Created system information element successfully");
    return system_elem;
}

// Parse sys:// URL and extract components
static bool parse_sys_url(const char* url, char** category, char** subcategory, char** item) {
    if (!url) {
        return false;
    }
    
    const char* path = url;
    // For pathname like "/system/info", skip the leading slash
    if (path && path[0] == '/') {
        path = path + 1;
    } else if (path && strncmp(path, "sys://", 6) == 0) {
        path = path + 6;  // Skip "sys://" scheme
    }
    
    // For path like "system/info", extract category and subcategory
    const char* slash1 = strchr(path, '/');
    if (!slash1) {
        // Only category provided
        *category = strdup(path);
        *subcategory = nullptr;
        *item = nullptr;
        return true;
    }
    
    // Extract category (before first slash)
    size_t cat_len = slash1 - path;
    *category = (char*)malloc(cat_len + 1);
    strncpy(*category, path, cat_len);
    (*category)[cat_len] = '\0';
    
    // Find second slash for item
    const char* slash2 = strchr(slash1 + 1, '/');
    if (!slash2) {
        // Category and subcategory provided (e.g., "system/info")
        *subcategory = strdup(slash1 + 1);
        *item = nullptr;
        return true;
    }
    
    // Extract subcategory (between first and second slash)
    size_t subcat_len = slash2 - (slash1 + 1);
    *subcategory = (char*)malloc(subcat_len + 1);
    strncpy(*subcategory, slash1 + 1, subcat_len);
    (*subcategory)[subcat_len] = '\0';
    
    // Extract item (after second slash)
    *item = strdup(slash2 + 1);
    
    return true;
}

// Main entry point for sys:// URLs
Input* input_from_sysinfo(Url* url, VariableMemPool* pool) {
    if (!url || !pool) {
        log_error("Invalid parameters for system information input");
        return nullptr;
    }
    
    SysInfoManager* manager = get_sysinfo_manager();
    if (!manager) {
        log_error("Failed to get system information manager");
        return nullptr;
    }
    
    // Parse URL components
    char* category = nullptr;
    char* subcategory = nullptr; 
    char* item = nullptr;
    
    // Parse URL components from host and pathname
    
    // Check if pathname is null
    if (!url->pathname || !url->pathname->chars) {
        log_error("URL pathname is null or empty");
        return nullptr;
    }
    
    // For sys:// URLs, reconstruct the full path from host + pathname
    // sys://system/info -> host="system", pathname="/info" -> full_path="system/info"
    char* full_path = nullptr;
    if (url->host && url->pathname) {
        size_t host_len = strlen(url->host->chars);
        // Skip leading slash in pathname
        const char* path_part = url->pathname->chars[0] == '/' ? url->pathname->chars + 1 : url->pathname->chars;
        size_t path_part_len = strlen(path_part);
        
        full_path = (char*)malloc(host_len + 1 + path_part_len + 1);
        strcpy(full_path, url->host->chars);
        strcat(full_path, "/");
        strcat(full_path, path_part);
        
    } else {
        full_path = strdup(url->pathname->chars);
    }
    
    if (!parse_sys_url(full_path, &category, &subcategory, &item)) {
        log_error("Failed to parse sys:// URL: %s", full_path);
        free(full_path);
        return nullptr;
    }
    
    free(full_path);
    
    // Debug logging
    log_info("Parsed sys:// URL - category: %s, subcategory: %s, item: %s", 
             category ? category : "null", 
             subcategory ? subcategory : "null", 
             item ? item : "null");
    
    // Currently only support system/info
    if (strcmp(category, "system") != 0 || strcmp(subcategory, "info") != 0) {
        log_error("Unsupported sys:// URL: %s/%s", category, subcategory);
        free(category);
        free(subcategory);
        free(item);
        return nullptr;
    }
    
    // Create Input object using the standard function
    Input* input = input_new(url);
    if (!input) {
        log_error("Failed to create Input object");
        free(category);
        free(subcategory);
        free(item);
        return nullptr;
    }
    
    // Set the memory pool for sys:// specific allocations
    input->pool = pool;
    
    // Create system information element
    Element* system_elem = create_system_info_element(manager, input);
    if (!system_elem) {
        log_error("Failed to create system information element");
        free(category);
        free(subcategory);
        free(item);
        return nullptr;
    }
    
    input->root = {.item = (uint64_t)system_elem};
    
    // Cleanup
    free(category);
    free(subcategory);
    free(item);
    
    log_info("Successfully created sys:// input for %s", url->pathname->chars);
    return input;
}

// Check if URL is a sys:// scheme
bool is_sys_url(const char* url) {
    return url && strncmp(url, "sys://", 6) == 0;
}
