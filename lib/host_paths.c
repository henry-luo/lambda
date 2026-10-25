// host_paths.c — host-supplied base directories (host_paths.h, MB13)
//
// The bases and the memoized host_path_dir strings live in static storage, not
// the heap: they are process-lifetime, and their first use may come from any
// thread, including one where memtrack is off, so a heap copy freed under a
// different tracking state would corrupt the allocator. Static storage also
// leaves nothing for live-allocation accounting at exit.

#include "host_paths.h"
#include "file.h"
#include "log.h"
#include "memtrack.h"
#include "shell.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define HOST_PATH_MAX 1024

static const char* const host_path_defaults[HOST_PATH_COUNT] = {
    "./lmd",   // HOME
    "./temp",  // CACHE
    "./temp",  // TEMP
    ".",       // LOG
    ".",       // DOCUMENTS
};

static const char* const host_path_env[HOST_PATH_COUNT] = {
    "LAMBDA_HOME", "LAMBDA_CACHE_DIR", "LAMBDA_TEMP_DIR", "LAMBDA_LOG_DIR",
    "LAMBDA_DOCUMENTS_DIR",
};

// empty string = the default
static char host_path_values[HOST_PATH_COUNT][HOST_PATH_MAX];

// memoized host_path_dir results; a changed base retires its kind's entries
typedef struct HostPathDirEntry {
    HostPathKind kind;
    bool live;
    char relative[128];
    char joined[HOST_PATH_MAX];
} HostPathDirEntry;
#define HOST_PATH_DIR_CAPACITY 32
static HostPathDirEntry host_path_dirs[HOST_PATH_DIR_CAPACITY];
static pthread_mutex_t host_path_dir_lock = PTHREAD_MUTEX_INITIALIZER;

static bool host_path_kind_valid(HostPathKind kind) {
    return kind >= HOST_PATH_HOME && kind < HOST_PATH_COUNT;
}

bool host_paths_set(HostPathKind kind, const char* dir) {
    if (!host_path_kind_valid(kind)) return false;
    size_t length = dir ? strlen(dir) : 0;
    // a root "/" keeps its only slash
    while (length > 1 && dir[length - 1] == '/') length--;
    if (length >= HOST_PATH_MAX) {
        log_error("host_paths: base directory too long (%zu bytes)", length);
        return false;
    }
    pthread_mutex_lock(&host_path_dir_lock);
    for (int i = 0; i < HOST_PATH_DIR_CAPACITY; i++) {
        if (host_path_dirs[i].live && host_path_dirs[i].kind == kind) host_path_dirs[i].live = false;
    }
    if (length) memcpy(host_path_values[kind], dir, length);
    host_path_values[kind][length] = '\0';
    pthread_mutex_unlock(&host_path_dir_lock);
    return true;
}

void host_paths_init_from_env(void) {
#ifndef LAMBDA_NO_AMBIENT_PROVIDERS
    // the browser profile has no ambient environment; its host sets paths directly
    for (int kind = 0; kind < HOST_PATH_COUNT; kind++) {
        const char* value = shell_getenv(host_path_env[kind]);
        if (value && value[0]) host_paths_set((HostPathKind)kind, value);
    }
#endif
}

const char* host_path(HostPathKind kind) {
    if (!host_path_kind_valid(kind)) return ".";
    return host_path_values[kind][0] ? host_path_values[kind] : host_path_defaults[kind];
}

char* host_path_join(HostPathKind kind, const char* relative) {
    const char* base = host_path(kind);
    if (!relative || !relative[0]) return mem_strdup(base, MEM_CAT_SYSTEM);
    size_t base_length = strlen(base);
    bool slash = base_length > 0 && base[base_length - 1] == '/';
    return mem_join3(base, base_length, slash ? "" : "/", slash ? 0 : 1,
                     relative, strlen(relative), MEM_CAT_SYSTEM);
}

const char* host_path_dir(HostPathKind kind, const char* relative) {
    if (!host_path_kind_valid(kind)) return ".";
    const char* rel = relative ? relative : "";
    const char* result = NULL;
    pthread_mutex_lock(&host_path_dir_lock);
    HostPathDirEntry* free_slot = NULL;
    for (int i = 0; i < HOST_PATH_DIR_CAPACITY && !result; i++) {
        HostPathDirEntry* entry = &host_path_dirs[i];
        if (!entry->live) { if (!free_slot) free_slot = entry; continue; }
        if (entry->kind == kind && strcmp(entry->relative, rel) == 0) result = entry->joined;
    }
    if (!result && free_slot && strlen(rel) < sizeof(free_slot->relative)) {
        const char* base = host_path(kind);
        size_t base_length = strlen(base);
        bool slash = !rel[0] || (base_length > 0 && base[base_length - 1] == '/');
        int written = snprintf(free_slot->joined, sizeof(free_slot->joined), "%s%s%s",
                               base, slash ? "" : "/", rel);
        if (written > 0 && (size_t)written < sizeof(free_slot->joined)) {
            strcpy(free_slot->relative, rel);
            free_slot->kind = kind;
            free_slot->live = true;
            result = free_slot->joined;
        }
    }
    pthread_mutex_unlock(&host_path_dir_lock);
    // a full table or an overlong path is a programming error: callers name a fixed set
    if (!result) log_error("host_paths: no stable directory for '%s'", rel);
    return result ? result : host_path(kind);
}

bool host_path_ensure(HostPathKind kind) {
#ifdef LAMBDA_NO_FILE_IO
    (void)kind;
    return false;  // no filesystem in this profile
#else
    return create_dir(host_path(kind));
#endif
}
