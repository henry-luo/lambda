#include "protocol.hpp"
#include "../input/input.hpp"
#include "../input/input-parsers.h"
#include "../format/format.h"
#include "../runtime/lambda-number-types.hpp"
#include "../../lib/mem.h"
#include "../../lib/mem_context.h"
#include "../../lib/mem_factory.h"
#include "../../lib/utf.h"
#include "../../lib/uv_loop.h"
#include "../../lib/arraylist.h"
#include <string.h>
#include <math.h>

DevtoolsExchange::DevtoolsExchange() : pool(nullptr), input(nullptr),
    error_code(nullptr), error_message(nullptr), revision(-1) {
    pool = mem_pool_create(nullptr, MEM_ROLE_TEMP, "devtools.exchange");
    input = pool ? Input::create(pool) : nullptr;
}

DevtoolsExchange::~DevtoolsExchange() {
    if (pool) mem_pool_destroy(pool);
}

Item DevtoolsExchange::fail(const char* code, const char* message) {
    error_code = code;
    error_message = message;
    return ItemNull;
}

bool devtools_symbol_is(Item value, const char* name) {
    if (get_type_id(value) != LMD_TYPE_SYMBOL) return false;
    Symbol* symbol = value.get_safe_symbol();
    return symbol && symbol->len == strlen(name) &&
        !memcmp(symbol->chars, name, symbol->len);
}

struct DevtoolsConnection {
    uv_pipe_t pipe;
    bool opened;
    bool closed;
    bool handshake;
    bool initialized;
    uint8_t header[4];
    size_t header_size;
    size_t expected;
    size_t queued_bytes;
    StrBuf* incoming;
    ArrayList* requests;
    const DevtoolsCommand* commands;
    size_t command_count;
    void* owner;
    char read_buffer[16384];
};

struct DevtoolsWrite {
    uv_write_t request;
    DevtoolsConnection* connection;
    size_t length;
    char bytes[];
};

static void devtools_pipe_closed(uv_handle_t* handle) {
    ((DevtoolsConnection*)handle->data)->closed = true;
}

void devtools_close(DevtoolsConnection* connection) {
    if (!connection || !connection->initialized ||
            uv_is_closing((uv_handle_t*)&connection->pipe)) return;
    connection->opened = false;
    uv_read_stop((uv_stream_t*)&connection->pipe);
    uv_close((uv_handle_t*)&connection->pipe, devtools_pipe_closed);
}

static void devtools_write_complete(uv_write_t* request, int status) {
    DevtoolsWrite* write = (DevtoolsWrite*)request;
    write->connection->queued_bytes -= write->length;
    if (status < 0) devtools_close(write->connection);
    mem_free(write);
}

bool devtools_send(DevtoolsConnection* connection, Item message) {
    if (!devtools_connected(connection)) return false;
    DevtoolsExchange exchange;
    if (!exchange.input) return false;
    String* mark = format_mark(exchange.pool, message);
    if (!mark || !mark->len || mark->len > DEVTOOLS_MAX_BYTES ||
            connection->queued_bytes + mark->len + 4 > DEVTOOLS_MAX_BYTES) {
        log_error("devtools: outgoing message or queue exceeded its limit");
        devtools_close(connection);
        return false;
    }
    size_t length = mark->len + 4;
    DevtoolsWrite* write = (DevtoolsWrite*)mem_alloc(sizeof(DevtoolsWrite) + length, MEM_CAT_TEMP);
    if (!write) { devtools_close(connection); return false; }
    write->connection = connection;
    write->length = length;
    for (unsigned i = 0; i < 4; i++) write->bytes[i] = (char)(mark->len >> (24 - 8 * i));
    memcpy(write->bytes + 4, mark->chars, mark->len);
    uv_buf_t buffer = uv_buf_init(write->bytes, (unsigned)length);
    connection->queued_bytes += length;
    int status = uv_write(&write->request, (uv_stream_t*)&connection->pipe,
        &buffer, 1, devtools_write_complete);
    if (status < 0) devtools_write_complete(&write->request, status);
    return status == 0;
}

static void devtools_allocate(uv_handle_t* handle, size_t, uv_buf_t* buffer) {
    DevtoolsConnection* connection = (DevtoolsConnection*)handle->data;
    *buffer = uv_buf_init(connection->read_buffer, sizeof(connection->read_buffer));
}

static void devtools_read(uv_stream_t* stream, ssize_t count, const uv_buf_t* buffer) {
    DevtoolsConnection* connection = (DevtoolsConnection*)stream->data;
    if (count < 0) { devtools_close(connection); return; }
    size_t offset = 0, length = (size_t)count;
    while (offset < length && connection->opened) {
        if (connection->header_size < 4) {
            connection->header[connection->header_size++] = (uint8_t)buffer->base[offset++];
            if (connection->header_size != 4) continue;
            connection->expected = 0;
            for (unsigned i = 0; i < 4; i++)
                connection->expected = (connection->expected << 8) | connection->header[i];
            if (!connection->expected || connection->expected > DEVTOOLS_MAX_BYTES ||
                    connection->queued_bytes + connection->expected > DEVTOOLS_MAX_BYTES) {
                devtools_close(connection);
                return;
            }
        }
        size_t take = connection->expected - connection->incoming->length;
        if (take > length - offset) take = length - offset;
        if (!strbuf_ensure_cap(connection->incoming, connection->expected + 1)) {
            devtools_close(connection);
            return;
        }
        strbuf_append_str_n(connection->incoming, buffer->base + offset, take);
        offset += take;
        if (connection->incoming->length == connection->expected) {
            if (connection->requests->length >= 128 ||
                    !arraylist_append(connection->requests, connection->incoming)) {
                devtools_close(connection);
                return;
            }
            connection->queued_bytes += connection->expected;
            connection->incoming = strbuf_new();
            connection->header_size = 0;
            if (!connection->incoming) { devtools_close(connection); return; }
        }
    }
}

DevtoolsConnection* devtools_open(int fd, const DevtoolsCommand* commands,
        size_t command_count, void* owner) {
    if (fd < 3 || lambda_uv_init() != 0) return nullptr;
    DevtoolsConnection* connection = (DevtoolsConnection*)mem_calloc(1, sizeof(DevtoolsConnection), MEM_CAT_TEMP);
    if (!connection) return nullptr;
    connection->incoming = strbuf_new();
    connection->requests = arraylist_new(8);
    connection->commands = commands;
    connection->command_count = command_count;
    connection->owner = owner;
    if (!connection->incoming || !connection->requests ||
            uv_pipe_init(lambda_uv_loop(), &connection->pipe, 0) < 0) {
        devtools_destroy(connection);
        return nullptr;
    }
    connection->initialized = true;
    connection->pipe.data = connection;
    if (uv_pipe_open(&connection->pipe, fd) < 0 ||
            uv_read_start((uv_stream_t*)&connection->pipe, devtools_allocate, devtools_read) < 0) {
        devtools_destroy(connection);
        return nullptr;
    }
    connection->opened = true;
    return connection;
}

bool devtools_connected(const DevtoolsConnection* connection) {
    return connection && connection->opened;
}

bool devtools_ready(const DevtoolsConnection* connection) {
    return devtools_connected(connection) && connection->handshake;
}

static void devtools_dispatch(DevtoolsConnection* connection, StrBuf* bytes) {
    DevtoolsExchange exchange;
    if (!exchange.input || memchr(bytes->str, 0, bytes->length) ||
            !utf8_valid(bytes->str, bytes->length)) {
        devtools_close(connection);
        return;
    }
    parse_mark(exchange.input, bytes->str);
    if (get_type_id(exchange.input->root) != LMD_TYPE_MAP) {
        devtools_close(connection);
        return;
    }
    MarkBuilder builder(exchange.input);
    MapReader request = MapReader::fromItem(exchange.input->root);
    Item id = request.get("id").item();
    if (get_type_id(id) != LMD_TYPE_INT || !isfinite(lambda_int_item_value(id)) ||
            lambda_int_item_value(id) <= 0 || lambda_int_item_value(id) > INT53_MAX) {
        devtools_close(connection);
        return;
    }
    Item value = ItemNull;
    Item operation = request.get("op").item();
    if (devtools_symbol_is(operation, "hello")) {
        if (!request.get("version").isInt() || request.get("version").asInt() != 1) {
            exchange.fail("version", "expected inspector protocol version 1");
        } else {
            auto operations = builder.array();
            operations.append(builder.createSymbolItem("hello"));
            for (size_t i = 0; i < connection->command_count; i++)
                operations.append(builder.createSymbolItem(connection->commands[i].name));
            value = builder.map().put("version", 1).put("ops", operations.final()).final();
            connection->handshake = true;
        }
    } else if (!connection->handshake) {
        exchange.fail("hello", "send hello before inspecting a target");
    } else if (get_type_id(operation) != LMD_TYPE_SYMBOL) {
        exchange.fail("invalid", "op must be a symbol");
    } else {
        bool found = false;
        for (size_t i = 0; i < connection->command_count; i++) {
            if (!devtools_symbol_is(operation, connection->commands[i].name)) continue;
            found = true;
            value = connection->commands[i].invoke(exchange, request, connection->owner);
            break;
        }
        if (!found) exchange.fail("unsupported", "operation is not supported by this target");
    }
    auto reply = builder.map();
    reply.put("id", id);
    if (exchange.error_code) {
        reply.put("error", builder.map().put("code", builder.createSymbolItem(exchange.error_code))
            .put("message", exchange.error_message).final());
    } else reply.put("value", value);
    if (exchange.revision >= 0) reply.put("rev", exchange.revision);
    devtools_send(connection, reply.final());
}

void devtools_dispatch_pending(DevtoolsConnection* connection) {
    if (!connection) return;
    // dispatch outside uv callbacks: CSS edits must not re-enter an application task.
    while (connection->requests->length && connection->opened) {
        StrBuf* bytes = (StrBuf*)connection->requests->data[0];
        arraylist_remove(connection->requests, 0);
        connection->queued_bytes -= bytes->length;
        devtools_dispatch(connection, bytes);
        strbuf_free(bytes);
    }
}

void devtools_destroy(DevtoolsConnection* connection) {
    if (!connection) return;
    devtools_close(connection);
    // close callbacks retire pending writes before the connection storage goes away.
    while (connection->initialized && !connection->closed)
        uv_run(connection->pipe.loop, UV_RUN_NOWAIT);
    if (connection->requests) {
        for (int i = 0; i < connection->requests->length; i++)
            strbuf_free((StrBuf*)connection->requests->data[i]);
        arraylist_free(connection->requests);
    }
    if (connection->incoming) strbuf_free(connection->incoming);
    mem_free(connection);
}
