#ifndef FILE_UTILS_H
#define FILE_UTILS_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct _ArrayList;

// ---------------------------------------------------------------------------
// Directory entry returned by dir_list
// ---------------------------------------------------------------------------

typedef struct {
    char* name;             // entry name (not full path) — caller must free
    bool is_dir;
    bool is_symlink;
} DirEntry;

// Free a single DirEntry (name string + struct itself).
void dir_entry_free(DirEntry* entry);

// ---------------------------------------------------------------------------
// Callback for recursive directory walk.
// `path` is the full path relative to the walk root.
// Return false to skip the subtree (for directories) or abort.
// ---------------------------------------------------------------------------
struct FileStat; // forward declare — defined in file.h
typedef bool (*FileWalkCallback)(const char* path, bool is_dir, void* user_data);

// ---------------------------------------------------------------------------
// Existing
// ---------------------------------------------------------------------------

/**
 * Create directory recursively (like mkdir -p).
 * Creates all parent directories as needed.
 * 
 * @param path Directory path to create
 * @return 0 on success, -1 on error
 */
int create_dir_recursive(const char* path);

// ---------------------------------------------------------------------------
// Directory operations
// ---------------------------------------------------------------------------

// List immediate children. Returns ArrayList of DirEntry*.
// Caller must free each DirEntry via dir_entry_free, then arraylist_free.
// Returns NULL on error.
struct _ArrayList* dir_list(const char* dir_path);

// Recursive depth-first walk with callback. Returns 0 on success, -1 on error.
int dir_walk(const char* dir_path, FileWalkCallback cb, void* user_data);

// Recursive delete (rm -rf). Returns 0 on success, -1 on error.
int dir_delete(const char* dir_path);

// Recursive directory copy. Returns 0 on success, -1 on error.
struct FileCopyOptions; // forward declare — defined in file.h
int dir_copy(const char* src, const char* dst);

// ---------------------------------------------------------------------------
// Glob & search
// ---------------------------------------------------------------------------

// Expand glob pattern to list of paths. Returns ArrayList of char*.
// Caller must free each string, then arraylist_free. NULL on error.
struct _ArrayList* file_glob(const char* pattern);

// Find files by name pattern under directory.
// Returns ArrayList of char* (full paths). Caller frees strings + list.
struct _ArrayList* file_find(const char* dir, const char* name_pattern, bool recursive);

#ifdef __cplusplus
}
#endif

#endif // FILE_UTILS_H
