#ifndef NPM_LOCKFILE_H
#define NPM_LOCKFILE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Lock file entry for a resolved package
// ---------------------------------------------------------------------------

typedef struct {
    char* name;         // package name
    char* version;      // resolved version
    char* resolved;     // tarball URL
    char* integrity;    // integrity hash
    // dependencies: parallel arrays (name → version)
    char** dep_names;
    char** dep_versions;
    int    dep_count;
} NpmLockEntry;

// ---------------------------------------------------------------------------
// Lock file (lambda-node.lock)
// ---------------------------------------------------------------------------

typedef struct {
    int version;            // lock file format version (currently 1)
    NpmLockEntry* entries;
    int entry_count;
    int entry_cap;
} NpmLockFile;

// Create an empty lock file.
NpmLockFile* npm_lockfile_create(void);

// Read a lock file from disk. Returns NULL if file doesn't exist or is invalid.
NpmLockFile* npm_lockfile_read(const char* path);

// Write the lock file to disk. Returns 0 on success, -1 on error.
int npm_lockfile_write(const NpmLockFile* lockfile, const char* path);

// Add an entry to the lock file. Takes ownership of the entry data.
void npm_lockfile_add(NpmLockFile* lockfile, const char* key,
                      const char* version, const char* resolved,
                      const char* integrity,
                      const char** dep_names, const char** dep_versions,
                      int dep_count);

// Look up a package in the lock file. Returns NULL if not found.
// The returned pointer is valid as long as the lockfile exists.
const NpmLockEntry* npm_lockfile_lookup(const NpmLockFile* lockfile, const char* key);

// Free a lock file.
void npm_lockfile_free(NpmLockFile* lockfile);

#ifdef __cplusplus
}
#endif

#endif // NPM_LOCKFILE_H
