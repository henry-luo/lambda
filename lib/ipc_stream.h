#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct IpcStream IpcStream;

// Mechanism only: adopt an exclusively owned, connected AF_UNIX stream. No
// endpoint creation, path lookup or caller authentication. Ownership transfers
// only on success; it becomes nonblocking and close-on-exec.
IpcStream* ipc_stream_adopt(int fd);
void ipc_stream_close(IpcStream* stream);
// Wakes blocked transfer; the caller must join users before close.
void ipc_stream_interrupt(IpcStream* stream);

// Transfer exactly size bytes before one absolute monotonic deadline (ms).
// Reject all ancillary data and close received descriptors. eof is true only
// for a clean receive EOF before any bytes; truncation never looks complete.
bool ipc_stream_transfer(IpcStream* stream, void* bytes, size_t size,
    bool sending, uint64_t deadline_ms, bool* eof);
// Owner predicate checked before each bounded transfer and after poll. Guarded
// poll waits are capped at 25 ms; transfers are at most 64 KiB per check.
// The predicate cannot wait on the peer; false aborts with EACCES.
typedef bool (*IpcStreamTransferCheck)(void* context);
bool ipc_stream_transfer_checked(IpcStream* stream, void* bytes, size_t size,
    bool sending, uint64_t deadline_ms, bool* eof,
    IpcStreamTransferCheck check, void* context);

#ifdef __cplusplus
}
#endif
