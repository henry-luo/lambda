#include "mcp_main.hpp"
#include "io/mcp_tools.hpp"
#include "../lib/log.h"
#include <stdio.h>
#include <string.h>

int exec_mcp(int argc, char** argv) {
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            fputs("Usage: lambda mcp [--transport stdio]\n", stdout);
            return 0;
        }
        if (!strcmp(argv[i], "--transport") && i + 1 < argc && !strcmp(argv[i + 1], "stdio")) { i++; continue; }
        log_error("MCP_CLI: unsupported option '%s'", argv[i]);
        return 1;
    }
    McpServer server = mcp_document_server();
    StrBuf* line = strbuf_new();
    bool too_large = false;
    int status = 0;
    for (;;) {
        int byte = fgetc(stdin);
        if (byte == EOF) break;
        if (byte != '\n') {
            if (line->length < MCP_MAX_MESSAGE_BYTES) strbuf_append_char(line, (char)byte);
            else too_large = true;
            continue;
        }
        // discard the entire oversized frame, never parse its accepted prefix.
        int http_status;
        StrBuf* response = mcp_dispatch(&server, too_large ? "" : line->str,
            too_large ? 0 : line->length, &http_status);
        if (!response) { status = 1; break; }
        if (response->length && (fwrite(response->str, 1, response->length, stdout) != response->length ||
                fputc('\n', stdout) == EOF || fflush(stdout) != 0)) status = 1;
        strbuf_free(response);
        strbuf_reset(line);
        too_large = false;
        if (status) break;
    }
    if (ferror(stdin)) status = 1;
    strbuf_free(line);
    return status;
}
