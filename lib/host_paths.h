#pragma once
// host_paths.h — the base directories a host supplies to Lambda and Radiant
// (vibe/Lambda_Design_Mobile.md MB13). Runtime code names a kind and a path
// relative to it instead of assuming the working directory, so an embedding
// host whose working directory is meaningless (iOS) can place the read-only
// package tree, caches, scratch files, logs and user documents itself.
//
// Defaults reproduce the desktop layout relative to the working directory;
// LAMBDA_HOME, LAMBDA_CACHE_DIR, LAMBDA_TEMP_DIR, LAMBDA_LOG_DIR and
// LAMBDA_DOCUMENTS_DIR override them, and an embedding host calls
// host_paths_set() before runtime start-up.

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum HostPathKind {
    HOST_PATH_HOME,       // read-only package tree (lmd/): "./lmd"
    HOST_PATH_CACHE,      // purgeable caches: "./temp"
    HOST_PATH_TEMP,       // scratch files and developer dumps: "./temp"
    HOST_PATH_LOG,        // log.conf and relative log outputs: "."
    HOST_PATH_DOCUMENTS,  // user documents: "."
    HOST_PATH_COUNT
} HostPathKind;

// Replaces one base directory (copied; a trailing '/' is dropped). NULL or ""
// restores the default. Call before runtime start-up; not thread-safe.
bool host_paths_set(HostPathKind kind, const char* dir);
// Applies the LAMBDA_* environment overrides above; LAMBDA_HOME also sets HOME.
void host_paths_init_from_env(void);
// The base directory; never NULL.
const char* host_path(HostPathKind kind);
// "<base>/<relative>", mem_alloc'd (MEM_CAT_SYSTEM); the caller mem_frees it.
char* host_path_join(HostPathKind kind, const char* relative);
// "<base>/<relative>" owned by host_paths: stable until that base changes, for
// long-lived directory strings such as cache roots. Thread-safe.
const char* host_path_dir(HostPathKind kind, const char* relative);
// Creates the base directory (and its parents) if missing.
bool host_path_ensure(HostPathKind kind);

#ifdef __cplusplus
}
#endif
