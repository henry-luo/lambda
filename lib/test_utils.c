// test_utils.c — pure-C helpers (no Lambda runtime deps).
// See lib/test_utils.h for the API contract.
//
// The companion file lib/test_utils_runtime.cpp owns the Pool+Heap+EvalContext
// fixture; tests that only need temp files / process spawn / string utils can
// link this .c file alone.

#include "test_utils.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

// -----------------------------------------------------------------------------
// Internal: build "./temp/<base>_<pid>_<epoch>[.<ext>]". Caller frees.
// -----------------------------------------------------------------------------
static char* make_temp_path(const char* base, const char* ext) {
    if (!base) return NULL;

    // ./temp exists by convention; create on demand defensively. mkdir is a
    // no-op if it already exists (EEXIST swallowed). If it failed for another
    // reason, callers will get a useful error from the subsequent open/fopen.
    (void)mkdir("./temp", 0755);

    size_t cap = 256;
    char* path = (char*)malloc(cap);
    if (!path) return NULL;

    long pid = (long)getpid();
    long epoch = (long)time(NULL);

    if (ext && *ext) {
        snprintf(path, cap, "./temp/%s_%ld_%ld.%s", base, pid, epoch, ext);
    } else {
        snprintf(path, cap, "./temp/%s_%ld_%ld", base, pid, epoch);
    }
    return path;
}

// =============================================================================
// Temp dir / file helpers
// =============================================================================

char* tu_mkdtemp(const char* base) {
    char* path = make_temp_path(base, NULL);
    if (!path) return NULL;

    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        free(path);
        return NULL;
    }
    return path;
}

void tu_rmtree(const char* path) {
    if (!path || !*path) return;

    // Refuse anything not under ./temp/ — tests should never delete
    // arbitrary paths via this helper.
    if (strncmp(path, "./temp/", 7) != 0 && strncmp(path, "temp/", 5) != 0) {
        return;
    }

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", path);
    int rc = system(cmd);
    (void)rc;  // best-effort
}

char* tu_write_temp(const char* base, const char* ext, const char* content) {
    char* path = make_temp_path(base, ext);
    if (!path) return NULL;

    FILE* f = fopen(path, "wb");
    if (!f) {
        free(path);
        return NULL;
    }
    if (content && *content) {
        size_t len = strlen(content);
        if (fwrite(content, 1, len, f) != len) {
            fclose(f);
            free(path);
            return NULL;
        }
    }
    fclose(f);
    return path;
}

// =============================================================================
// File I/O
// =============================================================================

char* tu_slurp(const char* path) {
    if (!path) return NULL;
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    rewind(f);

    char* buf = (char*)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }

    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[got] = '\0';
    return buf;
}

bool tu_exists(const char* path) {
    if (!path) return false;
    struct stat st;
    return stat(path, &st) == 0;
}

// =============================================================================
// Process spawn
// =============================================================================

char* tu_run(const char* cmd, int* exit_code) {
    if (!cmd) return NULL;

    // Capture combined stdout+stderr.
    char shell_cmd[4096];
    snprintf(shell_cmd, sizeof(shell_cmd), "%s 2>&1", cmd);

    FILE* p = popen(shell_cmd, "r");
    if (!p) return NULL;

    size_t cap = 4096;
    size_t len = 0;
    char* buf = (char*)malloc(cap);
    if (!buf) { pclose(p); return NULL; }

    char chunk[1024];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), p)) > 0) {
        if (len + n + 1 > cap) {
            while (len + n + 1 > cap) cap *= 2;
            char* nb = (char*)realloc(buf, cap);
            if (!nb) { free(buf); pclose(p); return NULL; }
            buf = nb;
        }
        memcpy(buf + len, chunk, n);
        len += n;
    }
    buf[len] = '\0';

    int rc = pclose(p);
    if (exit_code) {
        *exit_code = WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
    }
    return buf;
}

// =============================================================================
// String normalization
// =============================================================================

void tu_trim_trailing(char* s) {
    if (!s) return;
    size_t n = strlen(s);
    while (n > 0) {
        unsigned char c = (unsigned char)s[n - 1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            s[--n] = '\0';
        } else {
            break;
        }
    }
}

void tu_strip_lines(char* s, const char* prefix) {
    if (!s || !prefix || !*prefix) return;
    size_t plen = strlen(prefix);

    char* read = s;
    char* write = s;
    while (*read) {
        // Find end of current line (inclusive of newline).
        char* eol = strchr(read, '\n');
        size_t line_len = eol ? (size_t)(eol - read + 1) : strlen(read);

        bool drop = (line_len >= plen && memcmp(read, prefix, plen) == 0);
        if (!drop) {
            if (write != read) memmove(write, read, line_len);
            write += line_len;
        }
        read += line_len;
    }
    *write = '\0';
}
