#include "../lambda.hpp"
#include "../io/mcp.hpp"
#include "../io/mark_builder.hpp"
#include "../core/mark_reader.hpp"
#include "../format/format.h"
#include "../input/input.hpp"
#include "lambda-root-frame.hpp"
#include "lambda-error.h"
#include "runtime-state.h"

extern "C" Item pn_mcp_request(Item endpoint, Item method, Item params) {
#ifdef LAMBDA_NO_NETWORK
    set_runtime_error(ERR_INVALID_OPERATION, "mcp.request is excluded from this build");
    return ItemError;
#else
    if (g_dry_run) {
        set_runtime_error(ERR_INVALID_OPERATION, "MCP requests are disabled in dry-run mode");
        return ItemError;
    }
    StrBuf* error = strbuf_new();
    StrBuf* reply = mcp_http_request(endpoint, method, params, error);
    if (!reply) {
        set_runtime_error(ERR_INVALID_OPERATION, "%s", error->str);
        strbuf_free(error);
        return ItemError;
    }
    strbuf_free(error);
    McpExchange exchange;
    Item message;
    if (!mcp_parse(exchange, reply->str, reply->length, &message)) {
        strbuf_free(reply);
        set_runtime_error(ERR_INVALID_OPERATION, "mcp.request received invalid JSON");
        return ItemError;
    }
    strbuf_free(reply);
    MapReader envelope = MapReader::fromItem(message);
    Item payload = envelope.get(envelope.has("error") ? "error" : "result").item();
    StrBuf* json = mcp_serialize(exchange, payload);
    if (!json) return ItemError;
    if (envelope.has("error")) {
        set_runtime_error(ERR_INVALID_OPERATION, "MCP remote error: %s", json->str);
        strbuf_free(json);
        return ItemError;
    }
    // parse owns its Input snapshot; no value may borrow this exchange's arena.
    RootFrame roots(2);
    Rooted<Item> source(roots, (Item){.item = s2it(heap_strcpy(json->str, json->length))});
    strbuf_free(json);
    Rooted<Item> format(roots, (Item){.item = s2it(heap_strcpy("json", 4))});
    return fn_parse2(source.get(), format.get());
#endif
}

extern "C" Item pn_mcp_call_tool(Item endpoint, Item name, Item arguments) {
#ifdef LAMBDA_NO_NETWORK
    return pn_mcp_request(endpoint, name, arguments);
#else
    McpExchange exchange;
    if (!exchange.input) return ItemError;
    MarkBuilder builder(exchange.input);
    Item params = builder.map().put("name", builder.deep_copy(name))
        .put("arguments", builder.deep_copy(arguments)).final();
    Item method = builder.createStringItem("tools/call");
    return pn_mcp_request(endpoint, method, params);
#endif
}
