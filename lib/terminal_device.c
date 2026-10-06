#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L
#include "terminal_device.h"
#include "memtrack.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <errno.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#endif

struct TerminalDevice {
    bool is_tty;
    bool raw_mode;
#ifdef _WIN32
    HANDLE input;
    HANDLE output;
    int input_fd;
    int output_fd;
    DWORD input_mode;
    DWORD output_mode;
    UINT input_codepage;
    UINT output_codepage;
#else
    int input_fd;
    int output_fd;
    struct termios original_mode;
    struct sigaction old_int;
    struct sigaction old_term;
    struct sigaction old_winch;
    struct sigaction old_pipe;
    bool signals_installed;
#endif
};

#ifndef _WIN32
static volatile sig_atomic_t terminal_interrupted = 0;
static volatile sig_atomic_t terminal_resized = 0;
static TerminalDevice* terminal_signal_owner = NULL;

static void terminal_signal_handler(int signal_number) {
    if (signal_number == SIGWINCH) terminal_resized = 1;
    else terminal_interrupted = 1;
}

static bool terminal_install_signals(TerminalDevice* device) {
    if (terminal_signal_owner) return false;
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = terminal_signal_handler;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, &device->old_int) != 0) return false;
    if (sigaction(SIGTERM, &action, &device->old_term) != 0) {
        sigaction(SIGINT, &device->old_int, NULL);
        return false;
    }
    if (sigaction(SIGWINCH, &action, &device->old_winch) != 0) {
        sigaction(SIGTERM, &device->old_term, NULL);
        sigaction(SIGINT, &device->old_int, NULL);
        return false;
    }
    action.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &action, &device->old_pipe) != 0) {
        sigaction(SIGWINCH, &device->old_winch, NULL);
        sigaction(SIGTERM, &device->old_term, NULL);
        sigaction(SIGINT, &device->old_int, NULL);
        return false;
    }
    terminal_interrupted = 0;
    terminal_resized = 0;
    terminal_signal_owner = device;
    device->signals_installed = true;
    return true;
}

static void terminal_restore_signals(TerminalDevice* device) {
    if (!device->signals_installed) return;
    sigaction(SIGPIPE, &device->old_pipe, NULL);
    sigaction(SIGWINCH, &device->old_winch, NULL);
    sigaction(SIGTERM, &device->old_term, NULL);
    sigaction(SIGINT, &device->old_int, NULL);
    terminal_signal_owner = NULL;
    device->signals_installed = false;
}

static int terminal_signal_status(void) {
    if (terminal_interrupted) return -4;
    if (terminal_resized) {
        terminal_resized = 0;
        return -3;
    }
    return 0;
}
#endif

TerminalDevice* terminal_device_open(void) {
    TerminalDevice* device = (TerminalDevice*)mem_calloc(
        1, sizeof(TerminalDevice), MEM_CAT_SYSTEM);
    if (!device) return NULL;
#ifdef _WIN32
    device->input = GetStdHandle(STD_INPUT_HANDLE);
    device->output = GetStdHandle(STD_OUTPUT_HANDLE);
    device->input_fd = _fileno(stdin);
    device->output_fd = _fileno(stdout);
    if (device->input == INVALID_HANDLE_VALUE ||
            device->output == INVALID_HANDLE_VALUE) goto failed;
    device->is_tty = _isatty(device->input_fd) && _isatty(device->output_fd);
    if (device->is_tty) {
        if (!GetConsoleMode(device->input, &device->input_mode) ||
                !GetConsoleMode(device->output, &device->output_mode)) goto failed;
        device->input_codepage = GetConsoleCP();
        device->output_codepage = GetConsoleOutputCP();
        SetConsoleCP(CP_UTF8);
        SetConsoleOutputCP(CP_UTF8);
    }
#else
    device->input_fd = STDIN_FILENO;
    device->output_fd = STDOUT_FILENO;
    device->is_tty = isatty(device->input_fd) && isatty(device->output_fd);
    if (device->is_tty) {
        if (tcgetattr(device->input_fd, &device->original_mode) != 0 ||
                !terminal_install_signals(device)) goto failed;
    }
#endif
    return device;

failed:
    terminal_device_close(device);
    return NULL;
}

void terminal_device_close(TerminalDevice* device) {
    if (!device) return;
    if (device->raw_mode) terminal_device_set_raw(device, false);
#ifdef _WIN32
    if (device->is_tty) {
        SetConsoleCP(device->input_codepage);
        SetConsoleOutputCP(device->output_codepage);
    }
#else
    terminal_restore_signals(device);
#endif
    mem_free(device);
}

bool terminal_device_is_tty(TerminalDevice* device) {
    return device && device->is_tty;
}

int terminal_device_set_raw(TerminalDevice* device, bool enable) {
    if (!device || !device->is_tty) return -1;
    if (device->raw_mode == enable) return 0;
#ifdef _WIN32
    DWORD input_mode = device->input_mode;
    DWORD output_mode = device->output_mode;
    if (enable) {
        input_mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
        // VT input keeps key decoding in the shared Lambda protocol rather
        // than reintroducing a Win32 record-to-editor policy path.
        input_mode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
        output_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    }
    DWORD previous_input_mode = 0;
    if (!GetConsoleMode(device->input, &previous_input_mode) ||
            !SetConsoleMode(device->input, input_mode)) return -1;
    if (!SetConsoleMode(device->output, output_mode)) {
        SetConsoleMode(device->input, previous_input_mode);
        return -1;
    }
#else
    struct termios mode = device->original_mode;
    if (enable) {
        mode.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
        mode.c_iflag &= ~(IXON | ICRNL | INPCK | ISTRIP);
        mode.c_oflag &= ~OPOST;
        mode.c_cflag |= CS8;
        mode.c_cc[VMIN] = 1;
        mode.c_cc[VTIME] = 0;
    }
    if (tcsetattr(device->input_fd, TCSAFLUSH, &mode) != 0) return -1;
#endif
    device->raw_mode = enable;
    return 0;
}

int terminal_device_size(TerminalDevice* device, int* rows, int* columns) {
    if (!device || !rows || !columns) return -1;
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(device->output, &info)) {
        *rows = info.srWindow.Bottom - info.srWindow.Top + 1;
        *columns = info.srWindow.Right - info.srWindow.Left + 1;
        return 0;
    }
#else
    struct winsize size;
    if (ioctl(device->output_fd, TIOCGWINSZ, &size) == 0 &&
            size.ws_row > 0 && size.ws_col > 0) {
        *rows = size.ws_row;
        *columns = size.ws_col;
        return 0;
    }
#endif
    *rows = 24;
    *columns = 80;
    return -1;
}

int terminal_device_wait(TerminalDevice* device, int timeout_ms) {
    if (!device) return -1;
#ifdef _WIN32
    DWORD timeout = timeout_ms < 0 ? INFINITE : (DWORD)timeout_ms;
    DWORD status = WaitForSingleObject(device->input, timeout);
    return status == WAIT_OBJECT_0 ? 1 : status == WAIT_TIMEOUT ? 0 : -1;
#else
    int signal_status = terminal_signal_status();
    if (signal_status) return signal_status;
    fd_set readers;
    FD_ZERO(&readers);
    FD_SET(device->input_fd, &readers);
    struct timeval timeout = {0};
    struct timeval* deadline = NULL;
    if (timeout_ms >= 0) {
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_usec = (timeout_ms % 1000) * 1000;
        deadline = &timeout;
    }
    int ready = select(device->input_fd + 1, &readers, NULL, NULL, deadline);
    if (ready < 0 && errno == EINTR) {
        signal_status = terminal_signal_status();
        return signal_status ? signal_status : -2;
    }
    return ready;
#endif
}

int64_t terminal_device_read(TerminalDevice* device, char* bytes, size_t capacity) {
    if (!device || !bytes || capacity == 0) return -1;
#ifdef _WIN32
    DWORD read_count = 0;
    DWORD amount = capacity > UINT32_MAX ? UINT32_MAX : (DWORD)capacity;
    if (!ReadFile(device->input, bytes, amount, &read_count, NULL)) return -1;
    return (int64_t)read_count;
#else
    ssize_t read_count = read(device->input_fd, bytes, capacity);
    if (read_count < 0 && errno == EINTR) {
        int signal_status = terminal_signal_status();
        return signal_status ? signal_status : -2;
    }
    return (int64_t)read_count;
#endif
}

int64_t terminal_device_write(TerminalDevice* device, const char* bytes, size_t length) {
    if (!device || !bytes) return -1;
#ifdef _WIN32
    DWORD written = 0;
    DWORD amount = length > UINT32_MAX ? UINT32_MAX : (DWORD)length;
    if (device->is_tty) {
        if (!WriteConsoleA(device->output, bytes, amount, &written, NULL)) return -1;
    } else if (!WriteFile(device->output, bytes, amount, &written, NULL)) return -1;
    return (int64_t)written;
#else
    ssize_t written = write(device->output_fd, bytes, length);
    if (written < 0 && errno == EINTR) return -2;
    return (int64_t)written;
#endif
}
