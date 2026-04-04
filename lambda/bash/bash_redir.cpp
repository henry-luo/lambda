// bash_redir.cpp — Redirection & I/O Engine (Phase C — Module 7)
//
// Manages stderr routing, fd duplication, and I/O state save/restore.

#include "bash_redir.h"
#include "bash_runtime.h"
#include "../lambda-data.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"

#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

// ============================================================================
// I/O state
// ============================================================================

typedef struct IOState {
    BashStderrRoute stderr_route;
    char stderr_filename[512];
    int stderr_append;
} IOState;

#define IO_STACK_DEPTH 32

static IOState io_stack[IO_STACK_DEPTH];
static int io_stack_top = 0;

// current state
static BashStderrRoute current_stderr_route = BASH_STDERR_NORMAL;
static char current_stderr_filename[512] = {0};
static int current_stderr_append = 0;

// stderr capture buffer
static StrBuf* stderr_capture_buf = NULL;
static int stderr_capturing = 0;

// ============================================================================
// I/O state push/pop
// ============================================================================

extern "C" void bash_io_push(void) {
    if (io_stack_top >= IO_STACK_DEPTH) {
        log_error("bash_redir: I/O state stack overflow (depth %d)", IO_STACK_DEPTH);
        return;
    }
    IOState* state = &io_stack[io_stack_top++];
    state->stderr_route = current_stderr_route;
    memcpy(state->stderr_filename, current_stderr_filename, sizeof(current_stderr_filename));
    state->stderr_append = current_stderr_append;
}

extern "C" void bash_io_pop(void) {
    if (io_stack_top <= 0) {
        log_error("bash_redir: I/O state stack underflow");
        return;
    }

    // flush any pending stderr file capture before restoring state
    if (current_stderr_route == BASH_STDERR_TO_FILE && stderr_capturing) {
        bash_stderr_end_capture_to_file(current_stderr_filename, current_stderr_append);
    }

    IOState* state = &io_stack[--io_stack_top];
    current_stderr_route = state->stderr_route;
    memcpy(current_stderr_filename, state->stderr_filename, sizeof(current_stderr_filename));
    current_stderr_append = state->stderr_append;
}

// ============================================================================
// Stderr routing control
// ============================================================================

extern "C" void bash_stderr_set_route(BashStderrRoute route) {
    current_stderr_route = route;
}

extern "C" void bash_stderr_set_file(const char* filename, int append) {
    if (filename) {
        int len = (int)strlen(filename);
        if (len >= (int)sizeof(current_stderr_filename))
            len = (int)sizeof(current_stderr_filename) - 1;
        memcpy(current_stderr_filename, filename, len);
        current_stderr_filename[len] = '\0';
    }
    current_stderr_append = append;
}

extern "C" void bash_stderr_route_write(const char* data, int len) {
    if (!data || len <= 0) return;

    switch (current_stderr_route) {
    case BASH_STDERR_NORMAL:
        fwrite(data, 1, len, stderr);
        break;

    case BASH_STDERR_TO_NULL:
        // discard
        break;

    case BASH_STDERR_TO_STDOUT:
        // route through stdout (capture-aware)
        bash_raw_write(data, len);
        break;

    case BASH_STDERR_TO_FILE:
        // buffer into stderr capture
        if (stderr_capturing && stderr_capture_buf) {
            strbuf_append_str_n(stderr_capture_buf, data, (size_t)len);
        } else {
            // direct write to file (shouldn't normally happen — use capture)
            fwrite(data, 1, len, stderr);
        }
        break;
    }
}

// ============================================================================
// Stderr capture for file redirect
// ============================================================================

extern "C" void bash_stderr_begin_capture(void) {
    if (!stderr_capture_buf) {
        stderr_capture_buf = strbuf_new();
    } else {
        strbuf_reset(stderr_capture_buf);
    }
    stderr_capturing = 1;
}

extern "C" int bash_stderr_end_capture_to_file(const char* filename, int append) {
    stderr_capturing = 0;
    if (!stderr_capture_buf || !filename) return 1;

    // check for /dev/null — just discard
    if (strcmp(filename, "/dev/null") == 0) {
        strbuf_reset(stderr_capture_buf);
        return 0;
    }

    int flags = O_WRONLY | O_CREAT;
    flags |= append ? O_APPEND : O_TRUNC;
    int fd = open(filename, flags, 0644);
    if (fd < 0) {
        log_error("bash_redir: cannot open '%s' for stderr redirect: %s",
                  filename, strerror(errno));
        strbuf_reset(stderr_capture_buf);
        return 1;
    }

    StrBuf* buf = stderr_capture_buf;
    if (buf->length > 0) {
        write(fd, buf->str, buf->length);
    }
    close(fd);
    strbuf_reset(stderr_capture_buf);
    return 0;
}

// ============================================================================
// Combined redirect helpers
// ============================================================================

extern "C" void bash_redir_stderr_to_stdout(void) {
    current_stderr_route = BASH_STDERR_TO_STDOUT;
}

extern "C" void bash_redir_stderr_to_null(void) {
    current_stderr_route = BASH_STDERR_TO_NULL;
}

extern "C" void bash_redir_stderr_to_file(Item filename, int append) {
    Item fn_str = bash_to_string(filename);
    String* fn = it2s(fn_str);
    if (!fn || fn->len == 0) return;

    // check /dev/null shortcut
    if (fn->len == 9 && memcmp(fn->chars, "/dev/null", 9) == 0) {
        current_stderr_route = BASH_STDERR_TO_NULL;
        return;
    }

    current_stderr_route = BASH_STDERR_TO_FILE;
    bash_stderr_set_file(fn->chars, append);
    bash_stderr_begin_capture();
}

extern "C" void bash_redir_stderr_restore(void) {
    current_stderr_route = BASH_STDERR_NORMAL;
}
