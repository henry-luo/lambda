#pragma once

#include "../lambda-data.hpp"
#include "../../lib/strbuf.h"

// each exchange owns its parsed values until its serialized reply is captured.
struct McpExchange {
    Pool* pool;
    Input* input;
    McpExchange();
    ~McpExchange();
    McpExchange(const McpExchange&) = delete;
    McpExchange& operator=(const McpExchange&) = delete;
};

constexpr size_t MCP_MAX_MESSAGE_BYTES = 8 * 1024 * 1024;
constexpr const char* MCP_PROTOCOL_VERSION = "2026-07-28";
constexpr const char* MCP_LEGACY_VERSION = "2025-11-25";

bool mcp_parse(McpExchange& exchange, const char* bytes, size_t length, Item* result);
StrBuf* mcp_serialize(McpExchange& exchange, Item value);
bool mcp_json_value(Item value, unsigned depth = 0, bool protocol_values = false);
bool mcp_text_equals(Item value, const char* text);

typedef Item (*McpToolCallback)(McpExchange& exchange, Item arguments, void* data);
struct McpTool {
    const char* name;
    const char* description;
    const char* input_schema;
    McpToolCallback call;
    void* data;
};

struct McpServer {
    const McpTool* tools;
    size_t tool_count;
    // legacy initialization belongs to a stdio process; modern calls are independent.
    bool initialized;
    bool initialize_replied;
    bool legacy_enabled;
};

// an empty output denotes a notification. HTTP status is selected by the profile.
StrBuf* mcp_dispatch(McpServer* server, const char* bytes, size_t length,
                    int* http_status, const char* version_header = nullptr,
                    const char* method_header = nullptr, const char* name_header = nullptr);

// request-owned client: returns a serialized result or peer JSON-RPC error.
StrBuf* mcp_http_request(Item endpoint, Item method, Item params, StrBuf* error);
