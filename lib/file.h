#ifndef FILE_H
#define FILE_H
#include "lambda_api.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include "memtrack.h"

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

// read a regular file into a NUL-terminated buffer owned by mem_alloc().
// the caller supplies the allocation category and may receive the byte count.
// the returned buffer must be released with mem_free().
bool file_read_all(const char* filename, MemCategory category,
                   char** out_data, size_t* out_size);

// Open a regular file without following a final-component symlink/reparse point.
// The caller owns the returned stream and must fclose() it.
FILE* file_open_regular_read(const char* filename);
FILE* file_open_regular_write(const char* filename, bool overwrite);

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

// revision metadata is separate so existing FileStat callers retain their ABI.
typedef struct FileRevision {
    FileStat stat;
    uint32_t modified_nsec;
    uint64_t device, inode;
} FileRevision;

bool file_revision_equal(const FileRevision* left, const FileRevision* right);
FileRevision file_revision_stream(FILE* stream);

// Raw anchored mechanisms; the broker owns authorization and never lends these
// streams/anchors to workers. Unsupported platforms fail with ENOTSUP.
typedef struct FileAnchor FileAnchor;
typedef enum FileAnchorAction {
    FILE_ANCHOR_TRAVERSE,
    FILE_ANCHOR_READ,
    FILE_ANCHOR_METADATA,
    FILE_ANCHOR_CREATE,
    FILE_ANCHOR_REPLACE,
    FILE_ANCHOR_APPEND,
    FILE_ANCHOR_REMOVE
} FileAnchorAction;
typedef bool (*FileAnchorGuard)(void* context, const char* relative,
    FileAnchorAction action, const FileRevision* object);
typedef bool (*FileAnchorAncestorGuard)(void* context, const FileRevision* directory);
typedef bool (*FileAnchorNamespaceGuard)(void* context, const FileRevision* parent,
    const char* name);
enum {
    FILE_ANCHOR_TRUSTED_PARENTS = 1,
    FILE_ANCHOR_PRIVATE = 2,
    FILE_ANCHOR_SAME_DEVICE = 4,
    // macOS additionally requires F_FULLFSYNC; failure never falls back to
    // fsync alone. Other supported POSIX hosts use fsync's storage guarantee.
    FILE_ANCHOR_FULL_SYNC = 8,
    FILE_ANCHOR_STRICT_STORAGE = FILE_ANCHOR_TRUSTED_PARENTS | FILE_ANCHOR_PRIVATE |
        FILE_ANCHOR_SAME_DEVICE | FILE_ANCHOR_FULL_SYNC
};
typedef struct FileAnchorOptions {
    uint32_t flags;
    FileAnchorGuard guard;
    void* guard_context;
    // Called on every absolute ancestor, including root admission/revalidation.
    FileAnchorAncestorGuard ancestor_guard;
    // Runs before child lookup/create, including absent names and enumeration.
    FileAnchorNamespaceGuard namespace_guard;
} FileAnchorOptions;

FileAnchor* file_anchor_open(const char* path, const FileAnchorOptions* options);
// Create mode syncs each child directory and its parent, including retained
// directories on retry. Read-only acquisition does not perform any sync.
FileAnchor* file_anchor_directory(FileAnchor* parent, const char* relative,
    bool create, uint32_t flags);
// One new child only. Existing directories, links and competing creators fail.
// Creation syncs child then parent. A sync failure can leave the new object;
// failure never authorizes replacing or removing it on an exclusive retry.
FileAnchor* file_anchor_create_directory(FileAnchor* parent, const char* name, uint32_t flags);
bool file_anchor_validate(FileAnchor* anchor);
FileRevision file_anchor_identity(const FileAnchor* anchor);
// Kernel path of an acquired object; these raw mechanisms do not grant access.
// Caller owns the returned mem_alloc() storage. Unsupported hosts fail closed.
char* file_stream_path(FILE* stream);
char* file_anchor_path(FileAnchor* anchor);
void file_anchor_close(FileAnchor* anchor);
bool file_anchor_stat(FileAnchor* anchor, const char* relative, FileRevision* out);
// Removes only the named admitted object; directories must already be empty.
// Identity/revision checks detect substitution, not CAS against external writers.
// A sync error can follow a committed unlink; never retry against a new object.
bool file_anchor_remove(FileAnchor* anchor, const char* relative, const FileRevision* expected);
// Empty relative addresses the held directory itself. Enumeration visits only
// ordinary, singly linked files and directories accepted by the anchor guard.
// The visitor must retain its output privately until the whole call succeeds.
typedef bool (*FileAnchorVisitor)(void* context, const char* name, const FileRevision* revision);
bool file_anchor_visit(FileAnchor* directory, uint32_t max_entries,
    FileAnchorVisitor visitor, void* context);
// Cleanup/accounting must fail on forbidden entries instead of hiding them.
bool file_anchor_visit_strict(FileAnchor* directory, uint32_t max_entries,
    FileAnchorVisitor visitor, void* context);
FILE* file_anchor_open_read(FileAnchor* anchor, const char* relative);
// Create-only, synced empty inode and parent before returning the stream.
// Later content needs its own flush/sync. Failure can leave the created object.
FILE* file_anchor_create_regular(FileAnchor* anchor, const char* relative);
bool file_anchor_read_all(FileAnchor* anchor, const char* relative, uint64_t limit,
    MemCategory category, char** out_data, size_t* out_size, FileRevision* out_revision);
// Capture bytes, revision and canonical name from the same held file. Named
// capture additionally checks the final name before delivery; mem_free() path.
bool file_anchor_read_all_named(FileAnchor* anchor, const char* relative, uint64_t limit,
    MemCategory category, char** out_data, size_t* out_size, FileRevision* out_revision,
    char** out_path);
// NULL expected means create-only; otherwise require the admitted revision.
// Checks destination and staged-inode bindings before publication; not CAS
// against external writers.
// 0 = durable, -1 = not committed, 1 = committed but directory sync failed.
int file_anchor_write_atomic(FileAnchor* anchor, const char* relative,
    const void* content, size_t size, const FileRevision* expected);
// Incremental form of the same atomic publication. The anchor and guard context
// must outlive the writer; commit and abort consume it. Never lend it to workers.
typedef struct FileAtomicWriter FileAtomicWriter;
FileAtomicWriter* file_anchor_write_begin(FileAnchor* anchor, const char* relative,
    const FileRevision* expected);
bool file_anchor_write_chunk(FileAtomicWriter* writer, const void* content, size_t size);
int file_anchor_write_commit(FileAtomicWriter* writer);
void file_anchor_write_abort(FileAtomicWriter* writer);
// Opens without truncation/creation; verifies the held object before mutation.
// 0 = durable; -1 = unchanged; 1 = mutation began but an error left partial or
// non-durable bytes. Preserve errno for errors; never blindly retry status 1.
int file_anchor_write_existing(FileAnchor* anchor, const char* relative,
    const void* content, size_t size, const FileRevision* expected, bool append);
#define FILE_ATOMIC_STAGE_PREFIX ".lambda-stage-"
// Brokers reserve these names in every path component, including reads.
bool file_path_has_atomic_stage(const char* path);

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
int write_binary_file_atomic(const char* filename, const void* content, size_t size);

// Stage bytes, then atomically publish only if filename is absent (EEXIST otherwise).
// The caller must authorize/pin the parent directory; this is not path confinement.
int write_binary_file_atomic_new(const char* filename, const void* content, size_t size);
bool file_read_all_limit(const char* filename, MemCategory category,
    uint64_t limit, char** out_data, size_t* out_size);

// ---------------------------------------------------------------------------
// File operations
// ---------------------------------------------------------------------------

// Copy file. Creates parent dirs if needed. Returns 0 on success, -1 on error.
int file_copy(const char* src, const char* dst, const FileCopyOptions* opts);

// Move/rename file. Falls back to copy+delete across filesystems.
int file_move(const char* src, const char* dst);

// Delete a file (not directory). Returns 0 on success, -1 on error.
int file_delete(const char* path);

// Delete a file or directory recursively. Returns 0 on success, -1 on error.
int file_delete_recursive(const char* path);

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
bool     file_is_readable(const char* path);
bool     file_is_writable(const char* path);
bool     file_is_executable(const char* path);
FileStat file_stat(const char* path);
FileRevision file_revision(const char* path);
int64_t  file_size(const char* path);

// Resolve to absolute canonical path. Caller must free(). NULL on error.
LAMBDA_LIB_API char* file_realpath(const char* path);

// Return current working directory as malloc'd string. Caller must free().
LAMBDA_LIB_API char* file_getcwd(void);

// Write current working directory into caller storage. Returns false on error.
bool file_getcwd_into(char* buffer, size_t capacity);

// ---------------------------------------------------------------------------
// Streaming reads
// ---------------------------------------------------------------------------

// Stream file line-by-line via callback. Returns 0 on success, -1 on error.
int file_read_lines(const char* filename, FileLineCallback cb, void* user_data);

// ---------------------------------------------------------------------------
// Temporary files (always under ./temp/ per project rules)
// ---------------------------------------------------------------------------

// Reserve a unique empty temp file and return its path. Caller removes the file
// and frees the returned path.
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

// File extension for an explicit path length. Returns pointer into `path` and
// writes extension length, including dot, to `ext_len` when non-NULL.
const char* file_path_ext_len(const char* path, size_t path_len, size_t* ext_len);

// Case-insensitive extension check. `ext` may be "txt" or ".txt".
bool file_path_has_ext_ci(const char* path, const char* ext);

// ---------------------------------------------------------------------------
// Cache / directory convenience
// ---------------------------------------------------------------------------

// Ensure directory exists; return 0 on success, -1 if path exists but is not
// a directory or creation fails.
int file_ensure_dir(const char* dir_path);

#ifdef __cplusplus
}
#endif

#endif // FILE_H
