#ifndef TERMINAL_DEVICE_H
#define TERMINAL_DEVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TerminalDevice TerminalDevice;

// Raw OS transport only; Lambda owns decoding, editing and screen output.
TerminalDevice* terminal_device_open(void);
void terminal_device_close(TerminalDevice* device);
bool terminal_device_is_tty(TerminalDevice* device);
int terminal_device_set_raw(TerminalDevice* device, bool enable);
int terminal_device_size(TerminalDevice* device, int* rows, int* columns);
// wait/read: -2 interrupted, -3 resized, -4 termination requested.
int terminal_device_wait(TerminalDevice* device, int timeout_ms);
int64_t terminal_device_read(TerminalDevice* device, char* bytes, size_t capacity);
int64_t terminal_device_write(TerminalDevice* device, const char* bytes, size_t length);

#ifdef __cplusplus
}
#endif

#endif
