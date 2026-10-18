#include "mcp.hpp"
#include "mark_builder.hpp"
#include "../core/mark_reader.hpp"
#include "../input/input.hpp"
#include "../input/input-parsers.h"
#include "../format/format.h"
#include "../../lib/mem_context.h"
#include "../../lib/mem_factory.h"
#include "../../lib/mem.h"
#include "../../lib/utf.h"
#include "../../lib/base64.h"
#include <math.h>
#include <string.h>

McpExchange::McpExchange() {
    pool = mem_pool_create(nullptr, MEM_ROLE_TEMP, "mcp.exchange");
    input = pool ? Input::create(pool) : nullptr;
}

McpExchange::~McpExchange() {
    if (pool) mem_pool_destroy(pool);
}

bool mcp_text_equals(Item value, const char* text) {
    return get_type_id(value) == LMD_TYPE_STRING && value.get_len() == strlen(text) &&
        !memcmp(value.get_chars(), text, strlen(text));
}

bool mcp_parse(McpExchange& exchange, const char* bytes, size_t length, Item* result) {
    if (!exchange.input || !bytes || !length || length > MCP_MAX_MESSAGE_BYTES ||
            memchr(bytes, 0, length) || !utf8_valid(bytes, length)) return false;
    char* source = (char*)pool_alloc(exchange.pool, length + 1);
    if (!source) return false;
    memcpy(source, bytes, length);
    source[length] = 0;
    bool ok = false;
    *result = parse_json_to_item_strict(exchange.input, source, &ok);
    return ok;
}

bool mcp_json_value(Item value, unsigned depth, bool protocol_values) {
    // leave headroom below the shared formatter's recursion bound; never emit its debug sentinel.
    if (depth > 44) return false;
    ItemReader reader(value.to_const());
    switch (get_type_id(value)) {
    case LMD_TYPE_NULL: case LMD_TYPE_BOOL: case LMD_TYPE_INT:
        return true;
    case LMD_TYPE_STRING:
        return utf8_valid(value.get_chars(), value.get_len());
    case LMD_TYPE_FLOAT:
        return isfinite(value.get_double());
    case LMD_TYPE_INT64:
        return protocol_values || (value.get_int64() >= -9007199254740991LL && value.get_int64() <= 9007199254740991LL);
    case LMD_TYPE_UINT64:
        return protocol_values || value.get_uint64() <= 9007199254740991ULL;
    case LMD_TYPE_DECIMAL:
        return protocol_values;
    case LMD_TYPE_ARRAY: case LMD_TYPE_ARRAY_NUM:
        for (int64_t i = 0; i < reader.asArray().length(); i++) {
            if (!mcp_json_value(reader.asArray().get(i).item(), depth + 1, protocol_values)) return false;
        }
        return true;
    case LMD_TYPE_MAP: {
        MapReader map = reader.asMap();
        auto entries = map.entries();
        const char* key;
        ItemReader child;
        while (entries.next(&key, &child)) {
            if (!mcp_json_value(child.item(), depth + 1, protocol_values)) return false;
        }
        return true;
    }
    default: return false;
    }
}

StrBuf* mcp_serialize(McpExchange& exchange, Item value) {
    if (!exchange.input || !mcp_json_value(value, 0, true)) return nullptr;
    String* json = format_json(exchange.pool, value);
    if (!json || json->len > MCP_MAX_MESSAGE_BYTES) return nullptr;
    StrBuf* result = strbuf_new_cap(json->len + 1);
    if (!result) return nullptr;
    // stdio frames contain one JSON value per line; preserve quoted whitespace/escapes.
    bool quoted = false, escaped = false;
    for (size_t i = 0; i < json->len; i++) {
        char byte = json->chars[i];
        if (!quoted && (byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n')) continue;
        strbuf_append_char(result, byte);
        if (escaped) escaped = false;
        else if (quoted && byte == '\\') escaped = true;
        else if (byte == '"') quoted = !quoted;
    }
    return result;
}

static Item mcp_result(MarkBuilder& builder, bool modern) {
    auto result = builder.map();
    if (modern) result.put("resultType", "complete");
    return result.final();
}

static Item mcp_versions(MarkBuilder& builder, const McpServer* server) {
    auto versions = builder.array();
    versions.append(MCP_PROTOCOL_VERSION);
    if (server->legacy_enabled) versions.append(MCP_LEGACY_VERSION);
    return versions.final();
}

static Item mcp_error(MarkBuilder& builder, Item id, int code, const char* message) {
    Item error = builder.map().put("code", code).put("message", message).final();
    return builder.map().put("jsonrpc", "2.0").put("id", id).put("error", error).final();
}

static bool mcp_header_matches(const char* header, Item expected) {
    if (!header || get_type_id(expected) != LMD_TYPE_STRING) return false;
    size_t length = strlen(header);
    if (length > 11 && !memcmp(header, "=?base64?", 9) && !strcmp(header + length - 2, "?=")) {
        size_t decoded_length = 0;
        uint8_t* decoded = base64_decode(header + 9, length - 11, &decoded_length);
        bool match = decoded && decoded_length == expected.get_len() &&
            !memcmp(decoded, expected.get_chars(), decoded_length);
        mem_free(decoded);
        return match;
    }
    return length == expected.get_len() && !memcmp(header, expected.get_chars(), length);
}

StrBuf* mcp_dispatch(McpServer* server, const char* bytes, size_t length,
        int* http_status, const char* version_header, const char* method_header,
        const char* name_header) {
    *http_status = 200;
    McpExchange exchange;
    if (!exchange.input) return nullptr;
    MarkBuilder builder(exchange.input);
    Item request = ItemNull;
    if (!mcp_parse(exchange, bytes, length, &request)) {
        *http_status = 400;
        return mcp_serialize(exchange, mcp_error(builder, ItemNull, -32700, "Invalid JSON"));
    }
    MapReader message = MapReader::fromItem(request);
    Item id = message.get("id").item();
    Item method = message.get("method").item();
    Item params = message.get("params").item();
    TypeId id_type = get_type_id(id);
    if (get_type_id(request) != LMD_TYPE_MAP || !mcp_text_equals(message.get("jsonrpc").item(), "2.0") ||
            get_type_id(method) != LMD_TYPE_STRING ||
            (message.has("id") && id_type != LMD_TYPE_STRING && id_type != LMD_TYPE_INT && id_type != LMD_TYPE_INT64) ||
            (message.has("params") && get_type_id(params) != LMD_TYPE_MAP)) {
        *http_status = 400;
        return mcp_serialize(exchange, mcp_error(builder, ItemNull, -32600, "Invalid MCP request"));
    }
    MapReader args = MapReader::fromItem(params);
    MapReader meta = MapReader::fromItem(args.get("_meta").item());
    Item version = meta.get("io.modelcontextprotocol/protocolVersion").item();
    bool modern = get_type_id(version) != LMD_TYPE_NULL;
    if ((version_header && !modern) || (modern &&
            get_type_id(meta.get("io.modelcontextprotocol/clientCapabilities").item()) != LMD_TYPE_MAP)) {
        *http_status = 400;
        return mcp_serialize(exchange, mcp_error(builder, id,
            version_header ? -32020 : -32602, "Required MCP request metadata is missing"));
    }
    if (modern && !mcp_text_equals(version, MCP_PROTOCOL_VERSION)) {
        *http_status = 400;
        Item data = builder.map().put("supported", mcp_versions(builder, server))
            .put("requested", version).final();
        Item error = builder.map().put("code", -32022).put("message", "Unsupported protocol version").put("data", data).final();
        return mcp_serialize(exchange, builder.map().put("jsonrpc", "2.0").put("id", id).put("error", error).final());
    }
    if (!modern && !server->legacy_enabled) {
        *http_status = 400;
        return mcp_serialize(exchange, mcp_error(builder, id, -32600,
            "This endpoint requires protocol 2026-07-28 request metadata"));
    }
    // a non-null header selects HTTP validation; stdio carries only body metadata.
    if (modern && version_header && (!mcp_header_matches(version_header, version) ||
            !mcp_header_matches(method_header, method) ||
            ((mcp_text_equals(method, "tools/call") || mcp_text_equals(method, "prompts/get")) &&
             !mcp_header_matches(name_header, args.get("name").item())) ||
            (mcp_text_equals(method, "resources/read") && !mcp_header_matches(name_header, args.get("uri").item())))) {
        *http_status = 400;
        return mcp_serialize(exchange, mcp_error(builder, id, -32020, "MCP headers do not match request"));
    }
    if (!message.has("id")) {
        if (!modern && server->initialize_replied &&
                mcp_text_equals(method, "notifications/initialized")) server->initialized = true;
        *http_status = 202;
        return strbuf_new();
    }
    Item result = ItemNull;
    if (mcp_text_equals(method, "initialize") && !modern) {
        if (server->initialize_replied) return mcp_serialize(exchange,
            mcp_error(builder, id, -32600, "MCP connection was already initialized"));
        server->initialize_replied = true;
        result = builder.map().put("protocolVersion", MCP_LEGACY_VERSION)
            .put("capabilities", builder.map().put("tools", builder.map().final()).final())
            .put("serverInfo", builder.map().put("name", "lambda").put("version", "0.1.0").final()).final();
    } else if (mcp_text_equals(method, "server/discover") && modern) {
        result = builder.map().put("resultType", "complete")
            .put("supportedVersions", mcp_versions(builder, server))
            .put("capabilities", builder.map().put("tools", builder.map().final()).final())
            .put("_meta", builder.map().put("io.modelcontextprotocol/serverInfo",
                builder.map().put("name", "lambda").put("version", "0.1.0").final()).final()).final();
    } else if (!modern && !server->initialized && !mcp_text_equals(method, "ping")) {
        return mcp_serialize(exchange, mcp_error(builder, id, -32600, "Initialize the MCP connection first"));
    } else if (mcp_text_equals(method, "ping")) {
        result = mcp_result(builder, modern);
    } else if (mcp_text_equals(method, "tools/list")) {
        if (args.has("cursor")) return mcp_serialize(exchange, mcp_error(builder, id, -32602, "Invalid cursor"));
        auto tools = builder.array();
        for (size_t i = 0; i < server->tool_count; i++) {
            const McpTool& tool = server->tools[i];
            Item schema = ItemNull;
            if (!mcp_parse(exchange, tool.input_schema, strlen(tool.input_schema), &schema)) return nullptr;
            tools.append(builder.map().put("name", tool.name).put("description", tool.description)
                .put("inputSchema", schema).final());
        }
        auto list = builder.map();
        list.put("tools", tools.final());
        if (modern) list.put("resultType", "complete");
        result = list.final();
    } else if (mcp_text_equals(method, "tools/call")) {
        const McpTool* selected = nullptr;
        for (size_t i = 0; i < server->tool_count; i++) {
            if (mcp_text_equals(args.get("name").item(), server->tools[i].name)) selected = &server->tools[i];
        }
        Item arguments = args.get("arguments").item();
        if (!selected || (args.has("arguments") && get_type_id(arguments) != LMD_TYPE_MAP)) {
            return mcp_serialize(exchange, mcp_error(builder, id, -32602, "Unknown tool or invalid arguments"));
        }
        Item tool_result = selected->call(exchange, arguments, selected->data);
        if (get_type_id(tool_result) != LMD_TYPE_MAP) {
            return mcp_serialize(exchange, mcp_error(builder, id, -32603, "Tool handler did not return an MCP result"));
        }
        auto response = builder.map();
        if (modern) response.put("resultType", "complete");
        MapReader tool_output = MapReader::fromItem(tool_result);
        auto fields = tool_output.entries();
        const char* key;
        ItemReader value;
        while (fields.next(&key, &value)) response.put(key, value.item());
        result = response.final();
    } else {
        *http_status = modern ? 404 : 200;
        return mcp_serialize(exchange, mcp_error(builder, id, -32601, "Method not found"));
    }
    StrBuf* reply = mcp_serialize(exchange, builder.map().put("jsonrpc", "2.0").put("id", id).put("result", result).final());
    return reply ? reply : mcp_serialize(exchange, mcp_error(builder, id, -32603, "MCP result exceeds supported limits"));
}
