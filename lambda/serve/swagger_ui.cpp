/**
 * @file swagger_ui.cpp
 * @brief Embedded Swagger UI handler
 *
 * Serves a self-contained Swagger UI HTML page that loads the spec
 * from the configured URL. Uses Swagger UI CDN assets to keep the
 * binary size small.
 */

#include "swagger_ui.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include <cstring>
#include "../../lib/mem.h"

// ============================================================================
// Swagger UI HTML Template
// ============================================================================

static const char SWAGGER_UI_TEMPLATE[] =
    "<!DOCTYPE html>\n"
    "<html lang=\"en\">\n"
    "<head>\n"
    "  <meta charset=\"UTF-8\">\n"
    "  <title>API Documentation</title>\n"
    "  <link rel=\"stylesheet\" href=\"https://unpkg.com/swagger-ui-dist@5/swagger-ui.css\">\n"
    "  <style>\n"
    "    html { box-sizing: border-box; overflow-y: scroll; }\n"
    "    *, *::before, *::after { box-sizing: inherit; }\n"
    "    body { margin: 0; background: #fafafa; }\n"
    "    .topbar { display: none; }\n"
    "  </style>\n"
    "</head>\n"
    "<body>\n"
    "  <div id=\"swagger-ui\"></div>\n"
    "  <script src=\"https://unpkg.com/swagger-ui-dist@5/swagger-ui-bundle.js\"></script>\n"
    "  <script>\n"
    "    SwaggerUIBundle({\n"
    "      url: \"SPEC_URL_PLACEHOLDER\",\n"
    "      dom_id: '#swagger-ui',\n"
    "      deepLinking: true,\n"
    "      presets: [\n"
    "        SwaggerUIBundle.presets.apis,\n"
    "        SwaggerUIBundle.SwaggerUIStandalonePreset\n"
    "      ],\n"
    "      layout: 'BaseLayout'\n"
    "    });\n"
    "  </script>\n"
    "</body>\n"
    "</html>\n";

// ============================================================================
// Handler State
// ============================================================================

typedef struct SwaggerUIState {
    char *html;     // generated HTML with spec URL substituted
} SwaggerUIState;

static void handle_swagger_ui(HttpRequest* req, HttpResponse* resp, void* user_data) {
    SwaggerUIState *state = (SwaggerUIState*)user_data;

    http_response_status(resp, 200);
    http_response_set_header(resp, "Content-Type", "text/html; charset=utf-8");
    http_response_set_header(resp, "Cache-Control", "public, max-age=3600");
    http_response_write_str(resp, state->html);
}

// ============================================================================
// Public API
// ============================================================================

int swagger_ui_serve(Server *server, const char *docs_path, const char *spec_url) {
    if (!server || !docs_path || !spec_url) return -1;

    // build HTML with spec URL substituted
    const char *placeholder = "SPEC_URL_PLACEHOLDER";
    const char *pos = strstr(SWAGGER_UI_TEMPLATE, placeholder);
    if (!pos) return -1;

    size_t prefix_len = pos - SWAGGER_UI_TEMPLATE;
    size_t suffix_len = strlen(pos + strlen(placeholder));
    size_t html_len = prefix_len + strlen(spec_url) + suffix_len;

    // allocate state (leaked intentionally — lives for server lifetime)
    SwaggerUIState *state = (SwaggerUIState*)mem_calloc(1, sizeof(SwaggerUIState), MEM_CAT_SERVE);
    state->html = (char*)mem_alloc(html_len + 1, MEM_CAT_SERVE);

    memcpy(state->html, SWAGGER_UI_TEMPLATE, prefix_len);
    memcpy(state->html + prefix_len, spec_url, strlen(spec_url));
    memcpy(state->html + prefix_len + strlen(spec_url),
           pos + strlen(placeholder), suffix_len + 1);

    server_get(server, docs_path, handle_swagger_ui, state);

    log_info("swagger_ui: serving at %s (spec: %s)", docs_path, spec_url);
    return 0;
}
