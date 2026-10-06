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

// Completion provider (D8.1.1v17 session services). Given the text before the
// caret, returns how many trailing bytes form the word being completed and
// reports each candidate replacement for that word through `add`.
typedef void (*TerminalCompletionAdd)(void* sink, const char* text, size_t length);
typedef size_t (*TerminalCompleter)(void* opaque, const char* line, size_t length,
                                    TerminalCompletionAdd add, void* sink);

TerminalSession* terminal_session_open(bool is_tty, int columns);
// Later frames offer Tab completion through `completer` (NULL turns it off).
void terminal_session_set_completer(TerminalSession* session,
                                    TerminalCompleter completer, void* opaque);
// Replaces the editor's history with newline-separated entries, oldest first.
bool terminal_session_load_history(TerminalSession* session, const char* text);
// The editor's history as newline-separated entries, oldest first; the caller
// mem_free()s it. NULL when the history cannot be read.
char* terminal_session_history_text(TerminalSession* session);
TerminalReadResult terminal_session_readline(TerminalSession* session,
                                             const TerminalTransport* transport,
                                             const char* prompt);
void terminal_session_close(TerminalSession* session);
