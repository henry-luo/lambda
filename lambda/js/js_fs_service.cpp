// js_fs_service.cpp — raw filesystem operations owned by the Lambda host.
#include "js_fs_service.h"
#include "js_class.h"
#include "js_runtime.h"
#include "js_typed_array.h"
#include "../../lib/file.h"
#include "../../lib/str.h"

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>

extern "C" Item js_node_throw_system_error(const char* syscall, int error_number);

#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#include <windows.h>
typedef struct _stat64 JsFsServiceStat;
static int js_fs_service_unsupported(void) {
    errno = ENOSYS;
    return -1;
}

static int js_fs_service_link(const char* existing_path, const char* new_path) {
    if (CreateHardLinkA(new_path, existing_path, NULL)) return 0;
    errno = EIO;
    return -1;
}

#define JS_FS_SERVICE_CLOSE _close
#define JS_FS_SERVICE_FCHMOD(descriptor, mode) js_fs_service_unsupported()
#define JS_FS_SERVICE_FSTAT _fstat64
#define JS_FS_SERVICE_LSTAT _stat64
#define JS_FS_SERVICE_STAT _stat64
#define JS_FS_SERVICE_OPEN _open
#define JS_FS_SERVICE_READ _read
#define JS_FS_SERVICE_WRITE _write
#define JS_FS_SERVICE_ACCESS _access
#define JS_FS_SERVICE_CHMOD _chmod
#define JS_FS_SERVICE_LINK js_fs_service_link
#define JS_FS_SERVICE_MKDIR(path, mode) _mkdir(path)
#define JS_FS_SERVICE_RMDIR _rmdir
#define JS_FS_SERVICE_UNLINK _unlink
#ifndef O_BINARY
#define O_BINARY 0
#endif
#else
#include <dirent.h>
#include <sys/statvfs.h>
#include <unistd.h>
typedef struct stat JsFsServiceStat;
#define JS_FS_SERVICE_CLOSE close
#define JS_FS_SERVICE_FCHMOD fchmod
#define JS_FS_SERVICE_FSTAT fstat
#define JS_FS_SERVICE_LSTAT lstat
#define JS_FS_SERVICE_OPEN open
#define JS_FS_SERVICE_READ read
#define JS_FS_SERVICE_WRITE write
#define JS_FS_SERVICE_STAT stat
#define JS_FS_SERVICE_ACCESS access
#define JS_FS_SERVICE_CHMOD chmod
#define JS_FS_SERVICE_LINK link
#define JS_FS_SERVICE_MKDIR(path, mode) mkdir(path, mode)
#define JS_FS_SERVICE_RMDIR rmdir
#define JS_FS_SERVICE_UNLINK unlink
#define O_BINARY 0
#endif

bool js_node_fs_read_write(JubeNodeFilesystemReadWrite* operation) {
    if (!operation || !operation->path || (operation->input_length > 0 && !operation->input)) return false;
    operation->output = NULL;
    operation->output_length = 0;
    operation->error_number = 0;
    operation->error_syscall = NULL;
    int flags = O_RDONLY | O_BINARY;
    if (operation->mode == JUBE_NODE_FILESYSTEM_WRITE) flags = O_WRONLY | O_CREAT | O_TRUNC | O_BINARY;
    if (operation->mode == JUBE_NODE_FILESYSTEM_APPEND) flags = O_WRONLY | O_CREAT | O_APPEND | O_BINARY;
    int descriptor = JS_FS_SERVICE_OPEN(operation->path, flags, 0644);
    if (descriptor < 0) {
        operation->error_number = errno;
        operation->error_syscall = "open";
        return false;
    }
    if (operation->mode != JUBE_NODE_FILESYSTEM_READ) {
        size_t offset = 0;
        while (offset < operation->input_length) {
            int written = (int)JS_FS_SERVICE_WRITE(descriptor, operation->input + offset,
                                                   operation->input_length - offset);
            if (written <= 0) {
                operation->error_number = written < 0 ? errno : EIO;
                operation->error_syscall = "write";
                break;
            }
            offset += (size_t)written;
        }
    } else {
        JsFsServiceStat info = {};
        if (JS_FS_SERVICE_FSTAT(descriptor, &info) != 0 || info.st_size < 0 ||
                (uintmax_t)info.st_size > (uintmax_t)INT_MAX) {
            operation->error_number = errno ? errno : EFBIG;
            operation->error_syscall = "fstat";
        } else {
            operation->output_length = (size_t)info.st_size;
            operation->output = (uint8_t*)malloc(operation->output_length > 0 ?
                                                  operation->output_length : 1);
            if (!operation->output) {
                operation->error_number = ENOMEM;
                operation->error_syscall = "read";
            } else {
                size_t offset = 0;
                while (offset < operation->output_length) {
                    int read_count = (int)JS_FS_SERVICE_READ(descriptor, operation->output + offset,
                                                             operation->output_length - offset);
                    if (read_count < 0) {
                        operation->error_number = errno;
                        operation->error_syscall = "read";
                        break;
                    }
                    if (read_count == 0) break;
                    offset += (size_t)read_count;
                }
                operation->output_length = offset;
            }
        }
    }
    if (JS_FS_SERVICE_CLOSE(descriptor) != 0 && operation->error_number == 0) {
        operation->error_number = errno;
        operation->error_syscall = "close";
    }
    return operation->error_number == 0;
}

void js_node_fs_read_write_release(JubeNodeFilesystemReadWrite* operation) {
    if (!operation) return;
    free(operation->output);
    operation->output = NULL;
    operation->output_length = 0;
}

bool js_node_fs_copy_file(JubeNodeFilesystemCopy* operation) {
    if (!operation || !operation->source_path || !operation->destination_path) return false;
    operation->error_number = 0;
    operation->error_syscall = NULL;
    int source_descriptor = JS_FS_SERVICE_OPEN(operation->source_path, O_RDONLY | O_BINARY, 0);
    if (source_descriptor < 0) {
        operation->error_number = errno;
        operation->error_syscall = "open";
        return false;
    }
    int destination_descriptor = JS_FS_SERVICE_OPEN(operation->destination_path,
                                                    O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666);
    if (destination_descriptor < 0) {
        operation->error_number = errno;
        operation->error_syscall = "open";
        JS_FS_SERVICE_CLOSE(source_descriptor);
        return false;
    }
    const size_t buffer_size = 65536;
    uint8_t* buffer = (uint8_t*)malloc(buffer_size);
    if (!buffer) {
        operation->error_number = ENOMEM;
        operation->error_syscall = "copyfile";
    }
    for (;;) {
        if (operation->error_number != 0) break;
        int read_count = (int)JS_FS_SERVICE_READ(source_descriptor, buffer, buffer_size);
        if (read_count < 0) {
            operation->error_number = errno;
            operation->error_syscall = "read";
            break;
        }
        if (read_count == 0) break;
        size_t offset = 0;
        while (offset < (size_t)read_count) {
            int write_count = (int)JS_FS_SERVICE_WRITE(destination_descriptor, buffer + offset,
                                                        (size_t)read_count - offset);
            if (write_count < 0) {
                operation->error_number = errno;
                operation->error_syscall = "write";
                break;
            }
            if (write_count == 0) {
                operation->error_number = EIO;
                operation->error_syscall = "write";
                break;
            }
            offset += (size_t)write_count;
        }
    }
    if (JS_FS_SERVICE_CLOSE(destination_descriptor) != 0 && operation->error_number == 0) {
        operation->error_number = errno;
        operation->error_syscall = "close";
    }
    if (JS_FS_SERVICE_CLOSE(source_descriptor) != 0 && operation->error_number == 0) {
        operation->error_number = errno;
        operation->error_syscall = "close";
    }
    free(buffer);
    return operation->error_number == 0;
}

bool js_node_fs_path_operation(JubeNodeFilesystemPathOperation* operation) {
    if (!operation || !operation->path) return false;
    operation->error_number = 0;
    operation->error_syscall = NULL;
    int result = -1;
    switch (operation->mode) {
    case JUBE_NODE_FILESYSTEM_PATH_ACCESS:
        result = JS_FS_SERVICE_ACCESS(operation->path, (int)operation->numeric_value);
        operation->error_syscall = "access";
        break;
    case JUBE_NODE_FILESYSTEM_PATH_CHMOD:
        result = JS_FS_SERVICE_CHMOD(operation->path, (int)operation->numeric_value);
        operation->error_syscall = "chmod";
        break;
    case JUBE_NODE_FILESYSTEM_PATH_UNLINK:
        result = JS_FS_SERVICE_UNLINK(operation->path);
        operation->error_syscall = "unlink";
        break;
    case JUBE_NODE_FILESYSTEM_PATH_RMDIR:
        result = JS_FS_SERVICE_RMDIR(operation->path);
        operation->error_syscall = "rmdir";
        break;
    case JUBE_NODE_FILESYSTEM_PATH_RENAME:
        if (!operation->secondary_path) return false;
        result = rename(operation->path, operation->secondary_path);
        operation->error_syscall = "rename";
        break;
    case JUBE_NODE_FILESYSTEM_PATH_MKDIR: {
        result = 0;
        if (operation->recursive) {
            size_t path_length = strlen(operation->path);
            char* path_copy = (char*)malloc(path_length + 1);
            if (!path_copy) {
                operation->error_number = ENOMEM;
                operation->error_syscall = "mkdir";
                return false;
            }
            memcpy(path_copy, operation->path, path_length + 1);
            for (char* part = path_copy + 1; *part; ++part) {
                if (*part != '/') continue;
                *part = '\0';
                if (JS_FS_SERVICE_MKDIR(path_copy, (int)operation->numeric_value) != 0 && errno != EEXIST) {
                    result = -1;
                    *part = '/';
                    break;
                }
                *part = '/';
            }
            if (result == 0 && JS_FS_SERVICE_MKDIR(path_copy, (int)operation->numeric_value) != 0 && errno != EEXIST) {
                result = -1;
            }
            free(path_copy);
        } else {
            result = JS_FS_SERVICE_MKDIR(operation->path, (int)operation->numeric_value);
        }
        operation->error_syscall = "mkdir";
        break;
    }
    case JUBE_NODE_FILESYSTEM_PATH_TRUNCATE:
#if defined(_WIN32)
    {
        int descriptor = JS_FS_SERVICE_OPEN(operation->path, O_WRONLY | O_BINARY, 0);
        if (descriptor >= 0) {
            result = _chsize_s(descriptor, (size_t)operation->numeric_value);
            if (result == 0 && JS_FS_SERVICE_CLOSE(descriptor) != 0) result = -1;
        }
        if (result != 0 && errno == 0) operation->error_number = result;
        operation->error_syscall = "truncate";
        break;
    }
#else
        result = truncate(operation->path, (off_t)operation->numeric_value);
        operation->error_syscall = "truncate";
        break;
#endif
    case JUBE_NODE_FILESYSTEM_PATH_RM:
        if (operation->recursive) result = file_delete_recursive(operation->path);
        else {
            result = JS_FS_SERVICE_UNLINK(operation->path);
            if (result != 0) result = JS_FS_SERVICE_RMDIR(operation->path);
        }
        operation->error_syscall = "rm";
        break;
    case JUBE_NODE_FILESYSTEM_PATH_LINK:
        if (!operation->secondary_path) return false;
        result = JS_FS_SERVICE_LINK(operation->path, operation->secondary_path);
        operation->error_syscall = "link";
        break;
    case JUBE_NODE_FILESYSTEM_PATH_SYMLINK:
        if (!operation->secondary_path) return false;
        result = file_symlink(operation->path, operation->secondary_path);
        operation->error_syscall = "symlink";
        break;
    case JUBE_NODE_FILESYSTEM_PATH_CHOWN:
#if defined(_WIN32)
        result = js_fs_service_unsupported();
#else
        result = chown(operation->path, (uid_t)operation->numeric_value,
                       (gid_t)operation->secondary_numeric_value);
#endif
        operation->error_syscall = "chown";
        break;
    case JUBE_NODE_FILESYSTEM_PATH_LCHOWN:
#if defined(_WIN32)
        result = js_fs_service_unsupported();
#else
        result = lchown(operation->path, (uid_t)operation->numeric_value,
                        (gid_t)operation->secondary_numeric_value);
#endif
        operation->error_syscall = "lchown";
        break;
    case JUBE_NODE_FILESYSTEM_PATH_LCHMOD:
#if defined(__APPLE__)
        result = lchmod(operation->path, (mode_t)operation->numeric_value);
#elif defined(_WIN32)
        result = js_fs_service_unsupported();
#else
        result = JS_FS_SERVICE_CHMOD(operation->path, (int)operation->numeric_value);
#endif
        operation->error_syscall = "lchmod";
        break;
    }
    if (result != 0) {
        if (operation->error_number == 0) operation->error_number = errno ? errno : EIO;
        return false;
    }
    return true;
}

bool js_node_fs_string_operation(JubeNodeFilesystemStringOperation* operation) {
    if (!operation || !operation->path) return false;
    operation->output = NULL;
    operation->output_length = 0;
    operation->error_number = 0;
    operation->error_syscall = NULL;
    switch (operation->mode) {
    case JUBE_NODE_FILESYSTEM_STRING_REALPATH:
#if defined(_WIN32)
        operation->output = _fullpath(NULL, operation->path, 0);
#else
        operation->output = realpath(operation->path, NULL);
#endif
        operation->error_syscall = "lstat";
        if (operation->output) operation->output_length = strlen(operation->output);
        break;
    case JUBE_NODE_FILESYSTEM_STRING_MKDTEMP: {
        size_t prefix_length = strlen(operation->path);
        char* path = (char*)malloc(prefix_length + 7);
        if (!path) {
            operation->error_number = ENOMEM;
            operation->error_syscall = "mkdtemp";
            return false;
        }
        memcpy(path, operation->path, prefix_length);
        memcpy(path + prefix_length, "XXXXXX", 7);
#if defined(_WIN32)
        int result = _mktemp_s(path, prefix_length + 7);
        if (result == 0 && JS_FS_SERVICE_MKDIR(path, 0700) != 0) result = errno;
        if (result != 0) {
            operation->error_number = result;
            free(path);
        } else {
            operation->output = path;
            operation->output_length = prefix_length + 6;
        }
#else
        if (mkdtemp(path)) {
            operation->output = path;
            operation->output_length = strlen(path);
        } else free(path);
#endif
        operation->error_syscall = "mkdtemp";
        break;
    }
    case JUBE_NODE_FILESYSTEM_STRING_READLINK:
#if defined(_WIN32)
        operation->error_number = ENOSYS;
        operation->error_syscall = "readlink";
        break;
#else
    {
        size_t capacity = 256;
        char* target = (char*)malloc(capacity);
        if (!target) {
            operation->error_number = ENOMEM;
            operation->error_syscall = "readlink";
            break;
        }
        ssize_t length = -1;
        while (capacity <= 1024 * 1024) {
            length = readlink(operation->path, target, capacity);
            if (length < 0 || (size_t)length < capacity) break;
            size_t next_capacity = capacity * 2;
            char* resized = (char*)realloc(target, next_capacity);
            if (!resized) {
                operation->error_number = ENOMEM;
                break;
            }
            target = resized;
            capacity = next_capacity;
        }
        operation->error_syscall = "readlink";
        if (length < 0 || (size_t)length == capacity) {
            if (operation->error_number == 0) {
                operation->error_number = length < 0 ? errno : ENAMETOOLONG;
            }
            free(target);
        } else {
            operation->output = target;
            operation->output_length = (size_t)length;
        }
        break;
    }
#endif
    }
    if (!operation->output) {
        if (operation->error_number == 0) operation->error_number = errno ? errno : EIO;
        return false;
    }
    return true;
}

void js_node_fs_string_operation_release(JubeNodeFilesystemStringOperation* operation) {
    if (!operation) return;
    free(operation->output);
    operation->output = NULL;
    operation->output_length = 0;
}

void js_node_fs_directory_read_release(JubeNodeFilesystemDirectoryOperation* operation) {
    if (!operation) return;
    for (size_t index = 0; index < operation->entry_count; ++index) free(operation->entries[index]);
    free(operation->entries);
    operation->entries = NULL;
    operation->entry_count = 0;
}

static bool js_node_fs_directory_append(JubeNodeFilesystemDirectoryOperation* operation,
                                        const char* name) {
    if (!operation || !name) return false;
    size_t name_length = strlen(name);
    char* name_copy = (char*)malloc(name_length + 1);
    if (!name_copy) return false;
    memcpy(name_copy, name, name_length + 1);
    char** grown_entries = (char**)realloc(operation->entries,
                                           (operation->entry_count + 1) * sizeof(char*));
    if (!grown_entries) {
        free(name_copy);
        return false;
    }
    operation->entries = grown_entries;
    operation->entries[operation->entry_count++] = name_copy;
    return true;
}

bool js_node_fs_directory_read(JubeNodeFilesystemDirectoryOperation* operation) {
    if (!operation || !operation->path) return false;
    operation->entries = NULL;
    operation->entry_count = 0;
    operation->error_number = 0;
    operation->error_syscall = "scandir";
#if defined(_WIN32)
    size_t path_length = strlen(operation->path);
    char* pattern = (char*)malloc(path_length + 3);
    if (!pattern) {
        operation->error_number = ENOMEM;
        return false;
    }
    memcpy(pattern, operation->path, path_length);
    if (path_length > 0 && pattern[path_length - 1] != '/' && pattern[path_length - 1] != '\\') {
        pattern[path_length++] = '\\';
    }
    pattern[path_length++] = '*';
    pattern[path_length] = '\0';
    WIN32_FIND_DATAA entry = {};
    HANDLE directory = FindFirstFileA(pattern, &entry);
    free(pattern);
    if (directory == INVALID_HANDLE_VALUE) {
        operation->error_number = errno ? errno : ENOENT;
        return false;
    }
    bool success = true;
    do {
        if (strcmp(entry.cFileName, ".") != 0 && strcmp(entry.cFileName, "..") != 0 &&
                !js_node_fs_directory_append(operation, entry.cFileName)) {
            success = false;
            operation->error_number = ENOMEM;
            break;
        }
    } while (FindNextFileA(directory, &entry));
    FindClose(directory);
#else
    DIR* directory = opendir(operation->path);
    if (!directory) {
        operation->error_number = errno;
        return false;
    }
    bool success = true;
    struct dirent* entry = NULL;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0 &&
                !js_node_fs_directory_append(operation, entry->d_name)) {
            success = false;
            operation->error_number = ENOMEM;
            break;
        }
    }
    closedir(directory);
#endif
    if (!success) {
        js_node_fs_directory_read_release(operation);
        return false;
    }
    return true;
}

bool js_node_fs_descriptor_operation(JubeNodeFilesystemDescriptorOperation* operation) {
    if (!operation) return false;
    operation->error_number = 0;
    operation->error_syscall = NULL;
    int result = -1;
    switch (operation->mode) {
    case JUBE_NODE_FILESYSTEM_DESCRIPTOR_OPEN:
        if (!operation->path) return false;
        result = JS_FS_SERVICE_OPEN(operation->path, operation->flags, operation->mode_value);
        operation->descriptor = result;
        operation->error_syscall = "open";
        break;
    case JUBE_NODE_FILESYSTEM_DESCRIPTOR_CLOSE:
        result = JS_FS_SERVICE_CLOSE(operation->descriptor);
        operation->error_syscall = "close";
        break;
    case JUBE_NODE_FILESYSTEM_DESCRIPTOR_FCHMOD:
        result = JS_FS_SERVICE_FCHMOD(operation->descriptor, operation->mode_value);
        operation->error_syscall = "fchmod";
        break;
    case JUBE_NODE_FILESYSTEM_DESCRIPTOR_READ:
    case JUBE_NODE_FILESYSTEM_DESCRIPTOR_WRITE: {
        bool is_write = operation->mode == JUBE_NODE_FILESYSTEM_DESCRIPTOR_WRITE;
        if (!operation->bytes && operation->byte_length > 0) return false;
#if defined(_WIN32)
        if (operation->position >= 0 && _lseeki64(operation->descriptor, operation->position, SEEK_SET) < 0) {
            result = -1;
        } else {
            result = is_write ? JS_FS_SERVICE_WRITE(operation->descriptor, operation->bytes,
                                                 operation->byte_length) :
                             JS_FS_SERVICE_READ(operation->descriptor, operation->bytes,
                                                operation->byte_length);
        }
#else
        result = operation->position >= 0 ?
            (is_write ? (int)pwrite(operation->descriptor, operation->bytes, operation->byte_length,
                                 (off_t)operation->position) :
                     (int)pread(operation->descriptor, operation->bytes, operation->byte_length,
                                (off_t)operation->position)) :
            (is_write ? (int)JS_FS_SERVICE_WRITE(operation->descriptor, operation->bytes,
                                              operation->byte_length) :
                     (int)JS_FS_SERVICE_READ(operation->descriptor, operation->bytes,
                                             operation->byte_length));
#endif
        if (result >= 0) operation->transferred = (size_t)result;
        operation->error_syscall = is_write ? "write" : "read";
        break;
    }
    case JUBE_NODE_FILESYSTEM_DESCRIPTOR_FCHOWN:
#if defined(_WIN32)
        result = js_fs_service_unsupported();
#else
        result = fchown(operation->descriptor, (uid_t)operation->mode_value,
                        (gid_t)operation->secondary_mode_value);
#endif
        operation->error_syscall = "fchown";
        break;
    }
    if (result < 0) {
        operation->error_number = errno ? errno : EIO;
        return false;
    }
    return true;
}

static int64_t js_node_fs_metadata_time_millis(const JsFsServiceStat* value, int which) {
#if defined(_WIN32)
    int64_t seconds = which == 0 ? (int64_t)value->st_atime :
        which == 1 ? (int64_t)value->st_mtime : (int64_t)value->st_ctime;
    return seconds * 1000;
#elif defined(__APPLE__)
    const struct timespec* time = which == 0 ? &value->st_atimespec :
        which == 1 ? &value->st_mtimespec : which == 2 ? &value->st_ctimespec :
        &value->st_birthtimespec;
    return (int64_t)time->tv_sec * 1000 + (int64_t)(time->tv_nsec / 1000000);
#else
    const struct timespec* time = which == 0 ? &value->st_atim :
        which == 1 ? &value->st_mtim : &value->st_ctim;
    return (int64_t)time->tv_sec * 1000 + (int64_t)(time->tv_nsec / 1000000);
#endif
}

static void js_node_fs_metadata_copy(JubeNodeFilesystemMetadata* out, const JsFsServiceStat* value) {
    if (!out || !value) return;
    memset(out, 0, sizeof(*out));
    out->mode = (uint64_t)value->st_mode;
    out->size = (uint64_t)value->st_size;
    out->ino = (uint64_t)value->st_ino;
    out->nlink = (uint64_t)value->st_nlink;
    out->dev = (uint64_t)value->st_dev;
    out->atime_millis = js_node_fs_metadata_time_millis(value, 0);
    out->mtime_millis = js_node_fs_metadata_time_millis(value, 1);
    out->ctime_millis = js_node_fs_metadata_time_millis(value, 2);
    out->birthtime_millis = js_node_fs_metadata_time_millis(value, 3);
#if !defined(_WIN32)
    out->uid = (uint64_t)value->st_uid;
    out->gid = (uint64_t)value->st_gid;
    out->rdev = (uint64_t)value->st_rdev;
    out->blksize = (uint64_t)value->st_blksize;
    out->blocks = (uint64_t)value->st_blocks;
#endif
}

bool js_node_fs_metadata_operation(JubeNodeFilesystemMetadataOperation* operation) {
    if (!operation) return false;
    JsFsServiceStat value = {};
    int result = -1;
    switch (operation->mode) {
    case JUBE_NODE_FILESYSTEM_METADATA_STAT:
        if (!operation->path) return false;
        result = JS_FS_SERVICE_STAT(operation->path, &value);
        operation->error_syscall = "stat";
        break;
    case JUBE_NODE_FILESYSTEM_METADATA_LSTAT:
        if (!operation->path) return false;
        result = JS_FS_SERVICE_LSTAT(operation->path, &value);
        operation->error_syscall = "lstat";
        break;
    case JUBE_NODE_FILESYSTEM_METADATA_FSTAT:
        result = JS_FS_SERVICE_FSTAT(operation->descriptor, &value);
        operation->error_syscall = "fstat";
        break;
    }
    if (result != 0) {
        operation->error_number = errno ? errno : EIO;
        return false;
    }
    js_node_fs_metadata_copy(&operation->value, &value);
    return true;
}

bool js_node_fs_statfs_operation(JubeNodeFilesystemStatfsOperation* operation) {
    if (!operation || !operation->path) return false;
    operation->type = 0;
    operation->bsize = 4096;
    operation->frsize = 4096;
    operation->blocks = 0;
    operation->bfree = 0;
    operation->bavail = 0;
    operation->files = 0;
    operation->ffree = 0;
    operation->error_number = 0;
    operation->error_syscall = "statfs";
#if !defined(_WIN32)
    struct statvfs value = {};
    if (statvfs(operation->path, &value) != 0) {
        operation->error_number = errno;
        return false;
    }
    operation->bsize = (uint64_t)value.f_bsize;
    operation->frsize = value.f_frsize ? (uint64_t)value.f_frsize : operation->bsize;
    operation->blocks = (uint64_t)value.f_blocks;
    operation->bfree = (uint64_t)value.f_bfree;
    operation->bavail = (uint64_t)value.f_bavail;
    operation->files = (uint64_t)value.f_files;
    operation->ffree = (uint64_t)value.f_ffree;
#endif
    return true;
}

static Item js_fs_service_copy_path(Item value, char** out_path) {
    if (!out_path) return ItemError;
    *out_path = NULL;
    RootFrame roots(1);
    Rooted<Item> path_root(roots, js_to_string(value));
    if (item_is_error(path_root.get())) return path_root.get();
    if (get_type_id(path_root.get()) != LMD_TYPE_STRING) {
        return js_throw_invalid_arg_type("path", "string", path_root.get());
    }
    String* path = it2s(path_root.get());
    if (!path || !(*out_path = str_dup(path->chars, path->len))) return ItemError;
    return make_js_undefined();
}

static bool js_fs_service_event_is(Item value, const char* expected) {
    if (get_type_id(value) != LMD_TYPE_STRING || !expected) return false;
    String* name = it2s(value);
    size_t expected_length = strlen(expected);
    return name && name->len == expected_length &&
        memcmp(name->chars, expected, expected_length) == 0;
}

static void js_fs_service_stream_emit(Item stream, const char* event,
                                      Item value, bool has_value) {
    RootFrame roots(6);
    Rooted<Item> stream_root(roots, stream);
    Rooted<Item> listeners_root(roots,
        js_get_key_cstr(stream_root.get(), "__fs_stream_listeners__"));
    Rooted<Item> event_key_root(roots, js_make_string(event));
    Rooted<Item> callbacks_root(roots, ItemNull);
    Rooted<Item> callback_root(roots, ItemNull);
    Rooted<Item> value_root(roots, value);
    if (get_type_id(listeners_root.get()) != LMD_TYPE_MAP) return;
    callbacks_root.set(js_get_key_default(listeners_root.get(), event_key_root.get()));
    if (get_type_id(callbacks_root.get()) != LMD_TYPE_ARRAY) return;
    int64_t count = js_array_length(callbacks_root.get());
    for (int64_t index = 0; index < count; index++) {
        callback_root.set(js_elements_get_int(callbacks_root.get(), index));
        if (!js_is_callable(callback_root.get())) continue;
        if (has_value) {
            Item args[1] = {value_root.get()};
            (void)js_call_function(callback_root.get(), stream_root.get(), args, 1);
        } else {
            (void)js_call_function(callback_root.get(), stream_root.get(), NULL, 0);
        }
    }
}

static Item js_fs_service_stream_on(Item event, Item callback) {
    RootFrame roots(5);
    Rooted<Item> stream_root(roots, js_get_this());
    Rooted<Item> event_root(roots, event);
    Rooted<Item> callback_root(roots, callback);
    Rooted<Item> listeners_root(roots,
        js_get_key_cstr(stream_root.get(), "__fs_stream_listeners__"));
    Rooted<Item> callbacks_root(roots, ItemNull);
    if (get_type_id(event_root.get()) != LMD_TYPE_STRING || !js_is_callable(callback_root.get())) {
        return stream_root.get();
    }
    if (get_type_id(listeners_root.get()) != LMD_TYPE_MAP) {
        listeners_root.set(js_new_object());
        js_set_key_cstr(stream_root.get(), "__fs_stream_listeners__", listeners_root.get());
    }
    callbacks_root.set(js_get_key_default(listeners_root.get(), event_root.get()));
    if (get_type_id(callbacks_root.get()) != LMD_TYPE_ARRAY) {
        callbacks_root.set(js_array_new(0));
        js_set_key_default(listeners_root.get(), event_root.get(), callbacks_root.get());
    }
    js_array_push(callbacks_root.get(), callback_root.get());
    if (js_fs_service_event_is(event_root.get(), "finish") &&
            js_is_truthy(js_get_key_cstr(stream_root.get(), "__fs_stream_finished__")) &&
            !js_is_truthy(js_get_key_cstr(stream_root.get(), "__fs_stream_finish_emitted__"))) {
        js_set_key_cstr(stream_root.get(), "__fs_stream_finish_emitted__", (Item){.item = b2it(true)});
        (void)js_call_function(callback_root.get(), stream_root.get(), NULL, 0);
    }
    return stream_root.get();
}

static Item js_fs_service_read_stream_emit(Item env_item) {
    JS_ENV_OR_UNDEFINED(env, env_item);
    RootFrame roots(2);
    Rooted<Item> stream_root(roots, env[0]);
    Rooted<Item> data_root(roots,
        js_get_key_cstr(stream_root.get(), "__fs_stream_data__"));
    js_fs_service_stream_emit(stream_root.get(), "data", data_root.get(), true);
    js_fs_service_stream_emit(stream_root.get(), "end", ItemNull, false);
    return make_js_undefined();
}

static Item js_fs_service_write_stream_finish(Item env_item) {
    JS_ENV_OR_UNDEFINED(env, env_item);
    RootFrame roots(1);
    Rooted<Item> stream_root(roots, env[0]);
    if (js_is_truthy(js_get_key_cstr(stream_root.get(), "__fs_stream_finish_emitted__"))) {
        return make_js_undefined();
    }
    js_set_key_cstr(stream_root.get(), "__fs_stream_finish_emitted__", (Item){.item = b2it(true)});
    js_fs_service_stream_emit(stream_root.get(), "finish", ItemNull, false);
    return make_js_undefined();
}

static Item js_fs_service_write_stream_write(Item chunk, Item callback) {
    RootFrame roots(4);
    Rooted<Item> stream_root(roots, js_get_this());
    Rooted<Item> chunk_root(roots, chunk);
    Rooted<Item> callback_root(roots, callback);
    Rooted<Item> path_root(roots,
        js_get_key_cstr(stream_root.get(), "__fs_stream_path__"));
    const char* bytes = NULL;
    int length = 0;
    if (!js_item_bytes(chunk_root.get(), &bytes, &length)) {
        return js_throw_invalid_arg_type("chunk", "string or Buffer", chunk_root.get());
    }
    char* path = NULL;
    Item path_status = js_fs_service_copy_path(path_root.get(), &path);
    if (item_is_error(path_status)) return path_status;

    bool started = js_is_truthy(js_get_key_cstr(stream_root.get(), "__fs_stream_started__"));
    JubeNodeFilesystemReadWrite operation = {};
    operation.mode = started ? JUBE_NODE_FILESYSTEM_APPEND : JUBE_NODE_FILESYSTEM_WRITE;
    operation.path = path;
    operation.input = (const uint8_t*)bytes;
    operation.input_length = (size_t)length;
    bool written = js_node_fs_read_write(&operation);
    free(path);
    if (!written) return js_node_throw_system_error(operation.error_syscall, operation.error_number);

    js_set_key_cstr(stream_root.get(), "__fs_stream_started__", (Item){.item = b2it(true)});
    Item bytes_written = js_get_key_cstr(stream_root.get(), "bytesWritten");
    int64_t total = get_type_id(bytes_written) == LMD_TYPE_INT ? it2i(bytes_written) : 0;
    js_set_key_cstr(stream_root.get(), "bytesWritten", (Item){.item = i2it(total + length)});
    if (js_is_callable(callback_root.get())) {
        (void)js_call_function(callback_root.get(), stream_root.get(), NULL, 0);
    }
    return (Item){.item = b2it(true)};
}

static Item js_fs_service_write_stream_end(Item chunk) {
    RootFrame roots(3);
    Rooted<Item> stream_root(roots, js_get_this());
    Rooted<Item> chunk_root(roots, chunk);
    Rooted<Item> finish_root(roots, ItemNull);
    TypeId chunk_type = get_type_id(chunk_root.get());
    if (chunk_root.get().item != ITEM_NULL && chunk_type != LMD_TYPE_UNDEFINED) {
        Item write_result = js_fs_service_write_stream_write(chunk_root.get(), make_js_undefined());
        if (item_is_error(write_result)) return write_result;
    }
    js_set_key_cstr(stream_root.get(), "__fs_stream_finished__", (Item){.item = b2it(true)});
    Item* finish_env = js_alloc_env1(stream_root.get());
    if (!finish_env) return ItemError;
    finish_root.set(js_new_native_closure(js_fs_service_write_stream_finish, 0, finish_env, 1));
    if (item_is_error(finish_root.get())) return finish_root.get();
    js_next_tick_enqueue(finish_root.get());
    return stream_root.get();
}

Item js_node_fs_read_stream_new(Item path, Item options) {
    (void)options;
    char* path_chars = NULL;
    Item path_status = js_fs_service_copy_path(path, &path_chars);
    if (item_is_error(path_status)) return path_status;
    JubeNodeFilesystemReadWrite operation = {};
    operation.mode = JUBE_NODE_FILESYSTEM_READ;
    operation.path = path_chars;
    bool read = js_node_fs_read_write(&operation);
    free(path_chars);
    if (!read) return js_node_throw_system_error(operation.error_syscall, operation.error_number);

    RootFrame roots(4);
    Rooted<Item> stream_root(roots, js_new_object_with_class(JS_CLASS_READABLE_STREAM));
    Rooted<Item> data_root(roots,
        js_buffer_from_bytes((const char*)operation.output, (int)operation.output_length));
    Rooted<Item> emit_root(roots, ItemNull);
    Rooted<Item> listeners_root(roots, ItemNull);
    js_node_fs_read_write_release(&operation);
    if (item_is_error(data_root.get())) return data_root.get();
    js_set_key_cstr(stream_root.get(), "__fs_stream_data__", data_root.get());
    listeners_root.set(js_new_object());
    js_set_key_cstr(stream_root.get(), "__fs_stream_listeners__", listeners_root.get());
    js_install_native_method(stream_root.get(), "on", js_fs_service_stream_on);
    Item* emit_env = js_alloc_env1(stream_root.get());
    if (!emit_env) return ItemError;
    emit_root.set(js_new_native_closure(js_fs_service_read_stream_emit, 0, emit_env, 1));
    if (item_is_error(emit_root.get())) return emit_root.get();
    js_next_tick_enqueue(emit_root.get());
    return stream_root.get();
}

Item js_node_fs_write_stream_new(Item path, Item options) {
    (void)options;
    RootFrame roots(3);
    Rooted<Item> path_root(roots, path);
    Rooted<Item> stream_root(roots, js_new_object_with_class(JS_CLASS_WRITABLE_STREAM));
    Rooted<Item> listeners_root(roots, js_new_object());
    js_set_key_cstr(stream_root.get(), "__fs_stream_path__", path_root.get());
    js_set_key_cstr(stream_root.get(), "__fs_stream_listeners__", listeners_root.get());
    js_set_key_cstr(stream_root.get(), "__fs_stream_started__", (Item){.item = b2it(false)});
    js_set_key_cstr(stream_root.get(), "__fs_stream_finished__", (Item){.item = b2it(false)});
    js_set_key_cstr(stream_root.get(), "__fs_stream_finish_emitted__", (Item){.item = b2it(false)});
    js_set_key_cstr(stream_root.get(), "bytesWritten", (Item){.item = i2it(0)});
    js_install_native_method(stream_root.get(), "on", js_fs_service_stream_on);
    js_install_native_method(stream_root.get(), "write", js_fs_service_write_stream_write);
    js_install_native_method(stream_root.get(), "end", js_fs_service_write_stream_end);
    return stream_root.get();
}
