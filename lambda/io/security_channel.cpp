#include "security_channel.h"
#include "../../lib/endian.h"
#include "../../lib/mem.h"
#include "../../lib/ipc_stream.h"
#include <errno.h>
#include <limits.h>
#include <string.h>

#ifndef _WIN32
#include "../../lib/time_util.h"
#include <pthread.h>

enum { CHANNEL_HEADER = 32, CHANNEL_RESULT = 32, CHANNEL_PATH = 4096,
    CHANNEL_MAX_PAYLOAD = 16 * 1024 * 1024 };
enum ChannelOperation { CHANNEL_READ = 1, CHANNEL_WRITE, CHANNEL_SOURCE,
    CHANNEL_KIND, CHANNEL_ENUMERATE, CHANNEL_STAT, CHANNEL_STAMP };

struct SecurityChannel {
    IpcStream* stream;
    SecurityChannelConfig config;
    uint64_t sequence;
    uint64_t bytes;
    pthread_mutex_t mutex;
    bool dead;
    int error;
    IpcStreamTransferCheck transfer_check;
    void* transfer_context;
};

struct ChannelBuffer {
    uint8_t* data;
    uint32_t size, offset, capacity;
    bool good;
    bool locked;
    bool mutation;
};

static SecurityIOResult channel_error(int error) {
    return {SECURITY_DENIED, error ? error : EIO, false, {}, false};
}
static bool channel_ok(SecurityIOResult result) {
    return result.decision == SECURITY_ALLOWED && !result.system_error;
}
static bool channel_buffer(ChannelBuffer* buffer, uint32_t capacity) {
    *buffer = {};
    buffer->data = (uint8_t*)mem_calloc(1, capacity, MEM_CAT_TEMP);
    buffer->capacity = capacity;
    return buffer->good = buffer->data != nullptr;
}
static void channel_put(ChannelBuffer* buffer, const void* bytes, uint32_t size) {
    if (!buffer->good || size > buffer->capacity - buffer->size || (size && !bytes)) {
        buffer->good = false; return;
    }
    if (size) memcpy(buffer->data + buffer->size, bytes, size);
    buffer->size += size;
}
static const uint8_t* channel_take(ChannelBuffer* buffer, uint32_t size) {
    if (!buffer->good || size > buffer->size - buffer->offset) {
        buffer->good = false; return nullptr;
    }
    const uint8_t* bytes = buffer->data + buffer->offset;
    buffer->offset += size;
    return bytes;
}
static void channel_put32(ChannelBuffer* buffer, uint32_t value) {
    uint8_t bytes[4]; write_le32(bytes, value); channel_put(buffer, bytes, sizeof(bytes));
}
static void channel_put64(ChannelBuffer* buffer, uint64_t value) {
    uint8_t bytes[8]; write_le64(bytes, value); channel_put(buffer, bytes, sizeof(bytes));
}
static uint32_t channel_get32(ChannelBuffer* buffer) {
    const uint8_t* bytes = channel_take(buffer, 4); return bytes ? read_le32(bytes) : 0;
}
static uint64_t channel_get64(ChannelBuffer* buffer) {
    const uint8_t* bytes = channel_take(buffer, 8); return bytes ? read_le64(bytes) : 0;
}
static bool channel_end(ChannelBuffer* buffer) { return buffer->good && buffer->offset == buffer->size; }

static void channel_put_text(ChannelBuffer* buffer, const char* text) {
    size_t length = text ? strnlen(text, CHANNEL_PATH) : 0;
    if (!length || length >= CHANNEL_PATH) { buffer->good = false; return; }
    channel_put32(buffer, (uint32_t)length); channel_put(buffer, text, (uint32_t)length);
}
static bool channel_get_text(ChannelBuffer* buffer, char* text) {
    uint32_t length = channel_get32(buffer);
    if (!length || length >= CHANNEL_PATH) { buffer->good = false; return false; }
    const uint8_t* bytes = channel_take(buffer, length);
    if (!bytes || memchr(bytes, 0, length)) { buffer->good = false; return false; }
    memcpy(text, bytes, length); text[length] = 0; return true;
}
static void channel_put_stat(ChannelBuffer* buffer, const FileStat* stat) {
    channel_put64(buffer, (uint64_t)stat->size);
    channel_put64(buffer, (uint64_t)(int64_t)stat->modified);
    channel_put64(buffer, (uint64_t)(int64_t)stat->created);
    channel_put32(buffer, stat->mode);
    channel_put32(buffer, (uint32_t)stat->is_file | ((uint32_t)stat->is_dir << 1) |
        ((uint32_t)stat->is_symlink << 2) | ((uint32_t)stat->exists << 3));
}
static void channel_get_stat(ChannelBuffer* buffer, FileStat* stat) {
    stat->size = (int64_t)channel_get64(buffer);
    int64_t modified = (int64_t)channel_get64(buffer), created = (int64_t)channel_get64(buffer);
    stat->modified = (time_t)modified; stat->created = (time_t)created;
    uint32_t mode = channel_get32(buffer), flags = channel_get32(buffer);
    if (mode > UINT16_MAX || flags > 15 || (int64_t)stat->modified != modified ||
            (int64_t)stat->created != created) buffer->good = false;
    stat->mode = (uint16_t)mode; stat->is_file = flags & 1; stat->is_dir = flags & 2;
    stat->is_symlink = flags & 4; stat->exists = flags & 8;
}
static void channel_put_result(ChannelBuffer* buffer, SecurityIOResult result) {
    channel_put32(buffer, result.decision); channel_put32(buffer, (uint32_t)result.system_error);
    channel_put32(buffer, (uint32_t)result.committed | ((uint32_t)result.outcome_unknown << 1));
    channel_put32(buffer, 0); channel_put64(buffer, result.approval.session);
    channel_put64(buffer, result.approval.serial);
}
static SecurityIOResult channel_get_result(ChannelBuffer* buffer) {
    uint32_t decision = channel_get32(buffer), error = channel_get32(buffer);
    uint32_t flags = channel_get32(buffer), reserved = channel_get32(buffer);
    uint64_t session = channel_get64(buffer), serial = channel_get64(buffer);
    if (decision > SECURITY_LIMIT_EXCEEDED || error > INT_MAX || flags > 3 || reserved ||
            ((session == 0) != (serial == 0))) buffer->good = false;
    if (!buffer->good) return channel_error(EPROTO);
    return {(SecurityDecision)decision, (int)error, (bool)(flags & 1), {session, serial}, (bool)(flags & 2)};
}

static void channel_stop(SecurityChannel* channel, int error) {
    channel->dead = true; channel->error = error ? error : EIO;
    ipc_stream_interrupt(channel->stream);
}

// Raw descriptor acquisition/transfer stays in lib (D11.1.2); this owner
// poisons its protocol state on any mechanism failure.
static bool channel_transfer(SecurityChannel* channel, void* bytes, uint32_t size,
        bool sending, uint64_t deadline, bool* eof) {
    if (ipc_stream_transfer_checked(channel->stream, bytes, size, sending, deadline, eof,
            channel->transfer_check, channel->transfer_context)) return true;
    int error = errno; channel_stop(channel, error); return false;
}
static bool channel_budget(SecurityChannel* channel, uint32_t size) {
    uint64_t total = CHANNEL_HEADER + (uint64_t)size;
    if (size > channel->config.max_payload_bytes ||
            total > channel->config.max_total_bytes - channel->bytes) {
        channel_stop(channel, EFBIG); return false;
    }
    channel->bytes += total; return true;
}
static bool channel_decode_header(SecurityChannel* channel, const uint8_t* header,
        uint16_t expected_operation, bool response, uint16_t* operation, uint32_t* size) {
    *operation = read_le16(header + 6); *size = read_le32(header + 24);
    if (memcmp(header, "LSAF", 4) || read_le16(header + 4) != 1 ||
            *operation < CHANNEL_READ || *operation > CHANNEL_STAMP ||
            (expected_operation && *operation != expected_operation) ||
            read_le64(header + 8) != channel->sequence ||
            read_le64(header + 16) != channel->config.session ||
            read_le32(header + 28) != (response ? 1U : 0U)) {
        channel_stop(channel, EPROTO); return false;
    }
    return channel_budget(channel, *size);
}
static bool channel_frame(SecurityChannel* channel, uint16_t operation, bool response,
        ChannelBuffer* body, bool sending, uint64_t deadline, bool* eof) {
    uint8_t header[CHANNEL_HEADER] = {};
    if (sending) {
        if (!body->good || !channel_budget(channel, body->size)) return false;
        memcpy(header, "LSAF", 4); write_le16(header + 4, 1); write_le16(header + 6, operation);
        write_le64(header + 8, channel->sequence); write_le64(header + 16, channel->config.session);
        write_le32(header + 24, body->size); write_le32(header + 28, response ? 1 : 0);
    }
    if (!channel_transfer(channel, header, sizeof(header), sending, deadline, eof)) return false;
    if (!sending) {
        uint32_t size; uint16_t received_operation;
        if (!channel_decode_header(channel, header, operation, response, &received_operation, &size)) return false;
        body->size = size; body->offset = 0;
    }
    return channel_transfer(channel, body->data, body->size, sending, deadline, nullptr);
}

SecurityChannel* security_channel_create(int fd, const SecurityChannelConfig* config) {
    if (!config || !config->session || config->max_payload_bytes < 256 ||
            config->max_payload_bytes > CHANNEL_MAX_PAYLOAD || config->max_total_bytes < 128 ||
            !config->max_requests || config->max_requests > 1048576 ||
            !config->timeout_ms || config->timeout_ms > 60000) { errno = EINVAL; return nullptr; }
    SecurityChannel* channel = (SecurityChannel*)mem_calloc(1, sizeof(*channel), MEM_CAT_EVAL);
    if (!channel) { errno = ENOMEM; return nullptr; }
    if (pthread_mutex_init(&channel->mutex, nullptr)) { mem_free(channel); errno = ENOMEM; return nullptr; }
    channel->stream = ipc_stream_adopt(fd);
    if (!channel->stream) {
        int error = errno; pthread_mutex_destroy(&channel->mutex); mem_free(channel); errno = error; return nullptr;
    }
    channel->config = *config; return channel;
}
void security_channel_interrupt(SecurityChannel* channel) { if (channel) ipc_stream_interrupt(channel->stream); }
void security_channel_destroy(SecurityChannel* channel) {
    if (!channel) return;
    ipc_stream_close(channel->stream); pthread_mutex_destroy(&channel->mutex); mem_free(channel);
}

static SecurityIOResult channel_call(SecurityChannel* channel, uint16_t operation,
        ChannelBuffer* request, ChannelBuffer* response) {
    if (!channel || !request->good) return channel_error(EINVAL);
    if (!channel_buffer(response, channel->config.max_payload_bytes)) return channel_error(ENOMEM);
    pthread_mutex_lock(&channel->mutex);
    response->locked = true;
    response->mutation = operation == CHANNEL_WRITE;
    SecurityIOResult result = channel_error(channel->error);
    bool dispatched = false;
    if (!channel->dead && channel->sequence < channel->config.max_requests) {
        channel->sequence++;
        uint64_t deadline = time_now_ms() + channel->config.timeout_ms;
        dispatched = channel_frame(channel, operation, false, request, true, deadline, nullptr);
        if (dispatched &&
                channel_frame(channel, operation, true, response, false, deadline, nullptr)) {
            result = channel_get_result(response);
            if ((result.approval.session && result.approval.session != channel->config.session) ||
                    (operation != CHANNEL_WRITE && (result.committed || result.outcome_unknown || result.approval.serial)))
                response->good = false;
            if (!response->good) channel_stop(channel, EPROTO);
        } else result = channel_error(channel->error);
    } else if (!channel->dead) { channel_stop(channel, EFBIG); result = channel_error(EFBIG); }
    // No retry or alternate transport after any ambiguous write outcome.
    if (operation == CHANNEL_WRITE && dispatched && channel->dead) result.outcome_unknown = true;
    return result;
}
static bool channel_request(SecurityChannel* channel, ChannelBuffer* request, const char* path) {
    if (!channel || !channel_buffer(request, channel->config.max_payload_bytes)) return false;
    if (path) channel_put_text(request, path);
    return request->good;
}
static SecurityIOResult channel_finish(SecurityChannel* channel, SecurityIOResult result,
        ChannelBuffer* request, ChannelBuffer* response) {
    if (response->locked) {
        if (!channel_end(response)) {
            channel_stop(channel, EPROTO);
            bool unknown = response->mutation;
            result = channel_error(EPROTO); result.outcome_unknown = unknown;
        }
        pthread_mutex_unlock(&channel->mutex);
    }
    mem_free(request->data); mem_free(response->data); return result;
}
static SecurityIOResult channel_read(void* data, const char* path, bool source,
        uint64_t limit, MemCategory category, char** bytes, size_t* size) {
    *bytes = nullptr; *size = 0;
    SecurityChannel* channel = (SecurityChannel*)data;
    ChannelBuffer request = {}, response = {};
    channel_request(channel, &request, path); channel_put64(&request, limit); channel_put32(&request, source);
    SecurityIOResult result = channel_call(channel, CHANNEL_READ, &request, &response);
    if (channel_ok(result) && response.good) {
        uint32_t length = response.size - response.offset;
        if (length > limit) response.good = false;
        else {
            *bytes = (char*)mem_calloc(1, (size_t)length + 1, category);
            if (*bytes) { memcpy(*bytes, channel_take(&response, length), length); *size = length; }
            else { result = channel_error(ENOMEM); response.offset = response.size; }
        }
    }
    result = channel_finish(channel, result, &request, &response);
    if (!channel_ok(result)) { mem_free(*bytes); *bytes = nullptr; *size = 0; }
    return result;
}
static SecurityIOResult channel_write(void* data, const char* path, const void* bytes,
        size_t size, bool append, bool atomic) {
    SecurityChannel* channel = (SecurityChannel*)data;
    ChannelBuffer request = {}, response = {};
    channel_request(channel, &request, path);
    channel_put32(&request, (uint32_t)append | ((uint32_t)atomic << 1));
    if (size > UINT32_MAX) request.good = false;
    else channel_put(&request, bytes, (uint32_t)size);
    SecurityIOResult result = channel_call(channel, CHANNEL_WRITE, &request, &response);
    return channel_finish(channel, result, &request, &response);
}
static SecurityIOResult channel_source(void* data, const char* path, char** canonical) {
    *canonical = nullptr;
    SecurityChannel* channel = (SecurityChannel*)data;
    ChannelBuffer request = {}, response = {};
    channel_request(channel, &request, path);
    SecurityIOResult result = channel_call(channel, CHANNEL_SOURCE, &request, &response);
    if (channel_ok(result)) {
        char text[CHANNEL_PATH];
        if (channel_get_text(&response, text)) *canonical = mem_strdup(text, MEM_CAT_EVAL);
        if (!*canonical && response.good) result = channel_error(ENOMEM);
    }
    result = channel_finish(channel, result, &request, &response);
    if (!channel_ok(result)) { mem_free(*canonical); *canonical = nullptr; }
    return result;
}
static SecurityIOResult channel_kind(void* data, const char* path, int32_t* kind) {
    *kind = -1;
    SecurityChannel* channel = (SecurityChannel*)data;
    ChannelBuffer request = {}, response = {};
    channel_request(channel, &request, path);
    SecurityIOResult result = channel_call(channel, CHANNEL_KIND, &request, &response);
    if (channel_ok(result)) {
        uint32_t value = channel_get32(&response);
        if (value != SECURITY_OBJECT_FILE && value != SECURITY_OBJECT_DIRECTORY) response.good = false;
        else *kind = (int32_t)value;
    }
    result = channel_finish(channel, result, &request, &response);
    if (!channel_ok(result)) *kind = -1;
    return result;
}
static SecurityIOResult channel_stat(void* data, const char* path, FileStat* stat) {
    *stat = {};
    SecurityChannel* channel = (SecurityChannel*)data;
    ChannelBuffer request = {}, response = {};
    channel_request(channel, &request, path);
    SecurityIOResult result = channel_call(channel, CHANNEL_STAT, &request, &response);
    if (channel_ok(result)) channel_get_stat(&response, stat);
    result = channel_finish(channel, result, &request, &response);
    if (!channel_ok(result)) *stat = {};
    return result;
}
static SecurityIOResult channel_stamp(void* data, SecurityAuthorityStamp* stamp) {
    *stamp = {};
    SecurityChannel* channel = (SecurityChannel*)data;
    ChannelBuffer request = {}, response = {};
    channel_request(channel, &request, nullptr);
    SecurityIOResult result = channel_call(channel, CHANNEL_STAMP, &request, &response);
    if (channel_ok(result)) {
        stamp->session = channel_get64(&response); stamp->epoch = channel_get64(&response);
        if (stamp->session != channel->config.session || !stamp->epoch) response.good = false;
    }
    result = channel_finish(channel, result, &request, &response);
    if (!channel_ok(result)) *stamp = {};
    return result;
}
static SecurityIOResult channel_enumerate(void* data, const char* path,
        uint32_t limit, SecurityDirectory* directory) {
    *directory = {};
    SecurityChannel* channel = (SecurityChannel*)data;
    ChannelBuffer request = {}, response = {};
    channel_request(channel, &request, path); channel_put32(&request, limit);
    SecurityIOResult result = channel_call(channel, CHANNEL_ENUMERATE, &request, &response);
    if (channel_ok(result)) {
        uint32_t count = channel_get32(&response);
        if (count > limit || count > SECURITY_IO_DIRECTORY_LIMIT) response.good = false;
        else if (count) {
            directory->entries = (SecurityDirectoryEntry*)mem_calloc(count, sizeof(SecurityDirectoryEntry), MEM_CAT_EVAL);
            if (!directory->entries) { result = channel_error(ENOMEM); response.offset = response.size; }
        }
        if (channel_ok(result) && response.good) for (uint32_t i = 0; i < count && response.good; i++) {
            char text[CHANNEL_PATH];
            if (!channel_get_text(&response, text) || strchr(text, '/') || strchr(text, '\\') ||
                    !strcmp(text, ".") || !strcmp(text, "..")) { response.good = false; break; }
            SecurityDirectoryEntry* entry = &directory->entries[directory->count++];
            entry->name = mem_strdup(text, MEM_CAT_EVAL);
            uint32_t flags = channel_get32(&response);
            if (flags > 3 || !entry->name) { response.good = false; break; }
            entry->is_directory = flags & 1; entry->has_metadata = flags & 2;
            if (entry->has_metadata) channel_get_stat(&response, &entry->metadata);
        }
    }
    result = channel_finish(channel, result, &request, &response);
    if (!channel_ok(result)) security_io_directory_free(directory);
    return result;
}
SecurityIOClient security_channel_client(SecurityChannel* channel) {
    return {1, channel, channel_read, channel_write, channel_source,
        channel_kind, channel_enumerate, channel_stat, channel_stamp};
}

// Decode the entire request before dispatch, including reserved bits and
// trailing bytes. Malformed input cannot reach even a mutation callback.
struct ChannelRequest {
    char path[CHANNEL_PATH];
    uint64_t limit;
    uint32_t flags, entries, size;
    const uint8_t* data;
};
static bool channel_decode_request(uint16_t operation, ChannelBuffer* request, ChannelRequest* decoded) {
    *decoded = {};
    if (operation != CHANNEL_STAMP && !channel_get_text(request, decoded->path)) return false;
    if (operation == CHANNEL_READ) {
        decoded->limit = channel_get64(request); decoded->flags = channel_get32(request);
        if (decoded->flags > 1) return false;
    } else if (operation == CHANNEL_WRITE) {
        decoded->flags = channel_get32(request);
        if (!request->good || decoded->flags > 3) return false;
        decoded->size = request->size - request->offset; decoded->data = channel_take(request, decoded->size);
    } else if (operation == CHANNEL_ENUMERATE) {
        decoded->entries = channel_get32(request);
        if (decoded->entries > SECURITY_IO_DIRECTORY_LIMIT) return false;
    }
    return channel_end(request);
}
static SecurityIOResult channel_dispatch(uint16_t operation, const ChannelRequest* request,
        ChannelBuffer* response, const SecurityIOClient* client) {
    const char* path = request->path;
    SecurityIOResult result = channel_error(EPROTO);
    if (operation == CHANNEL_READ) {
        uint64_t limit = request->limit;
        uint64_t ceiling = response->capacity - CHANNEL_RESULT;
        if (limit > ceiling) limit = ceiling;
        char* bytes = nullptr; size_t size = 0;
        security_io_read_bytes(client, path, request->flags != 0, limit, MEM_CAT_TEMP, &bytes, &size, &result);
        if (channel_ok(result)) channel_put(response, bytes, (uint32_t)size);
        mem_free(bytes);
    } else if (operation == CHANNEL_WRITE) {
        result = client->write(client->context, path, request->data, request->size, request->flags & 1, request->flags & 2);
    } else if (operation == CHANNEL_SOURCE) {
        char* canonical = nullptr;
        security_io_source_path(client, path, &canonical, &result);
        if (channel_ok(result)) channel_put_text(response, canonical);
        mem_free(canonical);
    } else if (operation == CHANNEL_KIND) {
        int32_t kind;
        security_io_input_kind(client, path, &kind, &result);
        if (channel_ok(result)) channel_put32(response, (uint32_t)kind);
    } else if (operation == CHANNEL_STAT) {
        FileStat stat;
        security_io_stat(client, path, &stat, &result);
        if (channel_ok(result)) channel_put_stat(response, &stat);
    } else if (operation == CHANNEL_ENUMERATE) {
        SecurityDirectory directory = {};
        security_io_enumerate(client, path, request->entries, &directory, &result);
        if (channel_ok(result)) {
            channel_put32(response, directory.count);
            for (uint32_t i = 0; i < directory.count && response->good; i++) {
                SecurityDirectoryEntry* entry = &directory.entries[i];
                channel_put_text(response, entry->name);
                channel_put32(response, (uint32_t)entry->is_directory | ((uint32_t)entry->has_metadata << 1));
                if (entry->has_metadata) channel_put_stat(response, &entry->metadata);
            }
        }
        security_io_directory_free(&directory);
    } else if (operation == CHANNEL_STAMP) {
        SecurityAuthorityStamp stamp;
        security_io_authority_stamp(client, &stamp, &result);
        if (channel_ok(result)) { channel_put64(response, stamp.session); channel_put64(response, stamp.epoch); }
    }
    return result;
}

static bool channel_guard_valid(const SecurityChannelDeliveryGuard* guard) {
    return guard && guard->capture && guard->acquire && guard->check && guard->release &&
        guard->timeout_ms && guard->timeout_ms <= 1000;
}
static SecurityIOResult channel_delivery_denial(SecurityIOResult failure, SecurityIOResult previous) {
    if (failure.decision == SECURITY_ALLOWED || failure.decision == SECURITY_APPROVAL_REQUIRED ||
            (unsigned)failure.decision > SECURITY_LIMIT_EXCEEDED)
        failure = {SECURITY_REVOKED, EACCES, false, {}};
    failure.committed |= previous.committed;
    failure.outcome_unknown |= previous.outcome_unknown;
    failure.approval = {};
    return failure;
}
struct ChannelPublication {
    SecurityChannel* channel;
    const SecurityChannelDeliveryGuard* guard;
    void* lease;
    static bool check(void* context) {
        ChannelPublication* publication = (ChannelPublication*)context;
        return publication->guard->check(publication->guard->context, publication->lease);
    }
    ~ChannelPublication() {
        channel->transfer_check = nullptr; channel->transfer_context = nullptr;
        if (lease) guard->release(guard->context, lease);
    }
};

static bool channel_dispatch_loop(SecurityChannel* channel, const SecurityIOClient* client,
    const SecurityChannelDeliveryGuard* guard, SecurityChannelReport* report);
static bool channel_serve(int fd, const SecurityChannelConfig* config, const SecurityIOClient* client,
        const SecurityChannelDeliveryGuard* guard, SecurityChannelReport* report) {
    if (report) *report = {};
    if (!client || client->version != 1 || !client->read || !client->write) { errno = EINVAL; return false; }
    SecurityChannel* channel = security_channel_create(fd, config);
    if (!channel) return false;
    bool success = channel_dispatch_loop(channel, client, guard, report);
    int error = errno; security_channel_destroy(channel); errno = error;
    return success;
}

bool security_channel_serve(int fd, const SecurityChannelConfig* config,
        const SecurityIOClient* client, SecurityChannelReport* report) {
    return channel_serve(fd, config, client, nullptr, report);
}
bool security_channel_dispatch(SecurityChannel* channel, const SecurityIOClient* client, SecurityChannelReport* report) {
    return channel_dispatch_loop(channel, client, nullptr, report);
}
bool security_channel_serve_guarded(int fd, const SecurityChannelConfig* config, const SecurityIOClient* client,
        const SecurityChannelDeliveryGuard* guard, SecurityChannelReport* report) {
    if (!channel_guard_valid(guard)) { if (report) *report = {}; errno = EINVAL; return false; }
    return channel_serve(fd, config, client, guard, report);
}
bool security_channel_dispatch_guarded(SecurityChannel* channel, const SecurityIOClient* client,
        const SecurityChannelDeliveryGuard* guard, SecurityChannelReport* report) {
    if (!channel_guard_valid(guard)) { if (report) *report = {}; errno = EINVAL; return false; }
    return channel_dispatch_loop(channel, client, guard, report);
}
static bool channel_dispatch_loop(SecurityChannel* channel, const SecurityIOClient* client,
        const SecurityChannelDeliveryGuard* guard, SecurityChannelReport* report) {
    if (report) *report = {};
    if (!channel || !client || client->version != 1 || !client->read || !client->write) { errno = EINVAL; return false; }
    const SecurityChannelConfig* config = &channel->config;
    ChannelBuffer request = {}, response = {};
    bool success = false;
    if (!channel_buffer(&request, config->max_payload_bytes) ||
            !channel_buffer(&response, config->max_payload_bytes)) channel_stop(channel, ENOMEM);
    while (!channel->dead && channel->sequence < config->max_requests) {
        channel->sequence++;
        uint64_t deadline = time_now_ms() + config->timeout_ms;
        // Read the header once to select the operation, then validate the same
        // fixed envelope as the client. No unbounded peek/body allocation.
        uint8_t header[CHANNEL_HEADER]; bool eof = false;
        if (!channel_transfer(channel, header, sizeof(header), false, deadline, &eof)) { success = eof; break; }
        uint16_t operation; uint32_t size;
        if (!channel_decode_header(channel, header, 0, false, &operation, &size)) break;
        request.size = size; request.offset = 0; request.good = true;
        if (!channel_transfer(channel, request.data, size, false, deadline, nullptr)) break;
        ChannelRequest decoded;
        if (!channel_decode_request(operation, &request, &decoded)) { channel_stop(channel, EPROTO); break; }
        // Reserve a complete result before any callback can mutate user files.
        // A known inability to acknowledge must not become an ambiguous commit.
        uint64_t remaining = config->max_total_bytes - channel->bytes;
        if (remaining < CHANNEL_HEADER + CHANNEL_RESULT) { channel_stop(channel, EFBIG); break; }
        response.capacity = (uint32_t)(remaining - CHANNEL_HEADER > config->max_payload_bytes
            ? config->max_payload_bytes : remaining - CHANNEL_HEADER);
        response.size = CHANNEL_RESULT; response.offset = 0; response.good = true;
        SecurityAuthorityStamp stamp = {};
        SecurityIOResult result = guard ? guard->capture(guard->context, &stamp) : SecurityIOResult{SECURITY_ALLOWED, 0, false, {}};
        bool captured = !guard || (channel_ok(result) && stamp.session == config->session && stamp.epoch);
        if (captured) result = channel_dispatch(operation, &decoded, &response, client);
        else result = channel_delivery_denial(result, {});
        if (result.system_error == EPROTO) { channel_stop(channel, EPROTO); break; }
        if (!response.good) { response.size = CHANNEL_RESULT; response.good = true;
            result = {SECURITY_LIMIT_EXCEEDED, EFBIG, false, {}, false}; }
        // Publication is distinct from the potentially long request/consent
        // wait. Pin its captured authority only for the bounded transfer.
        uint64_t delivery_deadline = deadline;
        ChannelPublication publication = {channel, guard, nullptr};
        if (guard) {
            uint64_t limit = time_now_ms() + guard->timeout_ms;
            if (limit < delivery_deadline) delivery_deadline = limit;
            if (captured) {
                SecurityIOResult failure = {SECURITY_REVOKED, EACCES, false, {}};
                publication.lease = guard->acquire(guard->context, &stamp, &failure);
                if (publication.lease) {
                    channel->transfer_check = ChannelPublication::check;
                    channel->transfer_context = &publication;
                } else { result = channel_delivery_denial(failure, result); response.size = CHANNEL_RESULT; }
            }
        }
        ChannelBuffer result_header = {response.data, 0, 0, CHANNEL_RESULT, true};
        channel_put_result(&result_header, result);
        if (!channel_frame(channel, operation, true, &response, true, delivery_deadline, nullptr)) break;
        if (report) report->requests++;
    }
    if (!channel->dead && !success) channel_stop(channel, EFBIG);
    if (report) { report->bytes = channel->bytes; report->system_error = success ? 0 : channel->error; }
    int error = success ? 0 : channel->error;
    mem_free(request.data); mem_free(response.data);
    errno = error; return success;
}
#else
SecurityChannel* security_channel_create(int, const SecurityChannelConfig*) { errno = ENOTSUP; return nullptr; }
SecurityIOClient security_channel_client(SecurityChannel*) { return {}; }
void security_channel_interrupt(SecurityChannel*) {}
void security_channel_destroy(SecurityChannel*) {}
bool security_channel_serve(int, const SecurityChannelConfig*, const SecurityIOClient*, SecurityChannelReport*) {
    errno = ENOTSUP; return false;
}
bool security_channel_dispatch(SecurityChannel*, const SecurityIOClient*, SecurityChannelReport*) {
    errno = ENOTSUP; return false;
}
bool security_channel_serve_guarded(int, const SecurityChannelConfig*, const SecurityIOClient*,
        const SecurityChannelDeliveryGuard*, SecurityChannelReport*) { errno = ENOTSUP; return false; }
bool security_channel_dispatch_guarded(SecurityChannel*, const SecurityIOClient*,
        const SecurityChannelDeliveryGuard*, SecurityChannelReport*) { errno = ENOTSUP; return false; }
#endif
