#include "language_backend.hpp"
#include "server.hpp"
#include "serve_utils.hpp"
#include "lambda/input/input.hpp"
#include "lambda/core/mark_reader.hpp"
#include "../runtime/transpiler.hpp"
#include "../runtime/interp.hpp"
#include "../runtime/ast.hpp"
#include "../runtime/runtime-state.h"
#include "../runtime/value_builder.hpp"
#include "lib/mempool.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>


bool lambda_serve_validate_handler(const char* path) {
    Runtime runtime;
    runtime_init(&runtime);
    Script* script = NULL;
    Input* output = run_script_mir(&runtime, NULL, (char*)path, false, &script);
    bool valid = false;
    if (output && script && script->ast_root && !item_is_error(output->root)) {
        AstNode* node = ((AstScript*)script->ast_root)->child;
        while (node) {
            if (node->node_type == AST_NODE_CONTENT) {
                node = ((AstListNode*)node)->item;
                continue;
            }
            if (node->node_type == AST_NODE_FUNC || node->node_type == AST_NODE_PROC) {
                AstFuncNode* function = (AstFuncNode*)node;
                TypeFunc* signature = (TypeFunc*)function->type;
                if (signature && signature->is_public && signature->param_count == 1 &&
                        function->name && !strcmp(function->name->chars, "handle")) {
                    valid = true;
                    break;
                }
            }
            node = node->next;
        }
    }
    if (output && output->pool) pool_destroy(output->pool);
    runtime_cleanup(&runtime);
    return valid;
}

static Item serve_text(const char* value, size_t length) {
    return {.item = s2it(heap_strcpy(value ? value : "", (int64_t)length))};
}

static bool serve_response_header_valid(const char* name, const char* value) {
    if (!name || !*name || !value) return false;
    for (const char* c = name; *c; c++) {
        if (!(isalnum((unsigned char)*c) || strchr("!#$%&'*+-.^_`|~", *c))) return false;
    }
    return !strchr(value, '\r') && !strchr(value, '\n') &&
        serve_strcasecmp(name, "Content-Length") && serve_strcasecmp(name, "Transfer-Encoding");
}

// The response buffer owns copies after the task-scoped runtime is destroyed (D6.3.3).
static bool serve_call_script(const char* path, HttpRequest* req, HttpResponse* resp) {
    Runtime runtime;
    runtime_init(&runtime);
    Script* script = NULL;
    Input* output = run_script_mir(&runtime, NULL, (char*)path, false, &script);
    bool ok = false;
    if (output && script && !item_is_error(output->root)) {
        EvalContext* eval = runtime_get_eval_context(&runtime);
        if (eval) {
            RuntimeExecutionScope execution(eval);
            RootedValueBuilder request(RootedValueBuilder::DATA_MAP);
            RootedValueBuilder headers(RootedValueBuilder::DATA_MAP);
            RootedValueBuilder query(RootedValueBuilder::DATA_MAP);
            request.put("method", serve_text(http_method_to_string(req->method), strlen(http_method_to_string(req->method))));
            request.put("path", serve_text(req->path, strlen(req->path)));
            request.put("body", serve_text(req->body, req->body_len));
            request.put("query_string", serve_text(req->query_string, req->query_string ? strlen(req->query_string) : 0));
            request.put("remote_addr", serve_text(req->remote_addr, strlen(req->remote_addr)));
            for (HttpHeader* header = req->headers; header; header = header->next) {
                char key[256];
                size_t length = strlen(header->name);
                if (length >= sizeof(key)) continue;
                for (size_t i = 0; i < length; i++) key[i] = (char)tolower((unsigned char)header->name[i]);
                key[length] = '\0';
                headers.put(key, serve_text(header->value, strlen(header->value)));
            }
            for (HttpHeader* entry = req->query_params; entry; entry = entry->next)
                query.put(entry->name, serve_text(entry->value, strlen(entry->value)));
            request.put("headers", headers.value.get());
            request.put("query", query.value.get());
            RootFrame roots(1);
            Item args[] = {request.value.get()};
            Rooted<Item> result(roots, interp_call_module_export(&runtime, script, "handle", args, 1));
            MapReader response = MapReader::fromItem(result.get());
            ItemReader status = response.get("status");
            ItemReader body = response.get("body");
            MapReader response_headers = response.get("headers").asMap();
            if (response.isValid() && status.isInt() && status.asInt() >= 200 && status.asInt() <= 599 &&
                    (body.isNull() || body.isString() || body.getType() == LMD_TYPE_BINARY)) {
                http_response_status(resp, (int)status.asInt());
                bool headers_ok = true;
                MapReader::EntryIterator entries = response_headers.entries();
                const char* key = NULL;
                ItemReader value;
                while (entries.next(&key, &value)) {
                    String* text = value.asString();
                    if (!text || memchr(text->chars, '\0', text->len) ||
                            !serve_response_header_valid(key, text->chars)) { headers_ok = false; break; }
                    http_response_set_header(resp, key, text->chars);
                }
                if (headers_ok) {
                    if (body.isString()) {
                        String* text = body.asString();
                        http_response_write(resp, text->chars, text->len);
                    } else if (body.getType() == LMD_TYPE_BINARY) {
                        Binary* bytes = body.item().get_binary();
                        http_response_write(resp, binary_data(bytes), binary_length(bytes));
                    }
                    ok = true;
                }
            }
        }
    }
    if (output && output->pool) pool_destroy(output->pool);
    runtime_cleanup(&runtime);
    return ok;
}

void lambda_serve_script_request(HttpRequest* req, HttpResponse* resp, void* user_data) {
    ScriptServerConfig* config = (ScriptServerConfig*)user_data;
    size_t prefix_length = config->spa_prefix ? strlen(config->spa_prefix) : 0;
    if (config->public_dir && prefix_length && req->method == HTTP_GET &&
            !strncmp(req->path, config->spa_prefix, prefix_length) &&
            (req->path[prefix_length] == '\0' || req->path[prefix_length] == '/')) {
        char index[4096];
        int n = snprintf(index, sizeof(index), "%s/index.html", config->public_dir);
        if (n > 0 && (size_t)n < sizeof(index) && serve_file_exists(index)) {
            http_response_file(resp, index, CONTENT_TYPE_HTML);
            return;
        }
    }
    if (!serve_call_script(config->handler, req, resp)) {
        http_response_error(resp, HTTP_500_INTERNAL_ERROR, "Handler execution failed");
    }
}

static int lambda_backend_init(LanguageBackend* self) { (void)self; return 0; }
static void lambda_backend_shutdown(LanguageBackend* self) { (void)self; }
static int lambda_backend_execute(LanguageBackend* self, const char* path, HttpRequest* req, HttpResponse* resp) {
    (void)self;
    if (serve_call_script(path, req, resp)) return BACKEND_OK;
    http_response_error(resp, HTTP_500_INTERNAL_ERROR, "Handler execution failed");
    return BACKEND_ERROR;
}
static int lambda_backend_compile(LanguageBackend* self, const char* path) {
    (void)self;
    return lambda_serve_validate_handler(path) ? BACKEND_OK : BACKEND_COMPILE_ERROR;
}
LanguageBackend* create_lambda_backend(void) {
    static const char* extensions[] = {".ls"};
    static LanguageBackend backend = {};
    backend.name = "lambda";
    backend.extensions = extensions;
    backend.extension_count = 1;
    backend.init = lambda_backend_init;
    backend.shutdown = lambda_backend_shutdown;
    backend.execute = lambda_backend_execute;
    backend.compile = lambda_backend_compile;
    return &backend;
}
