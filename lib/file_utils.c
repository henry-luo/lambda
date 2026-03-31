// file_utils.c
// File system utility functions — directory operations, glob, find

#ifndef _WIN32
  #define _GNU_SOURCE
#endif

#include "file_utils.h"
#include "file.h"
#include "arraylist.h"
#include "log.h"

#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#ifdef _WIN32
  #include <direct.h>
  #include <io.h>
  #include <windows.h>
  #define mkdir(path, mode) _mkdir(path)
  #define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
  #define S_ISLNK(m) (0)
#else
  #include <sys/types.h>
  #include <dirent.h>
  #include <fnmatch.h>
  #include <glob.h>
  #include <unistd.h>
#endif

extern char* strdup(const char* s);

// Create directory recursively (like mkdir -p)
int create_dir_recursive(const char* path) {
    if (!path || !*path) {
        return -1;
    }

    char* path_copy = strdup(path);
    if (!path_copy) {
        return -1;
    }

    char* p = path_copy;
    struct stat st;  // Declare stat buffer here
    
    // Skip leading slash
    if (*p == '/') {
        p++;
    }

    for (; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            
            // Check if directory exists
            if (stat(path_copy, &st) != 0) {
                // Create directory
                if (mkdir(path_copy, 0755) != 0 && errno != EEXIST) {
                    free(path_copy);
                    return -1;
                }
            }
            
            *p = '/';
        }
    }

    // Create final directory
    if (stat(path_copy, &st) != 0) {
        if (mkdir(path_copy, 0755) != 0 && errno != EEXIST) {
            free(path_copy);
            return -1;
        }
    }

    free(path_copy);
    return 0;
}

// ---------------------------------------------------------------------------
// DirEntry helpers
// ---------------------------------------------------------------------------

void dir_entry_free(DirEntry* entry) {
    if (!entry) return;
    free(entry->name);
    free(entry);
}

// ---------------------------------------------------------------------------
// dir_list — list immediate children of a directory
// ---------------------------------------------------------------------------

#ifdef _WIN32

ArrayList* dir_list(const char* dir_path) {
    if (!dir_path) return NULL;

    size_t plen = strlen(dir_path);
    char* pattern = (char*)malloc(plen + 3);
    if (!pattern) return NULL;
    snprintf(pattern, plen + 3, "%s\\*", dir_path);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern, &fd);
    free(pattern);
    if (hFind == INVALID_HANDLE_VALUE) {
        log_error("dir_list: cannot open directory '%s'", dir_path);
        return NULL;
    }

    ArrayList* list = arraylist_new(16);
    if (!list) { FindClose(hFind); return NULL; }

    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
            continue;

        DirEntry* entry = (DirEntry*)calloc(1, sizeof(DirEntry));
        if (!entry) continue;
        entry->name = strdup(fd.cFileName);
        entry->is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        entry->is_symlink = (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        arraylist_append(list, entry);
    } while (FindNextFileA(hFind, &fd));

    FindClose(hFind);
    return list;
}

#else // POSIX

ArrayList* dir_list(const char* dir_path) {
    if (!dir_path) return NULL;

    DIR* dir = opendir(dir_path);
    if (!dir) {
        log_error("dir_list: cannot open directory '%s': %s", dir_path, strerror(errno));
        return NULL;
    }

    ArrayList* list = arraylist_new(16);
    if (!list) { closedir(dir); return NULL; }

    struct dirent* de;
    while ((de = readdir(dir)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;

        DirEntry* entry = (DirEntry*)calloc(1, sizeof(DirEntry));
        if (!entry) continue;
        entry->name = strdup(de->d_name);

        // determine type
#ifdef _DIRENT_HAVE_D_TYPE
        if (de->d_type != DT_UNKNOWN) {
            entry->is_dir = (de->d_type == DT_DIR);
            entry->is_symlink = (de->d_type == DT_LNK);
        } else
#endif
        {
            // fallback to stat
            size_t plen = strlen(dir_path) + 1 + strlen(de->d_name) + 1;
            char* full = (char*)malloc(plen);
            if (full) {
                snprintf(full, plen, "%s/%s", dir_path, de->d_name);
                struct stat st;
                if (lstat(full, &st) == 0) {
                    entry->is_dir = S_ISDIR(st.st_mode);
                    entry->is_symlink = S_ISLNK(st.st_mode);
                }
                free(full);
            }
        }
        arraylist_append(list, entry);
    }

    closedir(dir);
    return list;
}

#endif // _WIN32

// ---------------------------------------------------------------------------
// dir_walk — recursive depth-first traversal
// ---------------------------------------------------------------------------

static int dir_walk_recursive(const char* dir_path, FileWalkCallback cb,
                              void* user_data) {
    ArrayList* entries = dir_list(dir_path);
    if (!entries) return -1;

    int result = 0;
    for (int i = 0; i < entries->length; i++) {
        DirEntry* entry = (DirEntry*)entries->data[i];

        // build full path
        size_t plen = strlen(dir_path) + 1 + strlen(entry->name) + 1;
        char* full = (char*)malloc(plen);
        if (!full) { result = -1; break; }
        snprintf(full, plen, "%s/%s", dir_path, entry->name);

        bool should_descend = cb(full, entry->is_dir, user_data);

        if (entry->is_dir && should_descend) {
            if (dir_walk_recursive(full, cb, user_data) != 0) {
                free(full);
                result = -1;
                break;
            }
        }

        free(full);
    }

    // free entries
    for (int i = 0; i < entries->length; i++) {
        dir_entry_free((DirEntry*)entries->data[i]);
    }
    arraylist_free(entries);
    return result;
}

int dir_walk(const char* dir_path, FileWalkCallback cb, void* user_data) {
    if (!dir_path || !cb) {
        log_error("dir_walk: NULL argument");
        return -1;
    }
    return dir_walk_recursive(dir_path, cb, user_data);
}

// ---------------------------------------------------------------------------
// dir_delete — recursive delete (rm -rf)
// ---------------------------------------------------------------------------

static bool dir_delete_cb(const char* path, bool is_dir, void* user_data) {
    (void)user_data;
    // return true to descend into subdirectories (handled in post-order below)
    return true;
}

int dir_delete(const char* dir_path) {
    if (!dir_path) {
        log_error("dir_delete: NULL path");
        return -1;
    }

    ArrayList* entries = dir_list(dir_path);
    if (!entries) {
        // try to remove as file
        return remove(dir_path) == 0 ? 0 : -1;
    }

    int result = 0;
    for (int i = 0; i < entries->length; i++) {
        DirEntry* entry = (DirEntry*)entries->data[i];

        size_t plen = strlen(dir_path) + 1 + strlen(entry->name) + 1;
        char* full = (char*)malloc(plen);
        if (!full) { result = -1; break; }
        snprintf(full, plen, "%s/%s", dir_path, entry->name);

        if (entry->is_dir) {
            if (dir_delete(full) != 0) result = -1;
        } else {
            if (remove(full) != 0) {
                log_error("dir_delete: cannot remove '%s': %s", full, strerror(errno));
                result = -1;
            }
        }
        free(full);
    }

    for (int i = 0; i < entries->length; i++) {
        dir_entry_free((DirEntry*)entries->data[i]);
    }
    arraylist_free(entries);

    // remove the directory itself
    if (result == 0) {
#ifdef _WIN32
        if (RemoveDirectoryA(dir_path) == 0) {
            log_error("dir_delete: cannot remove directory '%s'", dir_path);
            result = -1;
        }
#else
        if (rmdir(dir_path) != 0) {
            log_error("dir_delete: rmdir '%s' failed: %s", dir_path, strerror(errno));
            result = -1;
        }
#endif
    }

    return result;
}

// ---------------------------------------------------------------------------
// dir_copy — recursive directory copy
// ---------------------------------------------------------------------------

int dir_copy(const char* src, const char* dst) {
    if (!src || !dst) {
        log_error("dir_copy: NULL argument");
        return -1;
    }

    // create destination directory
    if (!create_dir(dst)) {
        log_error("dir_copy: cannot create destination '%s'", dst);
        return -1;
    }

    ArrayList* entries = dir_list(src);
    if (!entries) return -1;

    int result = 0;
    FileCopyOptions opts = {true, true};

    for (int i = 0; i < entries->length; i++) {
        DirEntry* entry = (DirEntry*)entries->data[i];

        size_t slen = strlen(src) + 1 + strlen(entry->name) + 1;
        size_t dlen = strlen(dst) + 1 + strlen(entry->name) + 1;
        char* src_full = (char*)malloc(slen);
        char* dst_full = (char*)malloc(dlen);
        if (!src_full || !dst_full) {
            free(src_full); free(dst_full);
            result = -1; break;
        }
        snprintf(src_full, slen, "%s/%s", src, entry->name);
        snprintf(dst_full, dlen, "%s/%s", dst, entry->name);

        if (entry->is_dir) {
            if (dir_copy(src_full, dst_full) != 0) result = -1;
        } else {
            if (file_copy(src_full, dst_full, &opts) != 0) result = -1;
        }

        free(src_full);
        free(dst_full);
    }

    for (int i = 0; i < entries->length; i++) {
        dir_entry_free((DirEntry*)entries->data[i]);
    }
    arraylist_free(entries);
    return result;
}

// ---------------------------------------------------------------------------
// file_glob — expand glob pattern
// ---------------------------------------------------------------------------

#ifdef _WIN32

ArrayList* file_glob(const char* pattern) {
    if (!pattern) return NULL;

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return arraylist_new(0);

    ArrayList* list = arraylist_new(16);
    if (!list) { FindClose(hFind); return NULL; }

    // extract directory prefix from pattern
    const char* last_sep = strrchr(pattern, '\\');
    if (!last_sep) last_sep = strrchr(pattern, '/');
    size_t prefix_len = last_sep ? (size_t)(last_sep - pattern + 1) : 0;

    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
            continue;

        size_t need = prefix_len + strlen(fd.cFileName) + 1;
        char* path = (char*)malloc(need);
        if (!path) continue;
        if (prefix_len > 0) {
            memcpy(path, pattern, prefix_len);
            strcpy(path + prefix_len, fd.cFileName);
        } else {
            strcpy(path, fd.cFileName);
        }
        arraylist_append(list, path);
    } while (FindNextFileA(hFind, &fd));

    FindClose(hFind);
    return list;
}

#else // POSIX

ArrayList* file_glob(const char* pattern) {
    if (!pattern) return NULL;

    glob_t g;
    memset(&g, 0, sizeof(g));

    int ret = glob(pattern, GLOB_NOSORT | GLOB_TILDE, NULL, &g);

    ArrayList* list = arraylist_new(ret == 0 ? (int)g.gl_pathc : 0);
    if (!list) { globfree(&g); return NULL; }

    if (ret == 0) {
        for (size_t i = 0; i < g.gl_pathc; i++) {
            char* dup = strdup(g.gl_pathv[i]);
            if (dup) arraylist_append(list, dup);
        }
    }

    globfree(&g);
    return list;
}

#endif // _WIN32

// ---------------------------------------------------------------------------
// file_find — find files by name pattern
// ---------------------------------------------------------------------------

typedef struct {
    const char* name_pattern;
    ArrayList* results;
    bool recursive;
} FindCtx;

static bool file_find_cb(const char* path, bool is_dir, void* user_data) {
    FindCtx* ctx = (FindCtx*)user_data;

    if (!is_dir) {
        // extract basename for matching
        const char* base = strrchr(path, '/');
#ifdef _WIN32
        const char* base2 = strrchr(path, '\\');
        if (base2 && (!base || base2 > base)) base = base2;
#endif
        base = base ? base + 1 : path;

#ifdef _WIN32
        // simple wildcard matching for Windows
        // support only * wildcard
        bool match = true;
        const char* p = ctx->name_pattern;
        const char* s = base;
        while (*p && *s) {
            if (*p == '*') {
                p++;
                if (!*p) break; // trailing * matches all
                while (*s && *s != *p) s++;
            } else if (*p == *s || *p == '?') {
                p++; s++;
            } else {
                match = false;
                break;
            }
        }
        if (*p == '*') p++;
        if (*p) match = false;
#else
        bool match = (fnmatch(ctx->name_pattern, base, 0) == 0);
#endif
        if (match) {
            char* dup = strdup(path);
            if (dup) arraylist_append(ctx->results, dup);
        }
    }

    return ctx->recursive; // descend into subdirs only if recursive
}

ArrayList* file_find(const char* dir, const char* name_pattern, bool recursive) {
    if (!dir || !name_pattern) return NULL;

    FindCtx ctx;
    ctx.name_pattern = name_pattern;
    ctx.results = arraylist_new(16);
    ctx.recursive = recursive;
    if (!ctx.results) return NULL;

    dir_walk(dir, file_find_cb, &ctx);
    return ctx.results;
}
