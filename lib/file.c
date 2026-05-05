/* Enable POSIX/XSI interfaces used by nftw(), realpath(), and fileno(). */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <string.h>
#include "memtrack.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #include <io.h>
  #include <direct.h>
  #include <process.h>
  #define getpid _getpid
  #define mkdir_compat(p, m) _mkdir(p)
  #define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
  #define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
  #define S_ISLNK(m) (0)  // Windows stat doesn't report symlinks this way
#else
  #include <unistd.h>
  #include <sys/types.h>
  #include <utime.h>
  #define mkdir_compat(p, m) mkdir(p, m)
#endif

/* Explicit strdup declaration for compatibility */
extern char *strdup(const char *s);
#include <stdbool.h>
#include "file.h"
#include "log.h"

// Function to read and display the content of a text file
char* read_text_file(const char *filename) {
    FILE *file = fopen(filename, "r"); // open the file in read mode
    if (file == NULL) { // handle error when file cannot be opened
        log_error("Error opening file: %s", filename);
        return NULL;
    }
    // ensure it is a regular file
    struct stat sb;
    if (fstat(fileno(file), &sb) == -1) {
        log_error("Error getting file status: %s", filename);
        fclose(file);
        return NULL;
    }
    if (!S_ISREG(sb.st_mode)) {
        log_error("Not regular file: %s", filename);
        fclose(file);
        return NULL;
    }

    fseek(file, 0, SEEK_END);  // move the file pointer to the end to determine file size
    long fileSize = ftell(file);
    rewind(file); // reset file pointer to the beginning

    char* buf = (char*)mem_alloc(fileSize + 1, MEM_CAT_TEMP); // allocate memory for the file content
    if (!buf) {
        perror("Memory allocation failed");
        fclose(file);
        return NULL;
    }

    // read the file content into the buffer
    size_t bytesRead = fread(buf, 1, fileSize, file);
    buf[bytesRead] = '\0'; // Null-terminate the buffer

    // clean up
    fclose(file);
    return buf;
}

// Read binary file with explicit size output
// Returns allocated buffer, sets out_size to number of bytes read
// Caller must mem_free() the returned buffer
char* read_binary_file(const char *filename, size_t *out_size) {
    if (out_size) *out_size = 0;
    
    FILE *file = fopen(filename, "rb"); // open in binary mode
    if (file == NULL) {
        log_error("Error opening file: %s", filename);
        return NULL;
    }
    // ensure it is a regular file
    struct stat sb;
    if (fstat(fileno(file), &sb) == -1) {
        log_error("Error getting file status: %s", filename);
        fclose(file);
        return NULL;
    }
    if (!S_ISREG(sb.st_mode)) {
        log_error("Not regular file: %s", filename);
        fclose(file);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    rewind(file);

    char* buf = (char*)mem_alloc(fileSize + 1, MEM_CAT_TEMP);
    if (!buf) {
        perror("Memory allocation failed");
        fclose(file);
        return NULL;
    }

    size_t bytesRead = fread(buf, 1, fileSize, file);
    buf[bytesRead] = '\0'; // null-terminate for safety, but don't rely on it
    
    if (out_size) *out_size = bytesRead;

    fclose(file);
    return buf;
}

void write_text_file(const char *filename, const char *content) {
    FILE *file = fopen(filename, "w"); // Open the file in write mode
    if (file == NULL) {
        perror("Error opening file"); // Handle error if file cannot be opened
        return;
    }
    // Write the string to the file
    if (fprintf(file, "%s", content) < 0) {
        perror("Error writing to file");
    }
    fclose(file); // Close the file
}

// Create directory recursively if it doesn't exist
bool create_dir(const char* dir_path) {
    struct stat st = {0};
    
    if (stat(dir_path, &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    
    // Create a mutable copy of the path for manipulation
    char* path_copy = mem_strdup(dir_path, MEM_CAT_TEMP);
    if (!path_copy) {
        fprintf(stderr, "Memory allocation failed for path copy\n");
        return false;
    }
    
    // Find the last slash to get parent directory
    char* last_slash = strrchr(path_copy, '/');
    if (last_slash && last_slash != path_copy) {
        *last_slash = '\0';
        
        // Recursively create parent directory
        if (!create_dir(path_copy)) {
            mem_free(path_copy);
            return false;
        }
    }
    
    mem_free(path_copy);
    
    // Now create this directory
    int ret;
    #ifdef _WIN32
    ret = mkdir(dir_path);
    #else
    ret = mkdir(dir_path, 0755);
    #endif
    if (ret) {
        fprintf(stderr, "Failed to create directory %s: %s\n", dir_path, strerror(errno));
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// ensure parent directory of a file path exists
static void ensure_parent_dir(const char* filepath) {
    char* copy = mem_strdup(filepath, MEM_CAT_TEMP);
    if (!copy) return;
    char* last = strrchr(copy, '/');
#ifdef _WIN32
    if (!last) last = strrchr(copy, '\\');
#endif
    if (last && last != copy) {
        *last = '\0';
        create_dir(copy);
    }
    mem_free(copy);
}

// ---------------------------------------------------------------------------
// Write operations
// ---------------------------------------------------------------------------

int write_binary_file(const char* filename, const char* data, size_t len) {
    if (!filename || !data) {
        log_error("write_binary_file: NULL argument");
        return -1;
    }
    FILE* f = fopen(filename, "wb");
    if (!f) {
        log_error("write_binary_file: cannot open '%s': %s", filename, strerror(errno));
        return -1;
    }
    size_t written = fwrite(data, 1, len, f);
    fclose(f);
    if (written != len) {
        log_error("write_binary_file: short write to '%s'", filename);
        return -1;
    }
    return 0;
}

int append_text_file(const char* filename, const char* content) {
    if (!filename || !content) {
        log_error("append_text_file: NULL argument");
        return -1;
    }
    FILE* f = fopen(filename, "a");
    if (!f) {
        log_error("append_text_file: cannot open '%s': %s", filename, strerror(errno));
        return -1;
    }
    if (fprintf(f, "%s", content) < 0) {
        log_error("append_text_file: write error on '%s'", filename);
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

int append_binary_file(const char* filename, const char* data, size_t len) {
    if (!filename || !data) {
        log_error("append_binary_file: NULL argument");
        return -1;
    }
    FILE* f = fopen(filename, "ab");
    if (!f) {
        log_error("append_binary_file: cannot open '%s': %s", filename, strerror(errno));
        return -1;
    }
    size_t written = fwrite(data, 1, len, f);
    fclose(f);
    if (written != len) {
        log_error("append_binary_file: short write to '%s'", filename);
        return -1;
    }
    return 0;
}

int write_text_file_atomic(const char* filename, const char* content) {
    if (!filename || !content) {
        log_error("write_text_file_atomic: NULL argument");
        return -1;
    }

    // build temp file name in same directory
    size_t flen = strlen(filename);
    size_t tlen = flen + 8; // ".XXXXXX"
    char* tmp = (char*)mem_alloc(tlen + 1, MEM_CAT_TEMP);
    if (!tmp) return -1;
    snprintf(tmp, tlen + 1, "%s.XXXXXX", filename);

#ifdef _WIN32
    // _mktemp modifies in-place
    if (_mktemp(tmp) == NULL) {
        log_error("write_text_file_atomic: _mktemp failed");
        mem_free(tmp);
        return -1;
    }
#else
    int fd = mkstemp(tmp);
    if (fd < 0) {
        log_error("write_text_file_atomic: mkstemp failed: %s", strerror(errno));
        mem_free(tmp);
        return -1;
    }
    close(fd);
#endif

    // write content to temp file
    FILE* f = fopen(tmp, "w");
    if (!f) {
        log_error("write_text_file_atomic: cannot open temp '%s': %s", tmp, strerror(errno));
        mem_free(tmp);
        return -1;
    }
    if (fprintf(f, "%s", content) < 0) {
        log_error("write_text_file_atomic: write error on '%s'", tmp);
        fclose(f);
        remove(tmp);
        mem_free(tmp);
        return -1;
    }
    fclose(f);

    // atomic rename
    if (rename(tmp, filename) != 0) {
        log_error("write_text_file_atomic: rename '%s' -> '%s' failed: %s",
                  tmp, filename, strerror(errno));
        remove(tmp);
        mem_free(tmp);
        return -1;
    }

    mem_free(tmp);
    return 0;
}

// ---------------------------------------------------------------------------
// File operations
// ---------------------------------------------------------------------------

int file_copy(const char* src, const char* dst, const FileCopyOptions* opts) {
    if (!src || !dst) {
        log_error("file_copy: NULL argument");
        return -1;
    }

    // check if destination exists when overwrite is not set
    if (!opts || !opts->overwrite) {
        struct stat st;
        if (stat(dst, &st) == 0) {
            log_error("file_copy: destination '%s' exists", dst);
            return -1;
        }
    }

    // ensure parent directory exists
    ensure_parent_dir(dst);

    FILE* fin = fopen(src, "rb");
    if (!fin) {
        log_error("file_copy: cannot open source '%s': %s", src, strerror(errno));
        return -1;
    }

    FILE* fout = fopen(dst, "wb");
    if (!fout) {
        log_error("file_copy: cannot open destination '%s': %s", dst, strerror(errno));
        fclose(fin);
        return -1;
    }

    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fin)) > 0) {
        if (fwrite(buf, 1, n, fout) != n) {
            log_error("file_copy: write error on '%s'", dst);
            fclose(fin);
            fclose(fout);
            return -1;
        }
    }
    fclose(fin);
    fclose(fout);

    // preserve metadata if requested
    if (opts && opts->preserve_metadata) {
#ifndef _WIN32
        struct stat src_st;
        if (stat(src, &src_st) == 0) {
            chmod(dst, src_st.st_mode);
            struct utimbuf times;
            times.actime = src_st.st_atime;
            times.modtime = src_st.st_mtime;
            utime(dst, &times);
        }
#endif
    }

    return 0;
}

int file_move(const char* src, const char* dst) {
    if (!src || !dst) {
        log_error("file_move: NULL argument");
        return -1;
    }

    ensure_parent_dir(dst);

    // try rename first (same filesystem)
    if (rename(src, dst) == 0) return 0;

    // cross-filesystem: copy + delete
    if (errno == EXDEV) {
        FileCopyOptions opts = {true, true};
        if (file_copy(src, dst, &opts) != 0) return -1;
        if (remove(src) != 0) {
            log_error("file_move: copied but failed to remove source '%s': %s",
                      src, strerror(errno));
            return -1;
        }
        return 0;
    }

    log_error("file_move: rename '%s' -> '%s' failed: %s", src, dst, strerror(errno));
    return -1;
}

int file_delete(const char* path) {
    if (!path) {
        log_error("file_delete: NULL path");
        return -1;
    }
    if (remove(path) != 0) {
        log_error("file_delete: cannot remove '%s': %s", path, strerror(errno));
        return -1;
    }
    return 0;
}

#ifndef _WIN32
#include <ftw.h>
static int delete_recursive_cb(const char* fpath, const struct stat* sb,
                               int typeflag, struct FTW* ftwbuf) {
    (void)sb; (void)ftwbuf;
    int rv;
    if (typeflag == FTW_D || typeflag == FTW_DP) {
        rv = rmdir(fpath);
    } else {
        rv = remove(fpath);
    }
    if (rv != 0) {
        log_error("file_delete_recursive: cannot remove '%s': %s", fpath, strerror(errno));
    }
    return rv;
}
#endif

int file_delete_recursive(const char* path) {
    if (!path) {
        log_error("file_delete_recursive: NULL path");
        return -1;
    }
    struct stat st;
    if (stat(path, &st) != 0) {
        log_error("file_delete_recursive: path does not exist: '%s'", path);
        return -1;
    }
    if (!S_ISDIR(st.st_mode)) {
        return file_delete(path);
    }
#ifdef _WIN32
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "rmdir /S /Q \"%s\"", path);
    int result = system(cmd);
    if (result != 0) {
        log_error("file_delete_recursive: failed to delete directory '%s'", path);
    }
    return result;
#else
    return nftw(path, delete_recursive_cb, 64, FTW_DEPTH | FTW_PHYS);
#endif
}

int file_touch(const char* path) {
    if (!path) {
        log_error("file_touch: NULL path");
        return -1;
    }

    struct stat st;
    if (stat(path, &st) == 0) {
        // file exists — update modification time
#ifdef _WIN32
        HANDLE h = CreateFileA(path, FILE_WRITE_ATTRIBUTES, 0, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            FILETIME ft;
            GetSystemTimeAsFileTime(&ft);
            SetFileTime(h, NULL, NULL, &ft);
            CloseHandle(h);
        }
#else
        if (utime(path, NULL) != 0) {
            log_error("file_touch: utime failed on '%s': %s", path, strerror(errno));
            return -1;
        }
#endif
        return 0;
    }

    // create empty file
    FILE* f = fopen(path, "w");
    if (!f) {
        log_error("file_touch: cannot create '%s': %s", path, strerror(errno));
        return -1;
    }
    fclose(f);
    return 0;
}

int file_symlink(const char* target, const char* link_path) {
    if (!target || !link_path) {
        log_error("file_symlink: NULL argument");
        return -1;
    }
#ifdef _WIN32
    DWORD flags = 0;
    struct stat st;
    if (stat(target, &st) == 0 && S_ISDIR(st.st_mode)) {
        flags = SYMBOLIC_LINK_FLAG_DIRECTORY;
    }
    if (!CreateSymbolicLinkA(link_path, target, flags)) {
        log_error("file_symlink: CreateSymbolicLink failed: %lu", GetLastError());
        return -1;
    }
    return 0;
#else
    if (symlink(target, link_path) != 0) {
        log_error("file_symlink: symlink '%s' -> '%s' failed: %s",
                  link_path, target, strerror(errno));
        return -1;
    }
    return 0;
#endif
}

int file_chmod(const char* path, uint16_t mode) {
    if (!path) {
        log_error("file_chmod: NULL path");
        return -1;
    }
#ifdef _WIN32
    (void)mode;
    return 0; // no-op on Windows
#else
    if (chmod(path, mode) != 0) {
        log_error("file_chmod: chmod failed on '%s': %s", path, strerror(errno));
        return -1;
    }
    return 0;
#endif
}

int file_rename(const char* old_path, const char* new_path) {
    if (!old_path || !new_path) {
        log_error("file_rename: NULL argument");
        return -1;
    }
    if (rename(old_path, new_path) != 0) {
        log_error("file_rename: rename '%s' -> '%s' failed: %s",
                  old_path, new_path, strerror(errno));
        return -1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Queries & metadata
// ---------------------------------------------------------------------------

bool file_exists(const char* path) {
    if (!path) return false;
    struct stat st;
    return stat(path, &st) == 0;
}

bool file_is_file(const char* path) {
    if (!path) return false;
    struct stat st;
    if (stat(path, &st) != 0) return false;
    return S_ISREG(st.st_mode);
}

bool file_is_dir(const char* path) {
    if (!path) return false;
    struct stat st;
    if (stat(path, &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

bool file_is_symlink(const char* path) {
    if (!path) return false;
#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(path);
    return (attrs != INVALID_FILE_ATTRIBUTES) &&
           (attrs & FILE_ATTRIBUTE_REPARSE_POINT);
#else
    struct stat st;
    if (lstat(path, &st) != 0) return false;
    return S_ISLNK(st.st_mode);
#endif
}

bool file_is_readable(const char* path) {
    if (!path) return false;
#ifdef _WIN32
    return _access(path, 4) == 0;  // 4 = read
#else
    return access(path, R_OK) == 0;
#endif
}

bool file_is_writable(const char* path) {
    if (!path) return false;
#ifdef _WIN32
    return _access(path, 2) == 0;  // 2 = write
#else
    return access(path, W_OK) == 0;
#endif
}

bool file_is_executable(const char* path) {
    if (!path) return false;
#ifdef _WIN32
    return _access(path, 0) == 0;  // Windows: just check existence
#else
    return access(path, X_OK) == 0;
#endif
}

FileStat file_stat(const char* path) {
    FileStat fs;
    memset(&fs, 0, sizeof(fs));
    fs.size = -1;

    if (!path) return fs;

    struct stat st;
    if (stat(path, &st) != 0) return fs;

    fs.exists = true;
    fs.size = (int64_t)st.st_size;
    fs.modified = st.st_mtime;
    fs.is_file = S_ISREG(st.st_mode);
    fs.is_dir = S_ISDIR(st.st_mode);

#ifdef _WIN32
    fs.created = st.st_ctime; // on Windows, st_ctime is creation time
    fs.mode = 0;
    fs.is_symlink = false;
    DWORD attrs = GetFileAttributesA(path);
    if (attrs != INVALID_FILE_ATTRIBUTES)
        fs.is_symlink = (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    fs.created = st.st_ctime; // best approximation on POSIX (status change)
    fs.mode = (uint16_t)(st.st_mode & 07777);
    // check symlink via lstat
    struct stat lst;
    if (lstat(path, &lst) == 0) {
        fs.is_symlink = S_ISLNK(lst.st_mode);
    }
#endif

    return fs;
}

int64_t file_size(const char* path) {
    if (!path) return -1;
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (int64_t)st.st_size;
}

char* file_getcwd(void) {
    char buf[4096];
#ifdef _WIN32
    if (!_getcwd(buf, sizeof(buf))) return NULL;
#else
    if (!getcwd(buf, sizeof(buf))) return NULL;
#endif
    return mem_strdup(buf, MEM_CAT_TEMP);
}

char* file_realpath(const char* path) {
    if (!path) return NULL;
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD len = GetFullPathNameA(path, MAX_PATH, buf, NULL);
    if (len == 0 || len >= MAX_PATH) return NULL;
    return mem_strdup(buf, MEM_CAT_TEMP);
#else
    char* resolved = realpath(path, NULL);  // system malloc'd
    if (!resolved) return NULL;
    char* result = mem_strdup(resolved, MEM_CAT_TEMP);
    free(resolved);  // free system-allocated original
    return result;
#endif
}

// ---------------------------------------------------------------------------
// Streaming reads
// ---------------------------------------------------------------------------

int file_read_lines(const char* filename, FileLineCallback cb, void* user_data) {
    if (!filename || !cb) {
        log_error("file_read_lines: NULL argument");
        return -1;
    }

    FILE* f = fopen(filename, "r");
    if (!f) {
        log_error("file_read_lines: cannot open '%s': %s", filename, strerror(errno));
        return -1;
    }

    char buf[4096];
    int line_no = 0;
    while (fgets(buf, sizeof(buf), f)) {
        size_t len = strlen(buf);
        // strip trailing newline
        if (len > 0 && buf[len - 1] == '\n') {
            buf[--len] = '\0';
            if (len > 0 && buf[len - 1] == '\r') {
                buf[--len] = '\0';
            }
        }
        if (!cb(buf, len, line_no++, user_data)) break;
    }

    fclose(f);
    return 0;
}

// ---------------------------------------------------------------------------
// Temporary files (under ./temp/ per project rules)
// ---------------------------------------------------------------------------

static long s_temp_counter = 0;

char* file_temp_path(const char* prefix, const char* suffix) {
    const char* pfx = prefix ? prefix : "tmp";
    const char* sfx = suffix ? suffix : "";

    // ensure ./temp/ exists
    create_dir("temp");

    char buf[512];
    snprintf(buf, sizeof(buf), "temp/%s_%ld_%d%s",
             pfx, ++s_temp_counter, (int)getpid(), sfx);
    return mem_strdup(buf, MEM_CAT_TEMP);
}

char* file_temp_create(const char* prefix, const char* suffix) {
    char* path = file_temp_path(prefix, suffix);
    if (!path) return NULL;

    FILE* f = fopen(path, "w");
    if (!f) {
        log_error("file_temp_create: cannot create '%s': %s", path, strerror(errno));
        mem_free(path);
        return NULL;
    }
    fclose(f);
    return path;
}

char* dir_temp_create(const char* prefix) {
    const char* pfx = prefix ? prefix : "tmpdir";

    create_dir("temp");

    char buf[512];
    snprintf(buf, sizeof(buf), "temp/%s_%ld_%d",
             pfx, ++s_temp_counter, (int)getpid());

    if (!create_dir(buf)) {
        log_error("dir_temp_create: cannot create '%s'", buf);
        return NULL;
    }
    return mem_strdup(buf, MEM_CAT_TEMP);
}

// ---------------------------------------------------------------------------
// Path utilities
// ---------------------------------------------------------------------------

#ifdef _WIN32
  #define PATH_SEP '\\'
  #define IS_SEP(c) ((c) == '/' || (c) == '\\')
#else
  #define PATH_SEP '/'
  #define IS_SEP(c) ((c) == '/')
#endif

char* file_path_join(const char* base, const char* relative) {
    if (!base || !*base) return relative ? mem_strdup(relative, MEM_CAT_TEMP) : NULL;
    if (!relative || !*relative) return mem_strdup(base, MEM_CAT_TEMP);

    size_t blen = strlen(base);
    size_t rlen = strlen(relative);

    // skip leading separator on relative if base ends with one
    bool base_has_sep = IS_SEP(base[blen - 1]);
    bool rel_has_sep = IS_SEP(relative[0]);

    size_t need = blen + rlen + 2; // separator + nul
    char* out = (char*)mem_alloc(need, MEM_CAT_TEMP);
    if (!out) return NULL;

    if (base_has_sep && rel_has_sep) {
        snprintf(out, need, "%s%s", base, relative + 1);
    } else if (!base_has_sep && !rel_has_sep) {
        snprintf(out, need, "%s%c%s", base, PATH_SEP, relative);
    } else {
        snprintf(out, need, "%s%s", base, relative);
    }

    return out;
}

char* file_path_dirname(const char* path) {
    if (!path || !*path) return mem_strdup(".", MEM_CAT_TEMP);

    size_t len = strlen(path);
    // skip trailing separators
    while (len > 1 && IS_SEP(path[len - 1])) len--;

    // find last separator
    const char* last = NULL;
    for (size_t i = 0; i < len; i++) {
        if (IS_SEP(path[i])) last = path + i;
    }

    if (!last) return mem_strdup(".", MEM_CAT_TEMP);
    if (last == path) return mem_strdup("/", MEM_CAT_TEMP);

    size_t dir_len = (size_t)(last - path);
    char* out = (char*)mem_alloc(dir_len + 1, MEM_CAT_TEMP);
    if (!out) return NULL;
    memcpy(out, path, dir_len);
    out[dir_len] = '\0';
    return out;
}

const char* file_path_basename(const char* path) {
    if (!path || !*path) return path;

    const char* last = path + strlen(path) - 1;
    // skip trailing separators
    while (last > path && IS_SEP(*last)) last--;

    // find last separator before the basename
    const char* start = last;
    while (start > path && !IS_SEP(*(start - 1))) start--;

    return start;
}

const char* file_path_ext(const char* path) {
    if (!path) return NULL;
    const char* base = file_path_basename(path);
    if (!base) return NULL;

    const char* dot = strrchr(base, '.');
    if (!dot || dot == base) return NULL;
    return dot;
}

// ---------------------------------------------------------------------------
// Cache / directory convenience
// ---------------------------------------------------------------------------

int file_ensure_dir(const char* dir_path) {
    if (!dir_path) return -1;
    if (file_exists(dir_path)) {
        if (file_is_dir(dir_path)) return 0;
        log_error("file_ensure_dir: path exists but is not a directory: %s", dir_path);
        return -1;
    }
    if (!create_dir(dir_path)) {
        log_error("file_ensure_dir: failed to create directory: %s", dir_path);
        return -1;
    }
    return 0;
}

char* file_cache_path(const char* key, const char* cache_dir, const char* ext) {
    if (!key || !cache_dir) return NULL;
    if (!ext) ext = ".cache";

    // DJB2 hash
    unsigned long hash = 5381;
    for (const char* s = key; *s; s++) {
        hash = ((hash << 5) + hash) + (unsigned char)*s;
    }

    size_t dir_len = strlen(cache_dir);
    size_t ext_len = strlen(ext);
    // "<dir>/<8hex><ext>\0"
    char* buf = (char*)mem_alloc(dir_len + 1 + 8 + ext_len + 1, MEM_CAT_TEMP);
    if (!buf) return NULL;
    snprintf(buf, dir_len + 1 + 8 + ext_len + 1, "%s/%08lx%s", cache_dir, hash, ext);
    return buf;
}
