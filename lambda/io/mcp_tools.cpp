#include "mcp_tools.hpp"
#include "mark_builder.hpp"
#include "../core/mark_reader.hpp"
#include "../input/input.hpp"
#include "../format/format.h"
#include <string.h>

struct McpDocumentFormat {
    const char* name;
    const char* mime;
    bool input;
};

// this is a deployment allowlist, not a replacement for the parser/formatter registry.
static const McpDocumentFormat document_formats[] = {
    {"json", "application/json", true},
    {"yaml", "application/yaml", true},
    {"toml", "application/toml", true},
    {"xml", "application/xml", true},
    {"csv", "text/csv", true},
    {"markdown", "text/markdown", true},
    {"text", "text/plain", true},
    {"html", "text/html", false},
};

static Item tool_result(MarkBuilder& builder, Item content, bool failed) {
    Item text = builder.map().put("type", "text").put("text", content).final();
    return builder.map().put("content", builder.array().append(text).final()).put("isError", failed).final();
}

static Item tool_error(MarkBuilder& builder, const char* message) {
    return tool_result(builder, builder.createStringItem(message), true);
}

static bool tool_fields_allowed(const MapReader& args, const char* const* names, size_t count) {
    auto entries = args.entries();
    const char* key;
    ItemReader value;
    while (entries.next(&key, &value)) {
        bool found = false;
        for (size_t i = 0; i < count; i++) if (!strcmp(key, names[i])) found = true;
        if (!found) return false;
    }
    return true;
}

static Item convert_document(McpExchange& exchange, Item arguments, void*) {
    MarkBuilder builder(exchange.input);
    MapReader args = MapReader::fromItem(arguments);
    MapReader document = MapReader::fromItem(args.get("document").item());
    Item source = document.get("text").item();
    static const char* const fields[] = {"document", "from", "to"};
    if (!tool_fields_allowed(args, fields, sizeof(fields) / sizeof(fields[0])) ||
            !args.get("to").isString() || (args.has("from") && !args.get("from").isString())) {
        return tool_error(builder, "Expected document, string to, and optional string from");
    }
    if (!mcp_text_equals(document.get("kind").item(), "text") || !document.get("mimeType").isString() ||
            (document.has("name") && !document.get("name").isString()) ||
            get_type_id(source) != LMD_TYPE_STRING || memchr(source.get_chars(), 0, source.get_len())) {
        return tool_error(builder, "document must be a text envelope containing UTF-8 source");
    }
    const McpDocumentFormat* from = nullptr;
    const McpDocumentFormat* to = nullptr;
    for (const McpDocumentFormat& format : document_formats) {
        if (format.input && (mcp_text_equals(args.get("from").item(), format.name) ||
                (!args.has("from") && mcp_text_equals(document.get("mimeType").item(), format.mime)))) from = &format;
        if (mcp_text_equals(args.get("to").item(), format.name)) to = &format;
    }
    if (!from || !to || !strcmp(to->name, "csv")) return tool_error(builder, "Unsupported or missing document format; use lambda.capabilities");
    Input* parsed = Input::create(exchange.pool);
    if (!parsed) return tool_error(builder, "Could not allocate document input");
    if (!strcmp(from->name, "json")) {
        if (!mcp_parse(exchange, source.get_chars(), source.get_len(), &parsed->root)) {
            return tool_error(builder, "Invalid JSON document");
        }
    } else if (!strcmp(from->name, "text")) {
        MarkBuilder document_builder(parsed);
        parsed->root = document_builder.createStringItem(source.get_chars(), source.get_len());
    } else if (!input_parse_source_exact(from->name, parsed, source.get_chars())) {
        return tool_error(builder, "Unsupported input parser");
    }
    if (parsed->parse_failed || get_type_id(parsed->root) == LMD_TYPE_ERROR) {
        return tool_error(builder, parsed->parse_error_message ? parsed->parse_error_message : "Document parsing failed");
    }
    String* type = builder.createString(to->name);
    String* output = format_data(parsed->root, type, nullptr, exchange.pool);
    if (!output || output->len > MCP_MAX_MESSAGE_BYTES / 2) return tool_error(builder, "Conversion failed or output exceeds inline limit");
    Item document_out = builder.map().put("kind", "text").put("mimeType", to->mime)
        .put("text", builder.createStringItem(output->chars, output->len)).put("byteLength", (int64_t)output->len).final();
    Item structured = builder.map().put("schemaVersion", "1")
        .put("documents", builder.array().append(document_out).final())
        .put("diagnostics", builder.array().final()).final();
    Item text = builder.map().put("type", "text").put("text", "Document converted; output is in structuredContent.documents").final();
    return builder.map().put("content", builder.array().append(text).final())
        .put("isError", false).put("structuredContent", structured).final();
}

static Item capabilities(McpExchange& exchange, Item arguments, void*) {
    MarkBuilder builder(exchange.input);
    if (MapReader::fromItem(arguments).size()) return tool_error(builder, "lambda.capabilities takes no arguments");
    auto formats = builder.array();
    for (const McpDocumentFormat& format : document_formats) {
        formats.append(builder.map().put("name", format.name).put("mimeType", format.mime)
            .put("input", format.input).put("output", strcmp(format.name, "csv") != 0).final());
    }
    Item supported = builder.map().put("formats", formats.final())
        .put("maxMessageBytes", (int64_t)MCP_MAX_MESSAGE_BYTES)
        .put("operations", builder.array().append("lambda.convert").append("lambda.capabilities").final()).final();
    Item text = builder.map().put("type", "text").put("text", "Lambda MCP document capabilities").final();
    return builder.map().put("content", builder.array().append(text).final())
        .put("structuredContent", supported).put("isError", false).final();
}

McpServer mcp_document_server() {
    static const McpTool tools[] = {
        {"lambda.convert", "Convert a captured document between supported formats",
         R"({"type":"object","properties":{"document":{"type":"object","properties":{"kind":{"const":"text"},"mimeType":{"type":"string"},"text":{"type":"string"},"name":{"type":"string"}},"required":["kind","mimeType","text"]},"from":{"type":"string"},"to":{"type":"string"}},"required":["document","to"],"additionalProperties":false})",
         convert_document, nullptr},
        {"lambda.capabilities", "Inspect document formats, operations and limits",
         R"({"type":"object","additionalProperties":false})", capabilities, nullptr},
    };
    return {tools, sizeof(tools) / sizeof(tools[0]), false, false, true};
}
