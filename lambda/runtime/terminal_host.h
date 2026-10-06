#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct TerminalSession;

typedef enum TerminalReadStatus {
    TERMINAL_READ_LINE,
    TERMINAL_READ_EOF,
    TERMINAL_READ_INTERRUPTED,
    TERMINAL_READ_ERROR
} TerminalReadStatus;

typedef struct TerminalReadResult {
    TerminalReadStatus status;
    char* line;
} TerminalReadResult;

// Device callbacks transport bytes and modes only. Lambda owns protocol,
// editor, rendering and terminal-session policy (D7.1.6, S12.1.3).
typedef struct TerminalTransport {
    void* device;
    bool is_tty;
    int (*set_raw)(void* device, bool enable);
    int (*size)(void* device, int* rows, int* columns);
    int (*wait)(void* device, int timeout_ms);
    int64_t (*read)(void* device, char* bytes, size_t capacity);
    int64_t (*write)(void* device, const char* bytes, size_t length);
} TerminalTransport;

TerminalSession* terminal_session_open(bool is_tty, int columns);
TerminalReadResult terminal_session_readline(TerminalSession* session,
                                             const TerminalTransport* transport,
                                             const char* prompt);
void terminal_session_close(TerminalSession* session);
