#ifndef FILE_H
#define FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Existing API (unchanged)
// ---------------------------------------------------------------------------

// Read entire text file into malloc'd buffer. Caller must free().
char* read_text_file(const char *filename);

// Read binary file with explicit size output. Caller must free().
char* read_binary_file(const char *filename, size_t *out_size);

// Write string content to a text file.
void write_text_file(const char *filename, const char *content);

// Create directory recursively if it doesn't exist.
bool create_dir(const char* dir_path);

// ---------------------------------------------------------------------------
// File metadata
// ---------------------------------------------------------------------------

typedef struct FileStat {
    int64_t size;           // file size in bytes (-1 on error)
    time_t modified;        // last modification time
    time_t created;         // creation time (where available)
    uint16_t mode;          // Unix permission bits (0 on Windows)
    bool is_file;
    bool is_dir;
    bool is_symlink;
    bool exists;
} FileStat;

// ---------------------------------------------------------------------------
// Options for file copy
// ---------------------------------------------------------------------------

typedef struct FileCopyOptions {
    bool overwrite;         // overwrite destination if exists (default: false)
    bool preserve_metadata; // preserve permissions + timestamps (default: false)
} FileCopyOptions;

// ---------------------------------------------------------------------------
// Callback for streaming line reads.
// Return false to stop reading.
// ---------------------------------------------------------------------------
typedef bool (*FileLineCallback)(const char* line, size_t len, int line_number,
                                 void* user_data);

// ---------------------------------------------------------------------------
// Write operations
// ---------------------------------------------------------------------------

// Write raw bytes to file. Returns 0 on success, -1 on error.
int write_binary_file(const char* filename, const char* data, size_t len);

// Append string to file. Returns 0 on success, -1 on error.
int append_text_file(const char* filename, const char* content);

// Append raw bytes to file. Returns 0 on success, -1 on error.
int append_binary_file(const char* filename, const char* data, size_t len);

// Write to temp file then rename (crash-safe). Returns 0 on success, -1 on error.
int write_text_file_atomic(const char* filename, const char* content);

// ---------------------------------------------------------------------------
// File operations
// ---------------------------------------------------------------------------

// Copy file. Creates parent dirs if needed. Returns 0 on success, -1 on error.
int file_copy(const char* src, const char* dst, const FileCopyOptions* opts);

// Move/rename file. Falls back to copy+delete across filesystems.
int file_move(const char* src, const char* dst);

// Delete a file (not directory). Returns 0 on success, -1 on error.
int file_delete(const char* path);

// Create empty file or update modification time. Returns 0 on success, -1 on error.
int file_touch(const char* path);

// Create symbolic link. Returns 0 on success, -1 on error.
int file_symlink(const char* target, const char* link_path);

// Set file permissions. No-op on Windows. Returns 0 on success, -1 on error.
int file_chmod(const char* path, uint16_t mode);

// Rename file within same directory. Returns 0 on success, -1 on error.
int file_rename(const char* old_path, const char* new_path);

// ---------------------------------------------------------------------------
// Queries & metadata
// ---------------------------------------------------------------------------

bool     file_exists(const char* path);
bool     file_is_file(const char* path);
bool     file_is_dir(const char* path);
bool     file_is_symlink(const char* path);
FileStat file_stat(const char* path);
int64_t  file_size(const char* path);

// Resolve to absolute canonical path. Caller must free(). NULL on error.
char* file_realpath(const char* path);

// ---------------------------------------------------------------------------
// Streaming reads
// ---------------------------------------------------------------------------

// Stream file line-by-line via callback. Returns 0 on success, -1 on error.
int file_read_lines(const char* filename, FileLineCallback cb, void* user_data);

// ---------------------------------------------------------------------------
// Temporary files (always under ./temp/ per project rules)
// ---------------------------------------------------------------------------

// Generate unique temp file path. Caller must free().
char* file_temp_path(const char* prefix, const char* suffix);

// Create temp file, return path. Caller must free().
char* file_temp_create(const char* prefix, const char* suffix);

// Create temp directory, return path. Caller must free().
char* dir_temp_create(const char* prefix);

// ---------------------------------------------------------------------------
// Path utilities
// ---------------------------------------------------------------------------

// Join path segments with separator. Caller must free().
char* file_path_join(const char* base, const char* relative);

// Parent directory of path. Caller must free().
char* file_path_dirname(const char* path);

// Filename component (pointer into input string — no alloc).
const char* file_path_basename(const char* path);

// File extension including dot (pointer into input string — no alloc).
const char* file_path_ext(const char* path);

#ifdef __cplusplus
}
#endif

#endif // FILE_H
