/* Enable POSIX/XSI interfaces used by nftw(), realpath(), and fileno(). */
#ifdef __APPLE__
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif
#endif
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
#include <limits.h>
#include <time.h>
#include "math_checked.hpp"

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #include <io.h>
  #include <direct.h>
  #include <process.h>
  #include <fcntl.h>
  #define getpid _getpid
  #define mkdir_compat(p, m) _mkdir(p)
  #define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
  #define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
  #define S_ISLNK(m) (0)  // Windows stat doesn't report symlinks this way
#else
  #include <unistd.h>
  #include <sys/types.h>
  #include <utime.h>
  #include <dirent.h>
  #include <fcntl.h>
  #define mkdir_compat(p, m) mkdir(p, m)
#endif

/* Explicit strdup declaration for compatibility */
extern char *strdup(const char *s);
#include <stdbool.h>
#include "file.h"
#include "host_paths.h"
#include "log.h"
#include "str.h"
#include "digest.h"
#include "hex.h"
#include "path_str.h"
#include "utf.h"
#ifdef __linux__
#include <sys/syscall.h>
#include <linux/fs.h>
#endif

#ifndef _WIN32
static int file_publish_new_at(int source_dir, const char* source, int destination_dir, const char* destination) {
    // a crash must not leave a canonical record hard-linked to its staging name
#ifdef __APPLE__
    return renameatx_np(source_dir, source, destination_dir, destination, RENAME_EXCL);
#elif defined(__linux__) && defined(SYS_renameat2)
    int status = (int)syscall(SYS_renameat2, source_dir, source, destination_dir, destination, RENAME_NOREPLACE);
    if (status != 0 && errno == ENOSYS) errno = ENOTSUP;
    return status;
#else
    (void)source_dir; (void)source; (void)destination_dir; (void)destination;
    errno = ENOTSUP;
    return -1;
#endif
}
#endif

FILE* file_open_regular_read(const char* filename) {
    if (!filename) return NULL;
#ifdef _WIN32
    HANDLE handle = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, NULL,
                                OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (handle == INVALID_HANDLE_VALUE) return NULL;
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(handle, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
        CloseHandle(handle);
        errno = EACCES;
        return NULL;
    }
    int fd = _open_osfhandle((intptr_t)handle, _O_RDONLY | _O_BINARY);
    if (fd < 0) { CloseHandle(handle); return NULL; }
    FILE* file = _fdopen(fd, "rb");
    if (!file) _close(fd);
    return file;
#else
    int fd = open(filename, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return NULL;
    struct stat sb;
    if (fstat(fd, &sb) != 0 || !S_ISREG(sb.st_mode)) {
        close(fd);
        errno = EACCES;
        return NULL;
    }
    FILE* file = fdopen(fd, "rb");
    if (!file) close(fd);
    return file;
#endif
}

FILE* file_open_regular_write(const char* filename, bool overwrite) {
    if (!filename) return NULL;
#ifdef _WIN32
    DWORD disposition = overwrite ? CREATE_ALWAYS : CREATE_NEW;
    HANDLE handle = CreateFileA(filename, GENERIC_WRITE, 0, NULL, disposition,
                                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (handle == INVALID_HANDLE_VALUE) return NULL;
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(handle, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
        CloseHandle(handle);
        errno = EACCES;
        return NULL;
    }
    int fd = _open_osfhandle((intptr_t)handle, _O_WRONLY | _O_BINARY);
    if (fd < 0) { CloseHandle(handle); return NULL; }
    FILE* file = _fdopen(fd, "wb");
    if (!file) _close(fd);
    return file;
#else
    int flags = O_WRONLY | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK |
                (overwrite ? O_TRUNC : O_EXCL);
    int fd = open(filename, flags, 0666);
    if (fd < 0) return NULL;
    struct stat sb;
    if (fstat(fd, &sb) != 0 || !S_ISREG(sb.st_mode)) {
        close(fd);
        errno = EACCES;
        return NULL;
    }
    FILE* file = fdopen(fd, "wb");
    if (!file) close(fd);
    return file;
#endif
}

bool file_read_all(const char* filename, MemCategory category,
                   char** out_data, size_t* out_size) {
    return file_read_all_limit(filename, category, SIZE_MAX - 1, out_data, out_size);
}

bool file_read_all_limit(const char* filename, MemCategory category,
        uint64_t limit, char** out_data, size_t* out_size) {
    if (out_data) *out_data = NULL;
    if (out_size) *out_size = 0;
    if (!filename || !out_data) {
        log_error("FILE-READ-ALL: invalid arguments");
        return false;
    }

    FILE* file = fopen(filename, "rb");
    if (!file) {
        log_error("FILE-READ-ALL: cannot open '%s': %s", filename, strerror(errno));
        return false;
    }

    struct stat sb;
    if (fstat(fileno(file), &sb) != 0) {
        log_error("FILE-READ-ALL: cannot stat '%s': %s", filename, strerror(errno));
        fclose(file);
        return false;
    }
    if (!S_ISREG(sb.st_mode)) {
        log_error("FILE-READ-ALL: not a regular file: %s", filename);
        fclose(file);
        return false;
    }
    if (sb.st_size < 0 || (uintmax_t)sb.st_size > (uintmax_t)(SIZE_MAX - 1) || (uintmax_t)sb.st_size > limit) {
        log_error("FILE-READ-ALL: file is too large: %s", filename);
        fclose(file);
        return false;
    }

    size_t file_size = (size_t)sb.st_size;
    char* data = (char*)mem_alloc(file_size + 1, category);
    if (!data) {
        log_error("FILE-READ-ALL: allocation failed for '%s'", filename);
        fclose(file);
        return false;
    }

    size_t bytes_read = fread(data, 1, file_size, file);
    bool complete = bytes_read == file_size && !ferror(file);
    fclose(file);
    if (!complete) {
        log_error("FILE-READ-ALL: short read from '%s'", filename);
        mem_free(data);
        return false;
    }

    data[file_size] = '\0';
    *out_data = data;
    if (out_size) *out_size = file_size;
    return true;
}

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
        log_error("file: memory allocation failed for path copy");
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
        // another creator may have won after stat; only an existing directory is success.
        if (errno == EEXIST && stat(dir_path, &st) == 0 && S_ISDIR(st.st_mode)) return true;
        log_error("file: failed to create directory %s: %s", dir_path, strerror(errno));
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
    bool failed = written != len || ferror(f);
    if (fclose(f) != 0) failed = true;
    if (failed) {
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
    bool failed = written != len || ferror(f);
    if (fclose(f) != 0) failed = true;
    if (failed) {
        log_error("append_binary_file: short write to '%s'", filename);
        return -1;
    }
    return 0;
}

int write_text_file_atomic(const char* filename, const char* content) {
    return content ? write_binary_file_atomic(filename, content, strlen(content)) : -1;
}

static int write_binary_file_atomic_mode(const char* filename, const void* content,
        size_t content_len, bool overwrite) {
    if (!filename || !content) {
        log_error("write_text_file_atomic: NULL argument");
        errno = EINVAL;
        return -1;
    }

    // build temp file name in same directory
    size_t flen = strlen(filename);
    size_t tlen = 0;
    size_t allocation_size = 0;
    if (!math_checked_add(flen, 7, &tlen) ||
        !math_checked_add(tlen, 1, &allocation_size)) return -1;
    char* tmp = (char*)mem_alloc(allocation_size, MEM_CAT_TEMP);
    if (!tmp) return -1;
    snprintf(tmp, allocation_size, "%s.XXXXXX", filename);

    int fd = -1;
#ifdef _WIN32
    if (_mktemp_s(tmp, allocation_size) != 0 ||
        (fd = _open(tmp, _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY,
                    _S_IREAD | _S_IWRITE)) < 0) {
        log_error("write_text_file_atomic: _mktemp failed");
        mem_free(tmp);
        return -1;
    }
#else
    fd = mkstemp(tmp);
    if (fd < 0) {
        log_error("write_text_file_atomic: mkstemp failed: %s", strerror(errno));
        mem_free(tmp);
        return -1;
    }
#endif

    // Keep using the exclusively-created descriptor; reopening the pathname
    // would reintroduce a symlink swap between creation and writing.
#ifdef _WIN32
    FILE* f = _fdopen(fd, "wb");
#else
    FILE* f = fdopen(fd, "wb");
#endif
    if (!f) {
        log_error("write_text_file_atomic: cannot open temp '%s': %s", tmp, strerror(errno));
#ifdef _WIN32
        _close(fd);
#else
        close(fd);
#endif
        remove(tmp);
        mem_free(tmp);
        return -1;
    }
    if (fwrite(content, 1, content_len, f) != content_len || fflush(f) != 0) {
        log_error("write_text_file_atomic: write error on '%s'", tmp);
        fclose(f);
        remove(tmp);
        mem_free(tmp);
        return -1;
    }
#ifdef _WIN32
    if (_commit(fd) != 0) {
#else
    if (fsync(fd) != 0) {
#endif
        log_error("write_text_file_atomic: sync error on '%s'", tmp);
        fclose(f);
        remove(tmp);
        mem_free(tmp);
        return -1;
    }
    if (fclose(f) != 0) {
        remove(tmp);
        mem_free(tmp);
        return -1;
    }

    int published;
    if (overwrite) {
        published = file_rename(tmp, filename);
    } else {
        // D7.5.6: no existence-check/rename gap, and no replacement fallback.
#ifdef _WIN32
        published = MoveFileExA(tmp, filename, MOVEFILE_WRITE_THROUGH) ? 0 : -1;
        if (published != 0) {
            DWORD error = GetLastError();
            errno = (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) ? EEXIST : EACCES;
        }
#else
        published = file_publish_new_at(AT_FDCWD, tmp, AT_FDCWD, filename);
#endif
    }
    if (published != 0) {
        int publish_errno = errno;
        log_error("write_text_file_atomic: rename '%s' -> '%s' failed",
                  tmp, filename);
        remove(tmp);
        mem_free(tmp);
        errno = publish_errno;
        return -1;
    }

    mem_free(tmp);
    return 0;
}

int write_binary_file_atomic(const char* filename, const void* content, size_t content_len) {
    return write_binary_file_atomic_mode(filename, content, content_len, true);
}

int write_binary_file_atomic_new(const char* filename, const void* content, size_t content_len) {
    return write_binary_file_atomic_mode(filename, content, content_len, false);
}

// ---------------------------------------------------------------------------
// File operations
// ---------------------------------------------------------------------------

int file_copy(const char* src, const char* dst, const FileCopyOptions* opts) {
    if (!src || !dst) {
        log_error("file_copy: NULL argument");
        return -1;
    }

    // ensure parent directory exists
    ensure_parent_dir(dst);

    FILE* fin = file_open_regular_read(src);
    if (!fin) {
        log_error("file_copy: cannot open source '%s': %s", src, strerror(errno));
        return -1;
    }

    bool overwrite = opts && opts->overwrite;
    FILE* fout = file_open_regular_write(dst, overwrite);
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
            if (!overwrite) remove(dst);
            return -1;
        }
    }
    bool read_ok = !ferror(fin);
    bool write_ok = fflush(fout) == 0 && !ferror(fout);
    if (fclose(fin) != 0) read_ok = false;
    if (fclose(fout) != 0) write_ok = false;
    if (!read_ok || !write_ok) {
        log_error("file_copy: incomplete copy '%s' -> '%s'", src, dst);
        if (!overwrite) remove(dst);
        return -1;
    }

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

#ifdef _WIN32
static int file_delete_recursive_win32(const char* path) {
    DWORD attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) return -1;
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        return DeleteFileA(path) ? 0 : -1;
    }
    // Directory reparse points are links, not trees owned by this path.
    if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        return RemoveDirectoryA(path) ? 0 : -1;
    }

    char* pattern = file_path_join(path, "*");
    if (!pattern) return -1;
    WIN32_FIND_DATAA entry;
    HANDLE search = FindFirstFileA(pattern, &entry);
    mem_free(pattern);
    if (search == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_NOT_FOUND) return -1;

    int result = 0;
    if (search != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(entry.cFileName, ".") == 0 || strcmp(entry.cFileName, "..") == 0) continue;
            char* child = file_path_join(path, entry.cFileName);
            if (!child || file_delete_recursive_win32(child) != 0) result = -1;
            if (child) mem_free(child);
            if (result != 0) break;
        } while (FindNextFileA(search, &entry));
        if (result == 0 && GetLastError() != ERROR_NO_MORE_FILES) result = -1;
        FindClose(search);
    }
    if (result == 0 && !RemoveDirectoryA(path)) result = -1;
    return result;
}
#endif

int file_delete_recursive(const char* path) {
    if (!path) {
        log_error("file_delete_recursive: NULL path");
        return -1;
    }
#ifdef _WIN32
    int result = file_delete_recursive_win32(path);
    if (result != 0) {
        log_error("FILE-DELETE-RECURSIVE-WIN32: failed for '%s': %lu",
                  path, GetLastError());
    }
    return result;
#else
    struct stat st;
    if (lstat(path, &st) != 0) {
        log_error("file_delete_recursive: path does not exist: '%s'", path);
        return -1;
    }
    if (!S_ISDIR(st.st_mode)) {
        return file_delete(path);
    }
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
#ifdef _WIN32
    if (MoveFileExA(old_path, new_path, MOVEFILE_REPLACE_EXISTING)) return 0;
    log_error("file_rename: rename '%s' -> '%s' failed: Windows error %lu",
              old_path, new_path, GetLastError());
    return -1;
#else
    if (rename(old_path, new_path) != 0) {
        log_error("file_rename: rename '%s' -> '%s' failed: %s",
                  old_path, new_path, strerror(errno));
        return -1;
    }
    return 0;
#endif
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

static FileRevision file_revision_from_stat(const struct stat* st) {
    FileRevision revision = {0};
    FileStat* fs = &revision.stat;
    fs->size = -1;
    if (!st) return revision;
    fs->exists = true;
    fs->size = (int64_t)st->st_size;
    fs->modified = st->st_mtime;
    fs->created = st->st_ctime;
    fs->is_file = S_ISREG(st->st_mode);
    fs->is_dir = S_ISDIR(st->st_mode);
#ifndef _WIN32
    revision.device = (uint64_t)st->st_dev;
    revision.inode = (uint64_t)st->st_ino;
#ifdef __APPLE__
    revision.modified_nsec = (uint32_t)st->st_mtimespec.tv_nsec;
#else
    revision.modified_nsec = (uint32_t)st->st_mtim.tv_nsec;
#endif
    fs->mode = (uint16_t)(st->st_mode & 07777);
#endif
    return revision;
}

bool file_revision_equal(const FileRevision* left, const FileRevision* right) {
    return left && right && left->device == right->device && left->inode == right->inode &&
        left->modified_nsec == right->modified_nsec &&
        left->stat.exists == right->stat.exists && left->stat.size == right->stat.size &&
        left->stat.modified == right->stat.modified && left->stat.created == right->stat.created &&
        left->stat.mode == right->stat.mode && left->stat.is_file == right->stat.is_file &&
        left->stat.is_dir == right->stat.is_dir && left->stat.is_symlink == right->stat.is_symlink;
}

FileRevision file_revision_stream(FILE* stream) {
    struct stat st;
    return file_revision_from_stat(stream && fstat(fileno(stream), &st) == 0 ? &st : NULL);
}

FileRevision file_revision(const char* path) {
    struct stat st;
    FileRevision revision = file_revision_from_stat(path && stat(path, &st) == 0 ? &st : NULL);
    if (!revision.stat.exists) return revision;

#ifdef _WIN32
    FileStat* fs = &revision.stat;
    WIN32_FILE_ATTRIBUTE_DATA attributes;
    if (GetFileAttributesExA(path, GetFileExInfoStandard, &attributes)) {
        ULARGE_INTEGER ticks;
        ticks.LowPart = attributes.ftLastWriteTime.dwLowDateTime;
        ticks.HighPart = attributes.ftLastWriteTime.dwHighDateTime;
        fs->modified = (time_t)(ticks.QuadPart / 10000000ULL - 11644473600ULL);
        revision.modified_nsec = (uint32_t)((ticks.QuadPart % 10000000ULL) * 100);
    }
    DWORD attrs = GetFileAttributesA(path);
    if (attrs != INVALID_FILE_ATTRIBUTES)
        fs->is_symlink = (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    // check symlink via lstat
    struct stat lst;
    if (lstat(path, &lst) == 0) {
        revision.stat.is_symlink = S_ISLNK(lst.st_mode);
    }
#endif

    return revision;
}

FileStat file_stat(const char* path) {
    return file_revision(path).stat;
}

// Anchors keep acquisition relative to a held directory, with no path fallback.
struct FileAnchor {
    int fd;
    char* path;
    char* guard_prefix;
    FileRevision identity;
    FileAnchorOptions options;
};

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
static bool file_anchor_flags_valid(uint32_t flags) {
    return !(flags & ~(FILE_ANCHOR_TRUSTED_PARENTS | FILE_ANCHOR_PRIVATE | FILE_ANCHOR_SAME_DEVICE));
}

static bool file_anchor_component_valid(const char* name) {
    return name && *name && strcmp(name, ".") && strcmp(name, "..");
}

static bool file_anchor_directory_stat(const struct stat* st, uint32_t flags, bool final) {
    if (!S_ISDIR(st->st_mode) ||
        ((flags & (FILE_ANCHOR_TRUSTED_PARENTS | FILE_ANCHOR_PRIVATE)) &&
            ((st->st_uid != geteuid() && st->st_uid != 0) || (st->st_mode & 0022))) ||
        (final && (flags & FILE_ANCHOR_PRIVATE) &&
            (st->st_uid != geteuid() || (st->st_mode & 0077)))) {
        errno = EACCES;
        return false;
    }
    return true;
}

static bool file_anchor_namespace(const FileAnchorOptions* options, int parent, const char* name) {
    if (!options->namespace_guard) return true;
    struct stat st;
    if (fstat(parent, &st) != 0) return false;
    FileRevision revision = file_revision_from_stat(&st);
    if (options->namespace_guard(options->guard_context, &revision, name)) return true;
    errno = EACCES; return false;
}

static int file_anchor_open_absolute(const char* path, const FileAnchorOptions* options) {
    char copy[PATH_MAX];
    size_t length = path ? strnlen(path, sizeof(copy)) : 0;
    if (!length || length >= sizeof(copy) || path[0] != '/') { errno = EINVAL; return -1; }
    memcpy(copy, path, length + 1);
    int fd = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return -1;
    char* component = copy + 1;
    for (;;) {
        struct stat st;
        if (fstat(fd, &st) != 0 || !file_anchor_directory_stat(&st, options->flags, !*component)) break;
        FileRevision revision = file_revision_from_stat(&st);
        if (options->ancestor_guard && !options->ancestor_guard(options->guard_context, &revision)) {
            errno = EACCES; break;
        }
        if (!*component) return fd;
        char* slash = strchr(component, '/');
        if (slash) *slash = '\0';
        // A dot names the held directory itself; parent traversal remains forbidden.
        if (!strcmp(component, ".")) {
            component = slash ? slash + 1 : component + 1;
            continue;
        }
        if (!file_anchor_component_valid(component)) { errno = EINVAL; break; }
        if (!file_anchor_namespace(options, fd, component)) break;
        int next = openat(fd, component, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (next < 0) break;
        close(fd);
        fd = next;
        component = slash ? slash + 1 : component + strlen(component);
    }
    int error = errno;
    close(fd);
    errno = error;
    return -1;
}

static bool file_anchor_guard(FileAnchor* anchor, const char* relative,
        FileAnchorAction action, const FileRevision* object) {
    if (!anchor->options.guard) return true;
    char scoped[PATH_MAX];
    if (path_str_join_posix_into(scoped, sizeof(scoped), anchor->guard_prefix, relative) < 0) {
        errno = ENAMETOOLONG; return false;
    }
    if (anchor->options.guard(anchor->options.guard_context, scoped, action, object)) return true;
    errno = EACCES;
    return false;
}

static bool file_anchor_object_stat(FileAnchor* anchor, const struct stat* st) {
    if ((!S_ISREG(st->st_mode) && !S_ISDIR(st->st_mode)) ||
        (S_ISREG(st->st_mode) && st->st_nlink != 1) ||
        ((anchor->options.flags & FILE_ANCHOR_PRIVATE) &&
            (st->st_uid != geteuid() || (st->st_mode & 0077)))) {
        errno = EACCES;
        return false;
    }
    if (S_ISDIR(st->st_mode) && !file_anchor_directory_stat(st, anchor->options.flags, true)) return false;
    if ((anchor->options.flags & FILE_ANCHOR_SAME_DEVICE) &&
        (uint64_t)st->st_dev != anchor->identity.device) { errno = EXDEV; return false; }
    return true;
}

static int file_anchor_walk(FileAnchor* anchor, char* copy, bool parent_only, bool create, uint32_t flags) {
    if (!file_anchor_validate(anchor)) return -1;
    int fd = fcntl(anchor->fd, F_DUPFD_CLOEXEC, 0);
    if (fd < 0) return -1;
    char* component = copy;
    for (;;) {
        char* slash = strchr(component, '/');
        if (slash) *slash = '\0';
        if (!file_anchor_component_valid(component)) { errno = EINVAL; break; }
        if (!file_anchor_namespace(&anchor->options, fd, component)) break;
        if (parent_only && !slash) return fd;
        int next = openat(fd, component, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (next < 0 && errno == ENOENT && create) {
            if (!file_anchor_guard(anchor, copy, FILE_ANCHOR_CREATE, NULL)) break;
            if (mkdirat(fd, component, 0700) != 0 && errno != EEXIST) break;
            next = openat(fd, component, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        }
        if (next < 0) break;
        struct stat st;
        bool valid = fstat(next, &st) == 0 && file_anchor_object_stat(anchor, &st) &&
            file_anchor_directory_stat(&st, flags, true);
        FileRevision revision = file_revision_from_stat(valid ? &st : NULL);
        if (valid) valid = file_anchor_guard(anchor, copy, FILE_ANCHOR_TRAVERSE, &revision);
        if (!valid) { int error = errno; close(next); errno = error; break; }
        close(fd);
        fd = next;
        if (!slash) return fd;
        *slash = '/';
        component = slash + 1;
    }
    int error = errno;
    close(fd);
    errno = error;
    return -1;
}

static char* file_anchor_copy_relative(const char* relative) {
    size_t length = relative ? strnlen(relative, PATH_MAX) : 0;
    if (!length || length >= PATH_MAX || relative[0] == '/') { errno = EINVAL; return NULL; }
    // validate the entire request before a directory-creation side effect
    const char* start = relative;
    for (const char* p = start;; p++) {
        if (*p && *p != '/') continue;
        size_t part = (size_t)(p - start);
        if (!part || (part == 1 && start[0] == '.') ||
            (part == 2 && start[0] == '.' && start[1] == '.')) { errno = EINVAL; return NULL; }
        if (!*p) break;
        start = p + 1;
    }
    return mem_dup_n(relative, length, MEM_CAT_TEMP);
}

static int file_anchor_parent(FileAnchor* anchor, const char* relative, char** copy, const char** leaf) {
    *copy = file_anchor_copy_relative(relative);
    if (!*copy) return -1;
    int fd = file_anchor_walk(anchor, *copy, true, false, anchor ? anchor->options.flags : 0);
    char* slash = strrchr(*copy, '/');
    *leaf = slash ? slash + 1 : *copy;
    return fd;
}
#endif

FileAnchor* file_anchor_open(const char* path, const FileAnchorOptions* options) {
#if defined(_WIN32) || defined(__EMSCRIPTEN__)
    (void)path; (void)options; errno = ENOTSUP; return NULL;
#else
    FileAnchorOptions selected = options ? *options : (FileAnchorOptions){0};
    if (!path || !*path || !file_anchor_flags_valid(selected.flags)) { errno = EINVAL; return NULL; }
    char absolute[PATH_MAX];
    if (path[0] == '/') {
        if (path_str_copy(absolute, sizeof(absolute), path) < 0) { errno = ENAMETOOLONG; return NULL; }
    } else {
        char cwd[PATH_MAX];
        if (!file_getcwd_into(cwd, sizeof(cwd))) return NULL;
        if (path_str_join_posix_into(absolute, sizeof(absolute), cwd, !strcmp(path, ".") ? "" : path) < 0) {
            errno = ENAMETOOLONG; return NULL;
        }
    }
    size_t length = strlen(absolute);
    while (length > 1 && absolute[length - 1] == '/') absolute[--length] = '\0';
    int fd = file_anchor_open_absolute(absolute, &selected);
    if (fd < 0) return NULL;
    FileAnchor* anchor = (FileAnchor*)mem_calloc(1, sizeof(FileAnchor), MEM_CAT_SYSTEM);
    if (!anchor) { close(fd); return NULL; }
    anchor->fd = fd;
    anchor->path = mem_dup_n(absolute, length, MEM_CAT_SYSTEM);
    anchor->guard_prefix = mem_dup_n("", 0, MEM_CAT_SYSTEM);
    anchor->options = selected;
    struct stat st;
    if (fstat(fd, &st) != 0 || !anchor->path || !anchor->guard_prefix) { file_anchor_close(anchor); return NULL; }
    anchor->identity = file_revision_from_stat(&st);
    if (!file_anchor_guard(anchor, "", FILE_ANCHOR_TRAVERSE, &anchor->identity)) {
        file_anchor_close(anchor); return NULL;
    }
    return anchor;
#endif
}

void file_anchor_close(FileAnchor* anchor) {
    if (!anchor) return;
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
    close(anchor->fd);
#endif
    mem_free(anchor->path);
    mem_free(anchor->guard_prefix);
    mem_free(anchor);
}

bool file_anchor_validate(FileAnchor* anchor) {
#if defined(_WIN32) || defined(__EMSCRIPTEN__)
    (void)anchor; errno = ENOTSUP; return false;
#else
    if (!anchor) { errno = EINVAL; return false; }
    int current = file_anchor_open_absolute(anchor->path, &anchor->options);
    if (current < 0) return false;
    struct stat st;
    bool valid = fstat(current, &st) == 0 &&
        (uint64_t)st.st_dev == anchor->identity.device && (uint64_t)st.st_ino == anchor->identity.inode;
    close(current);
    if (!valid) { errno = ESTALE; return false; }
    FileRevision revision = file_revision_from_stat(&st);
    return file_anchor_guard(anchor, "", FILE_ANCHOR_TRAVERSE, &revision);
#endif
}

FileRevision file_anchor_identity(const FileAnchor* anchor) {
    return anchor ? anchor->identity : file_revision_from_stat(NULL);
}

static char* file_descriptor_path(int fd) {
#if defined(__APPLE__)
    char path[PATH_MAX];
    if (fcntl(fd, F_GETPATH, path) != 0) return NULL;
    return mem_dup_n(path, strnlen(path, sizeof(path)), MEM_CAT_SYSTEM);
#elif defined(__linux__)
    char descriptor[64], path[PATH_MAX];
    int count = snprintf(descriptor, sizeof(descriptor), "/proc/self/fd/%d", fd);
    if (count < 0 || (size_t)count >= sizeof(descriptor)) { errno = EINVAL; return NULL; }
    ssize_t length = readlink(descriptor, path, sizeof(path));
    if (length < 0) return NULL;
    if ((size_t)length == sizeof(path)) { errno = ENAMETOOLONG; return NULL; }
    return mem_dup_n(path, (size_t)length, MEM_CAT_SYSTEM);
#else
    (void)fd; errno = ENOTSUP; return NULL;
#endif
}

char* file_stream_path(FILE* stream) {
    if (!stream) { errno = EINVAL; return NULL; }
    return file_descriptor_path(fileno(stream));
}

char* file_anchor_path(FileAnchor* anchor) {
    return file_anchor_validate(anchor) ? file_descriptor_path(anchor->fd) : NULL;
}

FileAnchor* file_anchor_directory(FileAnchor* parent, const char* relative, bool create, uint32_t flags) {
#if defined(_WIN32) || defined(__EMSCRIPTEN__)
    (void)parent; (void)relative; (void)create; (void)flags; errno = ENOTSUP; return NULL;
#else
    if (!parent || !file_anchor_flags_valid(flags) || (parent->options.flags & ~flags)) {
        errno = EINVAL; return NULL;
    }
    char* copy = file_anchor_copy_relative(relative);
    if (!copy) return NULL;
    int fd = file_anchor_walk(parent, copy, false, create, flags);
    mem_free(copy);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) != 0 || !file_anchor_directory_stat(&st, flags, true)) {
        int error = errno; close(fd); errno = error; return NULL;
    }
    FileAnchor* child = (FileAnchor*)mem_calloc(1, sizeof(FileAnchor), MEM_CAT_SYSTEM);
    if (!child) { close(fd); return NULL; }
    child->fd = fd;
    child->path = file_path_join(parent->path, relative);
    child->guard_prefix = file_path_join(parent->guard_prefix, relative);
    child->identity = file_revision_from_stat(&st);
    child->options = parent->options;
    child->options.flags = flags;
    if (!child->path || !child->guard_prefix || !file_anchor_validate(child)) { file_anchor_close(child); return NULL; }
    return child;
#endif
}

bool file_anchor_stat(FileAnchor* anchor, const char* relative, FileRevision* out) {
    if (out) *out = file_revision_from_stat(NULL);
#if defined(_WIN32) || defined(__EMSCRIPTEN__)
    (void)anchor; (void)relative; errno = ENOTSUP; return false;
#else
    if (!out) { errno = EINVAL; return false; }
    if (relative && !*relative) {
        struct stat st;
        bool valid = file_anchor_validate(anchor) && fstat(anchor->fd, &st) == 0;
        if (valid) {
            *out = file_revision_from_stat(&st);
            valid = file_anchor_guard(anchor, "", FILE_ANCHOR_METADATA, out);
        }
        if (!valid) *out = file_revision_from_stat(NULL);
        return valid;
    }
    char* copy = NULL; const char* leaf = NULL;
    int parent = file_anchor_parent(anchor, relative, &copy, &leaf);
    struct stat st;
    bool valid = parent >= 0 && fstatat(parent, leaf, &st, AT_SYMLINK_NOFOLLOW) == 0 &&
        file_anchor_object_stat(anchor, &st);
    if (valid) {
        *out = file_revision_from_stat(&st);
        valid = file_anchor_guard(anchor, relative, FILE_ANCHOR_METADATA, out);
    }
    int error = errno;
    if (parent >= 0) close(parent);
    mem_free(copy);
    if (!valid) *out = file_revision_from_stat(NULL);
    errno = error;
    return valid;
#endif
}

bool file_anchor_visit(FileAnchor* directory, uint32_t max_entries,
        FileAnchorVisitor visitor, void* context) {
#if defined(_WIN32) || defined(__EMSCRIPTEN__)
    (void)directory; (void)max_entries; (void)visitor; (void)context;
    errno = ENOTSUP; return false;
#else
    if (!visitor || !max_entries) { errno = EINVAL; return false; }
    FileRevision before;
    if (!file_anchor_stat(directory, "", &before)) return false;
    // A fresh description avoids sharing a directory offset with other calls.
    int fd = openat(directory->fd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return false;
    DIR* stream = fdopendir(fd);
    if (!stream) { int error = errno; close(fd); errno = error; return false; }
    bool valid = true;
    uint32_t visited = 0;
    for (;;) {
        errno = 0;
        struct dirent* entry = readdir(stream);
        if (!entry) { valid = errno == 0; break; }
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (visited++ == max_entries) { errno = EFBIG; valid = false; break; }
        if (!file_anchor_namespace(&directory->options, fd, entry->d_name)) {
            if (errno == EACCES) continue;
            valid = false; break;
        }
        struct stat st;
        if (fstatat(fd, entry->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
            valid = false; break;
        }
        if (!file_anchor_object_stat(directory, &st)) {
            if (errno == EACCES || errno == EXDEV) continue;
            valid = false; break;
        }
        FileRevision revision = file_revision_from_stat(&st);
        if (!file_anchor_guard(directory, entry->d_name, FILE_ANCHOR_METADATA, &revision)) {
            if (errno == EACCES) continue;
            valid = false; break;
        }
        if (!visitor(context, entry->d_name, &revision)) { valid = false; break; }
    }
    int error = errno;
    closedir(stream);
    FileRevision after;
    if (valid) {
        if (!file_anchor_stat(directory, "", &after)) { error = errno; valid = false; }
        else if (!file_revision_equal(&before, &after)) { error = ESTALE; valid = false; }
    }
    errno = error;
    return valid;
#endif
}

static FILE* file_anchor_open_stream(FileAnchor* anchor, const char* relative, bool create) {
#if defined(_WIN32) || defined(__EMSCRIPTEN__)
    (void)anchor; (void)relative; (void)create; errno = ENOTSUP; return NULL;
#else
    char* copy = NULL; const char* leaf = NULL;
    int parent = file_anchor_parent(anchor, relative, &copy, &leaf);
    int fd = -1;
    if (parent >= 0 && (!create || file_anchor_guard(anchor, relative, FILE_ANCHOR_CREATE, NULL)))
        fd = openat(parent, leaf, O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK |
            (create ? O_RDWR | O_CREAT | O_EXCL : O_RDONLY), 0600);
    struct stat st;
    bool valid = fd >= 0 && fstat(fd, &st) == 0 && S_ISREG(st.st_mode) &&
        file_anchor_object_stat(anchor, &st);
    if (fd >= 0 && !valid) errno = EACCES;
    if (valid && !create) {
        FileRevision revision = file_revision_from_stat(&st);
        valid = file_anchor_guard(anchor, relative, FILE_ANCHOR_READ, &revision);
    }
    FILE* stream = valid ? fdopen(fd, create ? "w+b" : "rb") : NULL;
    int error = errno;
    if (!stream && fd >= 0) close(fd);
    if (parent >= 0) close(parent);
    mem_free(copy);
    errno = error;
    return stream;
#endif
}

FILE* file_anchor_open_read(FileAnchor* anchor, const char* relative) {
    return file_anchor_open_stream(anchor, relative, false);
}

FILE* file_anchor_create_regular(FileAnchor* anchor, const char* relative) {
    return file_anchor_open_stream(anchor, relative, true);
}

bool file_anchor_read_all(FileAnchor* anchor, const char* relative, uint64_t limit,
        MemCategory category, char** out_data, size_t* out_size, FileRevision* out_revision) {
    return file_anchor_read_all_named(anchor, relative, limit, category, out_data,
        out_size, out_revision, NULL);
}

bool file_anchor_read_all_named(FileAnchor* anchor, const char* relative, uint64_t limit,
        MemCategory category, char** out_data, size_t* out_size, FileRevision* out_revision,
        char** out_path) {
    if (out_data) *out_data = NULL;
    if (out_size) *out_size = 0;
    if (out_path) *out_path = NULL;
    if (out_revision) *out_revision = file_revision_from_stat(NULL);
    if (!out_data) { errno = EINVAL; return false; }
    FILE* stream = file_anchor_open_read(anchor, relative);
    if (!stream) return false;
    FileRevision before = file_revision_stream(stream);
    uint64_t size = before.stat.size < 0 ? UINT64_MAX : (uint64_t)before.stat.size;
    char* data = size <= limit && size < SIZE_MAX ? (char*)mem_alloc((size_t)size + 1, category) : NULL;
    if (!data) { fclose(stream); errno = size > limit || size >= SIZE_MAX ? EFBIG : ENOMEM; return false; }
    bool valid = fread(data, 1, (size_t)size, stream) == size && !ferror(stream);
    FileRevision after = file_revision_stream(stream);
    char* actual = out_path ? file_stream_path(stream) : NULL;
    if (out_path && !actual) valid = false;
    if (fclose(stream) != 0) valid = false;
    if (valid && out_path) {
        FileRevision named;
        valid = file_anchor_stat(anchor, relative, &named) && file_revision_equal(&after, &named);
    }
    if (!valid || !file_revision_equal(&before, &after) || !file_anchor_validate(anchor)) {
        mem_free(actual); mem_free(data); errno = ESTALE; return false;
    }
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
    if (!file_anchor_guard(anchor, relative, FILE_ANCHOR_READ, &after)) {
        mem_free(actual); mem_free(data); return false;
    }
#endif
    data[size] = '\0';
    *out_data = data;
    if (out_size) *out_size = (size_t)size;
    if (out_revision) *out_revision = after;
    if (out_path) *out_path = actual;
    return true;
}

int file_anchor_write_atomic(FileAnchor* anchor, const char* relative,
        const void* content, size_t size, const FileRevision* expected) {
#if defined(_WIN32) || defined(__EMSCRIPTEN__)
    (void)anchor; (void)relative; (void)content; (void)size; (void)expected; errno = ENOTSUP; return -1;
#else
    if (!content || (expected && (!expected->stat.exists || !expected->stat.is_file))) {
        errno = EINVAL; return -1;
    }
    char* copy = NULL; const char* leaf = NULL;
    int parent = file_anchor_parent(anchor, relative, &copy, &leaf);
    if (parent < 0) { mem_free(copy); return -1; }
    struct stat st;
    bool exists = fstatat(parent, leaf, &st, AT_SYMLINK_NOFOLLOW) == 0;
    FileRevision actual = file_revision_from_stat(exists ? &st : NULL);
    int result = -1;
    if (!exists && errno != ENOENT) goto done;
    if (!expected && exists) { errno = EEXIST; goto done; }
    if (expected && (!exists || !file_anchor_object_stat(anchor, &st) ||
        !file_revision_equal(expected, &actual))) { errno = ESTALE; goto done; }
    if (!file_anchor_guard(anchor, relative, expected ? FILE_ANCHOR_REPLACE : FILE_ANCHOR_CREATE,
            exists ? &actual : NULL)) goto done;
    uint8_t random[16];
    char stage[48];
    int fd = -1;
    for (int retry = 0; retry < 8; retry++) {
        if (!crypto_random_bytes(random, sizeof(random))) { errno = EIO; goto done; }
        memcpy(stage, FILE_ATOMIC_STAGE_PREFIX, sizeof(FILE_ATOMIC_STAGE_PREFIX) - 1);
        hex_encode(random, sizeof(random), stage + sizeof(FILE_ATOMIC_STAGE_PREFIX) - 1);
        fd = openat(parent, stage, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (fd >= 0 || errno != EEXIST) break;
    }
    if (fd < 0) goto done;
    FILE* stream = fdopen(fd, "wb");
    if (!stream) { close(fd); unlinkat(parent, stage, 0); goto done; }
    bool written = fwrite(content, 1, size, stream) == size && fflush(stream) == 0 && fsync(fd) == 0;
    if (fclose(stream) != 0) written = false;
    if (!written || !file_anchor_validate(anchor)) { unlinkat(parent, stage, 0); goto done; }
    if (expected) {
        exists = fstatat(parent, leaf, &st, AT_SYMLINK_NOFOLLOW) == 0;
        actual = file_revision_from_stat(exists ? &st : NULL);
        if (!exists || !file_anchor_object_stat(anchor, &st) || !file_revision_equal(expected, &actual)) {
            unlinkat(parent, stage, 0); errno = ESTALE; goto done;
        }
        if (!file_anchor_guard(anchor, relative, FILE_ANCHOR_REPLACE, &actual)) {
            int error = errno; unlinkat(parent, stage, 0); errno = error; goto done;
        }
        result = renameat(parent, stage, parent, leaf);
    } else {
        if (!file_anchor_guard(anchor, relative, FILE_ANCHOR_CREATE, NULL)) {
            int error = errno; unlinkat(parent, stage, 0); errno = error; goto done;
        }
        result = file_publish_new_at(parent, stage, parent, leaf);
    }
    {
        int publish_error = errno;
        if (result != 0) unlinkat(parent, stage, 0);
        // publication has committed even if directory durability cannot be confirmed
        if (result == 0 && fsync(parent) != 0) {
            publish_error = errno;
            log_error("FILE-ANCHOR: publication committed but directory sync failed");
            result = 1;
        }
        errno = publish_error;
    }
done:
    {
        int error = errno;
        close(parent);
        mem_free(copy);
        errno = error;
    }
    return result;
#endif
}

static bool file_path_has_atomic_stage_folded(const char* path) {
    for (const char* part = path; *part;) {
        size_t prefix_size = sizeof(FILE_ATOMIC_STAGE_PREFIX) - 1;
        // APFS commonly resolves ASCII case aliases to the same staging object.
        if (strnlen(part, prefix_size) == prefix_size &&
            !str_icmp(part, prefix_size, FILE_ATOMIC_STAGE_PREFIX, prefix_size)) return true;
        const char* slash = strchr(part, '/');
        if (!slash) break;
        part = slash + 1;
    }
    return false;
}

bool file_path_has_atomic_stage(const char* path) {
    if (!path) return false;
    bool ascii = true;
    for (const unsigned char* byte = (const unsigned char*)path; *byte; byte++)
        if (*byte >= 128) { ascii = false; break; }
    if (ascii) return file_path_has_atomic_stage_folded(path);
    // Reserve a superset of filesystem aliases; this is never path resolution.
    char* folded = utf8_normalize(path, strlen(path), UTF_NORMALIZE_NFKC_CASEFOLD, NULL);
    if (!folded) return true;
    bool reserved = file_path_has_atomic_stage_folded(folded);
    raw_free(folded); // RAWALLOC_OK: utf8_normalize returns utf8proc storage
    return reserved;
}

int file_anchor_write_existing(FileAnchor* anchor, const char* relative,
        const void* content, size_t size, const FileRevision* expected, bool append) {
#if defined(_WIN32) || defined(__EMSCRIPTEN__)
    (void)anchor; (void)relative; (void)content; (void)size; (void)expected; (void)append;
    errno = ENOTSUP; return -1;
#else
    if (!content || !expected || !expected->stat.is_file) { errno = EINVAL; return -1; }
    char* copy = NULL; const char* leaf = NULL;
    int parent = file_anchor_parent(anchor, relative, &copy, &leaf);
    int fd = parent >= 0 ? openat(parent, leaf, O_WRONLY | O_NOFOLLOW | O_NONBLOCK |
        O_CLOEXEC | (append ? O_APPEND : 0)) : -1;
    struct stat held, named;
    bool valid = fd >= 0 && fstat(fd, &held) == 0 && S_ISREG(held.st_mode) &&
        file_anchor_object_stat(anchor, &held) &&
        fstatat(parent, leaf, &named, AT_SYMLINK_NOFOLLOW) == 0;
    FileRevision actual = file_revision_from_stat(valid ? &held : NULL);
    FileRevision current = file_revision_from_stat(valid ? &named : NULL);
    if (valid) valid = file_revision_equal(expected, &actual) && file_revision_equal(expected, &current);
    if (!valid && fd >= 0) errno = ESTALE;
    if (valid) valid = file_anchor_validate(anchor) && file_anchor_guard(anchor, relative,
        append ? FILE_ANCHOR_APPEND : FILE_ANCHOR_REPLACE, &actual);
    bool touched = false;
    if (valid && !append) { valid = ftruncate(fd, 0) == 0; touched = valid; }
    size_t offset = 0;
    while (valid && offset < size) {
        size_t chunk = size - offset;
        if (chunk > (size_t)SSIZE_MAX) chunk = (size_t)SSIZE_MAX;
        ssize_t written = write(fd, (const char*)content + offset, chunk);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) { valid = false; break; }
        touched = true;
        offset += (size_t)written;
    }
    if (valid) valid = fsync(fd) == 0;
    int error = errno;
    if (fd >= 0 && close(fd) != 0) { valid = false; error = errno; }
    if (parent >= 0) close(parent);
    mem_free(copy);
    errno = error;
    return valid ? 0 : touched ? 1 : -1;
#endif
}

int64_t file_size(const char* path) {
    if (!path) return -1;
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (int64_t)st.st_size;
}

bool file_getcwd_into(char* buffer, size_t capacity) {
    if (!buffer || capacity == 0) return false;
#ifdef _WIN32
    if (capacity > INT_MAX) return false;
    return _getcwd(buffer, (int)capacity) != NULL;
#else
    return getcwd(buffer, capacity) != NULL;
#endif
}

char* file_getcwd(void) {
    char buf[4096];
    if (!file_getcwd_into(buf, sizeof(buf))) return NULL;
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

static bool file_temp_component_valid(const char* component, bool allow_dot) {
    if (!component) return true;
    for (const unsigned char* p = (const unsigned char*)component; *p; p++) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' ||
            (allow_dot && *p == '.')) continue;
        return false;
    }
    return true;
}

static int file_temp_open_unique(const char* prefix, const char* suffix, char** out_path) {
    if (out_path) *out_path = NULL;
    const char* pfx = prefix ? prefix : "tmp";
    const char* sfx = suffix ? suffix : "";
    // unique files live in the host's temp directory (host_paths.h, MB13)
    const char* base = host_path(HOST_PATH_TEMP);
    if (!out_path || !file_temp_component_valid(pfx, false) ||
        !file_temp_component_valid(sfx, true) || !create_dir(base)) {
        log_error("FILE-TEMP-NAME: invalid prefix or suffix");
        return -1;
    }

    size_t path_size = 0;
    if (!math_checked_add(strlen(pfx), strlen(sfx), &path_size) ||
        !math_checked_add(path_size, strlen(base), &path_size) ||
        !math_checked_add(path_size, 1 + 1 + 16 + 1, &path_size)) return -1;
    char* path = (char*)mem_alloc(path_size, MEM_CAT_TEMP);
    if (!path) return -1;

#ifdef _WIN32
    int fd = -1;
    for (int attempt = 0; attempt < 128; attempt++) {
        unsigned int random_a = 0;
        unsigned int random_b = 0;
        if (rand_s(&random_a) != 0 || rand_s(&random_b) != 0) break;
        snprintf(path, path_size, "%s/%s_%08x%08x%s", base, pfx,
                 random_a, random_b, sfx);
        fd = _open(path, _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY,
                   _S_IREAD | _S_IWRITE);
        if (fd >= 0) break;
        if (errno != EEXIST) break;
    }
#else
    snprintf(path, path_size, "%s/%s_XXXXXX%s", base, pfx, sfx);
    size_t suffix_len = strlen(sfx);
    if (suffix_len > INT_MAX) {
        mem_free(path);
        return -1;
    }
    int fd = mkstemps(path, (int)suffix_len);
#endif
    if (fd < 0) {
        log_error("FILE-TEMP-CREATE: cannot reserve a unique path: %s", strerror(errno));
        mem_free(path);
        return -1;
    }
    *out_path = path;
    return fd;
}

char* file_temp_path(const char* prefix, const char* suffix) {
    char* path = NULL;
    int fd = file_temp_open_unique(prefix, suffix, &path);
    if (fd < 0) return NULL;
#ifdef _WIN32
    _close(fd);
#else
    close(fd);
#endif
    return path;
}

char* file_temp_create(const char* prefix, const char* suffix) {
    return file_temp_path(prefix, suffix);
}

char* dir_temp_create(const char* prefix) {
    const char* pfx = prefix ? prefix : "tmpdir";
    const char* base = host_path(HOST_PATH_TEMP);
    if (!file_temp_component_valid(pfx, false) || !create_dir(base)) {
        log_error("DIR-TEMP-NAME: invalid prefix");
        return NULL;
    }
    size_t path_size = 0;
    if (!math_checked_add(strlen(pfx), strlen(base), &path_size) ||
        !math_checked_add(path_size, 1 + 1 + 16 + 1, &path_size)) return NULL;
    char* path = (char*)mem_alloc(path_size, MEM_CAT_TEMP);
    if (!path) return NULL;
#ifdef _WIN32
    bool created = false;
    for (int attempt = 0; attempt < 128; attempt++) {
        unsigned int random_a = 0;
        unsigned int random_b = 0;
        if (rand_s(&random_a) != 0 || rand_s(&random_b) != 0) break;
        snprintf(path, path_size, "%s/%s_%08x%08x", base, pfx, random_a, random_b);
        if (CreateDirectoryA(path, NULL)) { created = true; break; }
        if (GetLastError() != ERROR_ALREADY_EXISTS) break;
    }
    if (!created) {
#else
    snprintf(path, path_size, "%s/%s_XXXXXX", base, pfx);
    if (!mkdtemp(path)) {
#endif
        log_error("DIR-TEMP-CREATE: cannot reserve a unique directory");
        mem_free(path);
        return NULL;
    }
    return path;
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
    return file_path_ext_len(path, strlen(path), NULL);
}

const char* file_path_ext_len(const char* path, size_t path_len, size_t* ext_len) {
    return str_file_ext(path, path_len, ext_len);
}

bool file_path_has_ext_ci(const char* path, const char* ext) {
    if (!path || !ext) return false;

    size_t path_ext_len = 0;
    const char* path_ext = file_path_ext_len(path, strlen(path), &path_ext_len);
    if (!path_ext) return false;

    if (ext[0] == '.') ext++;
    size_t cmp_ext_len = strlen(ext);
    if (cmp_ext_len == 0) return false;

    if (path_ext[0] == '.') {
        path_ext++;
        path_ext_len--;
    }
    return str_ieq(path_ext, path_ext_len, ext, cmp_ext_len);
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
