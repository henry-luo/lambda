// openapi/docs.ls — Generate Swagger UI documentation pages
//
// Produces HTML for the /docs endpoint (Swagger UI) and
// JSON for the /openapi.json endpoint from a parsed OpenAPI spec.

import util: .util

// ============================================================
// Public API
// ============================================================

// Generate the /openapi.json response body from a spec.
// Re-formats the parsed spec back to JSON string.
//
// spec: parsed OpenAPI spec map
// returns: JSON string
pub fn openapi_json(spec) => format(spec, 'json')

// Generate a Swagger UI HTML page that loads the spec from
// the given spec_url endpoint.
//
// spec_url: URL path to the openapi.json endpoint (e.g. "/openapi.json")
// title: page title (optional, defaults to spec title if available)
// returns: HTML string
pub fn swagger_ui_html(spec_url, title) {
    let page_title = if (title != null and len(title) > 0) title
                     else "API Documentation";
    swagger_page(spec_url, page_title)
}

// Generate a Swagger UI HTML page with the spec embedded inline.
// No separate /openapi.json fetch needed.
//
// spec: parsed OpenAPI spec map
// returns: HTML string
pub fn swagger_ui_inline(spec) {
    let title = util.get_or(spec.info, "title", "API Documentation");
    let spec_json = format(spec, 'json');
    swagger_page_inline(spec_json, title)
}

// Generate a simple Redoc HTML page.
//
// spec_url: URL to the openapi.json endpoint
// title: page title
// returns: HTML string
pub fn redoc_html(spec_url, title) {
    let page_title = if (title != null and len(title) > 0) title
                     else "API Documentation";
    redoc_page(spec_url, page_title)
}

// ============================================================
// Route summary — list all paths and methods from the spec
// ============================================================

// Extract a summary of all routes from the spec.
//
// spec: parsed OpenAPI spec map
// returns: array of {path, method, summary, operation_id, tags} maps
pub fn route_summary(spec) {
    let paths = spec.paths;
    if (paths == null) []
    else {
        for (path, path_obj at paths)
            for (method in util.METHODS where path_obj[method] != null) {
                let op = path_obj[method];
                {
                    path: string(path),
                    method: method,
                    summary: util.get_or(op, "summary", ""),
                    operation_id: util.get_or(op, "operationId", ""),
                    tags: util.get_or(op, "tags", [])
                }
            }
    }
}

// ============================================================
// Swagger UI HTML template
// ============================================================

fn swagger_page(spec_url, title) =>
    "<!DOCTYPE html>\n"
    ++ "<html lang=\"en\">\n"
    ++ "<head>\n"
    ++ "  <meta charset=\"UTF-8\">\n"
    ++ "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
    ++ "  <title>" ++ title ++ "</title>\n"
    ++ "  <link rel=\"stylesheet\" href=\"https://unpkg.com/swagger-ui-dist@5/swagger-ui.css\">\n"
    ++ "</head>\n"
    ++ "<body>\n"
    ++ "  <div id=\"swagger-ui\"></div>\n"
    ++ "  <script src=\"https://unpkg.com/swagger-ui-dist@5/swagger-ui-bundle.js\"></script>\n"
    ++ "  <script>\n"
    ++ "    SwaggerUIBundle({\n"
    ++ "      url: \"" ++ spec_url ++ "\",\n"
    ++ "      dom_id: '#swagger-ui',\n"
    ++ "      presets: [\n"
    ++ "        SwaggerUIBundle.presets.apis,\n"
    ++ "        SwaggerUIBundle.SwaggerUIStandalonePreset\n"
    ++ "      ],\n"
    ++ "      layout: 'StandaloneLayout'\n"
    ++ "    });\n"
    ++ "  </script>\n"
    ++ "</body>\n"
    ++ "</html>"

// ============================================================
// Swagger UI page with inline spec
// ============================================================

fn swagger_page_inline(spec_json, title) =>
    "<!DOCTYPE html>\n"
    ++ "<html lang=\"en\">\n"
    ++ "<head>\n"
    ++ "  <meta charset=\"UTF-8\">\n"
    ++ "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
    ++ "  <title>" ++ title ++ "</title>\n"
    ++ "  <link rel=\"stylesheet\" href=\"https://unpkg.com/swagger-ui-dist@5/swagger-ui.css\">\n"
    ++ "</head>\n"
    ++ "<body>\n"
    ++ "  <div id=\"swagger-ui\"></div>\n"
    ++ "  <script src=\"https://unpkg.com/swagger-ui-dist@5/swagger-ui-bundle.js\"></script>\n"
    ++ "  <script>\n"
    ++ "    SwaggerUIBundle({\n"
    ++ "      spec: " ++ spec_json ++ ",\n"
    ++ "      dom_id: '#swagger-ui',\n"
    ++ "      presets: [\n"
    ++ "        SwaggerUIBundle.presets.apis,\n"
    ++ "        SwaggerUIBundle.SwaggerUIStandalonePreset\n"
    ++ "      ],\n"
    ++ "      layout: 'StandaloneLayout'\n"
    ++ "    });\n"
    ++ "  </script>\n"
    ++ "</body>\n"
    ++ "</html>"

// ============================================================
// Redoc HTML template
// ============================================================

fn redoc_page(spec_url, title) =>
    "<!DOCTYPE html>\n"
    ++ "<html lang=\"en\">\n"
    ++ "<head>\n"
    ++ "  <meta charset=\"UTF-8\">\n"
    ++ "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
    ++ "  <title>" ++ title ++ "</title>\n"
    ++ "</head>\n"
    ++ "<body>\n"
    ++ "  <redoc spec-url=\"" ++ spec_url ++ "\"></redoc>\n"
    ++ "  <script src=\"https://cdn.redoc.ly/redoc/latest/bundles/redoc.standalone.js\"></script>\n"
    ++ "</body>\n"
    ++ "</html>"
