#include "mcp.hpp"
#include "mark_builder.hpp"
#include "../core/mark_reader.hpp"
#include "../input/input.hpp"
#include "../../lib/str.h"
#include "../../lib/shell.h"
#include "../../lib/base64.h"
#include <string.h>
#include <uv.h>

static bool mcp_http_header(StrBuf* header, const char* prefix, Item value) {
    if (get_type_id(value) != LMD_TYPE_STRING || memchr(value.get_chars(), 0, value.get_len())) return false;
    strbuf_append_str(header, prefix);
    const char* bytes = value.get_chars();
    size_t length = value.get_len();
    bool encode = length && (bytes[0] <= ' ' || bytes[length - 1] <= ' ');
    if (length >= 11 && !memcmp(bytes, "=?base64?", 9) && !memcmp(bytes + length - 2, "?=", 2)) encode = true;
    for (size_t i = 0; i < length; i++) if ((unsigned char)bytes[i] < 0x20 || (unsigned char)bytes[i] > 0x7e) encode = true;
    if (encode) {
        char* encoded = base64_encode_alloc(bytes, length, BASE64_STD);
        if (!encoded) return false;
        strbuf_append_all(header, 3, "=?base64?", encoded, "?=");
        mem_free(encoded);
    } else strbuf_append_str_n(header, bytes, length);
    return true;
}

static bool mcp_response_message(McpExchange& exchange, const char* bytes,
        size_t length, Item request_id, Item* result) {
    Item message;
    if (!mcp_parse(exchange, bytes, length, &message)) return false;
    MapReader response = MapReader::fromItem(message);
    Item id = response.get("id").item();
    if (!mcp_text_equals(response.get("jsonrpc").item(), "2.0") ||
            !mcp_text_equals(id, request_id.get_chars()) ||
            response.has("result") == response.has("error")) return false;
    if (response.has("result") && get_type_id(response.get("result").item()) != LMD_TYPE_MAP) return false;
    if (response.has("error")) {
        MapReader error = MapReader::fromItem(response.get("error").item());
        if (!error.get("code").isInt() || !error.get("message").isString()) return false;
    }
    *result = message;
    return true;
}

static bool mcp_sse_response(McpExchange& exchange, const char* bytes, size_t length,
        Item request_id, Item* result) {
    StrBuf* data = strbuf_new();
    bool found = false;
    size_t offset = 0;
    // SSE data can span lines and uses either CR, LF, or CRLF delimiters.
    while (offset < length) {
        size_t start = offset;
        while (offset < length && bytes[offset] != '\n' && bytes[offset] != '\r') offset++;
        size_t count = offset - start;
        if (offset < length && bytes[offset++] == '\r' && offset < length && bytes[offset] == '\n') offset++;
        if (!count) {
            if (data->length) {
                Item message;
                if (mcp_response_message(exchange, data->str, data->length, request_id, &message)) {
                    if (found) { found = false; break; }
                    *result = message;
                    found = true;
                }
                strbuf_reset(data);
            }
        } else if (count >= 5 && !memcmp(bytes + start, "data:", 5)) {
            start += 5; count -= 5;
            if (count && bytes[start] == ' ') { start++; count--; }
            if (data->length) strbuf_append_char(data, '\n');
            strbuf_append_str_n(data, bytes + start, count);
        }
    }
    strbuf_free(data);
    return found;
}

StrBuf* mcp_http_request(Item endpoint, Item method, Item params, StrBuf* error) {
    McpExchange exchange;
    if (!exchange.input) return nullptr;
    MarkBuilder builder(exchange.input);
    MapReader config = MapReader::fromItem(endpoint);
    Item url = config.get("url").item();
    if (get_type_id(endpoint) == LMD_TYPE_STRING) url = endpoint;
    if (get_type_id(url) != LMD_TYPE_STRING || memchr(url.get_chars(), 0, url.get_len()) ||
            (!str_starts_with_const(url.get_chars(), url.get_len(), "http://") &&
             !str_starts_with_const(url.get_chars(), url.get_len(), "https://")) ||
            get_type_id(method) != LMD_TYPE_STRING || !method.get_len() ||
            memchr(method.get_chars(), 0, method.get_len()) ||
            (get_type_id(params) != LMD_TYPE_MAP && get_type_id(params) != LMD_TYPE_NULL) ||
            !mcp_json_value(params)) {
        strbuf_append_str(error, "mcp.request requires an HTTP endpoint, a method string, and JSON object parameters");
        return nullptr;
    }
    int64_t timeout = config.has("timeout") ? config.get("timeout").asInt() : 30;
    if (timeout < 1 || timeout > 3600) {
        strbuf_append_str(error, "mcp.request timeout must be between 1 and 3600 seconds");
        return nullptr;
    }
    auto request_params = builder.map();
    MapReader original = MapReader::fromItem(params);
    auto fields = original.entries();
    const char* key;
    ItemReader value;
    while (fields.next(&key, &value)) if (strcmp(key, "_meta")) request_params.put(key, builder.deep_copy(value.item()));
    auto metadata = builder.map();
    MapReader previous_meta = MapReader::fromItem(original.get("_meta").item());
    auto original_meta = previous_meta.entries();
    while (original_meta.next(&key, &value)) {
        if (strcmp(key, "io.modelcontextprotocol/protocolVersion") &&
                strcmp(key, "io.modelcontextprotocol/clientInfo") &&
                strcmp(key, "io.modelcontextprotocol/clientCapabilities")) metadata.put(key, builder.deep_copy(value.item()));
    }
    metadata.put("io.modelcontextprotocol/protocolVersion", MCP_PROTOCOL_VERSION)
        .put("io.modelcontextprotocol/clientInfo", builder.map().put("name", "lambda").put("version", "0.1.0").final())
        .put("io.modelcontextprotocol/clientCapabilities", builder.map().final());
    request_params.put("_meta", metadata.final());
    StrBuf* id_text = strbuf_new();
    strbuf_append_str(id_text, "lambda-");
    strbuf_append_uint64(id_text, uv_hrtime());
    Item id = builder.createStringItem(id_text->str);
    strbuf_free(id_text);
    Item request = builder.map().put("jsonrpc", "2.0").put("id", id).put("method", builder.deep_copy(method))
        .put("params", request_params.final()).final();
    StrBuf* json = mcp_serialize(exchange, request);
    if (!json) { strbuf_append_str(error, "MCP request exceeds the message limit"); return nullptr; }
    StrBuf* method_header = strbuf_new();
    StrBuf* name_header = strbuf_new();
    StrBuf* auth_header = strbuf_new();
    bool valid = mcp_http_header(method_header, "Mcp-Method: ", method);
    if (mcp_text_equals(method, "tools/call") || mcp_text_equals(method, "prompts/get") || mcp_text_equals(method, "resources/read")) {
        valid = valid && mcp_http_header(name_header, "Mcp-Name: ",
            original.get(mcp_text_equals(method, "resources/read") ? "uri" : "name").item());
    }
    Item token_env = config.get("token_env").item();
    if (get_type_id(token_env) != LMD_TYPE_NULL) {
        if (get_type_id(token_env) != LMD_TYPE_STRING || memchr(token_env.get_chars(), 0, token_env.get_len())) valid = false;
        else {
            const char* token = shell_getenv(token_env.get_chars());
            if (!token || strpbrk(token, "\r\n")) valid = false;
            else strbuf_append_all(auth_header, 2, "Authorization: Bearer ", token);
        }
    }
    char* headers[] = {(char*)"Content-Type: application/json", (char*)"Accept: application/json, text/event-stream",
        (char*)"MCP-Protocol-Version: 2026-07-28", method_header->str, nullptr, nullptr};
    int header_count = 4;
    if (name_header->length) headers[header_count++] = name_header->str;
    if (auth_header->length) headers[header_count++] = auth_header->str;
    FetchConfig fetch = {};
    fetch.method = "POST";
    fetch.body = json->str;
    fetch.body_size = json->length;
    fetch.headers = headers;
    fetch.header_count = header_count;
    fetch.timeout_seconds = (long)timeout;
    fetch.max_redirects = 0;
    fetch.user_agent = "Lambda-MCP/0.1";
    fetch.verify_ssl = true;
    fetch.enable_compression = true;
    fetch.max_response_bytes = MCP_MAX_MESSAGE_BYTES;
    FetchResponse* response = valid ? http_fetch(url.get_chars(), &fetch) : nullptr;
    strbuf_free(json);
    strbuf_free(method_header);
    strbuf_free(name_header);
    strbuf_free(auth_header);
    if (!response) {
        strbuf_append_str(error, valid ? "MCP HTTP transport failed" : "Invalid MCP headers or token environment setting");
        return nullptr;
    }
    Item message = ItemNull;
    bool parsed = response->content_type && str_istarts_with_const(response->content_type, strlen(response->content_type), "text/event-stream")
        ? mcp_sse_response(exchange, response->data, response->size, id, &message)
        : mcp_response_message(exchange, response->data, response->size, id, &message);
    StrBuf* result = parsed ? mcp_serialize(exchange, message) : nullptr;
    if (!result) strbuf_append_format(error, "Invalid MCP reply (HTTP %ld)", response->status_code);
    free_fetch_response(response);
    return result;
}
