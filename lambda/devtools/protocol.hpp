#pragma once

#include "../io/mark_builder.hpp"
#include "../core/mark_reader.hpp"
#include "../../lib/strbuf.h"

// requests and replies own their Mark data until the encoded frame is copied.
struct DevtoolsExchange {
    Pool* pool;
    Input* input;
    const char* error_code;
    const char* error_message;
    int64_t revision;
    DevtoolsExchange();
    ~DevtoolsExchange();
    Item fail(const char* code, const char* message);
};

typedef Item (*DevtoolsHandler)(DevtoolsExchange&, MapReader, void*);
struct DevtoolsCommand {
    const char* name;
    DevtoolsHandler invoke;
};

struct DevtoolsConnection;
constexpr size_t DEVTOOLS_MAX_BYTES = 8 * 1024 * 1024;

DevtoolsConnection* devtools_open(int fd, const DevtoolsCommand* commands,
    size_t command_count, void* owner);
// libuv receives bytes; the host explicitly dispatches after its application turn.
void devtools_dispatch_pending(DevtoolsConnection* connection);
bool devtools_connected(const DevtoolsConnection* connection);
bool devtools_ready(const DevtoolsConnection* connection);
void devtools_close(DevtoolsConnection* connection);
void devtools_destroy(DevtoolsConnection* connection);
bool devtools_send(DevtoolsConnection* connection, Item message);
bool devtools_symbol_is(Item value, const char* name);
