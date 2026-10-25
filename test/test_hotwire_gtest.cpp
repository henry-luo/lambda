#include <gtest/gtest.h>
#include "../lambda/serve/server.hpp"
#include "../lambda/serve/serve_utils.hpp"
#include "../lambda/serve/body_parser.hpp"
#include "../lib/file.h"
#include "../lib/strbuf.h"
#include "../lib/shell.h"
#include "../lib/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct HotwireServerState {
    uv_mutex_t mutex;
    StrBuf* requests;
};

static const char* hotwire_header(HttpRequest* request, const char* name) {
    const char* value = http_request_header(request, name);
    return value ? value : "";
}

static void hotwire_route(HttpRequest* request, HttpResponse* response, void* data) {
    HotwireServerState* state = (HotwireServerState*)data;
    uv_mutex_lock(&state->mutex);
    strbuf_append_format(state->requests, "%s %s\naccept=%s\nframe=%s\ntype=%s\nbody=",
        http_method_to_string(request->method), request->path,
        hotwire_header(request, "Accept"), hotwire_header(request, "Turbo-Frame"),
        hotwire_header(request, "Content-Type"));
    if (request->body) strbuf_append_str_n(state->requests, request->body, request->body_len);
    strbuf_append_char(state->requests, '\n');
    uv_mutex_unlock(&state->mutex);
    http_response_set_header(response, "Cache-Control", "no-store");
    if (!strcmp(request->path, "/health")) {
        http_response_text(response, 200, "lambda-hotwire");
    } else if (!strcmp(request->path, "/")) {
        http_response_html(response, 200,
            "<!doctype html><html><head><title>Hotwire HTTP</title>"
            "<meta name=\"turbo-prefetch\" content=\"false\"></head><body>"
            "<main id=\"home\">Home</main></body></html>");
    } else if (!strcmp(request->path, "/frame") || !strcmp(request->path, "/frame-next")) {
        http_response_write_fmt(response,
            "<!doctype html><html><head><title>Frame response</title></head><body>"
            "<p id=\"outside-frame\">must stay outside</p><turbo-frame id=\"message\">"
            "<span id=\"remote\" data-controller=\"remote\">%s</span>"
            "<a id=\"frame-next\" href=\"/frame-next\">Next frame</a>"
            "</turbo-frame></body></html>",
            !strcmp(request->path, "/frame") ? "Frame one" : "Frame two");
        http_response_set_header(response, "Content-Type", "text/html; charset=utf-8");
        http_response_send(response);
    } else if (!strcmp(request->path, "/page") || !strcmp(request->path, "/saved")) {
        bool saved = !strcmp(request->path, "/saved");
        http_response_write_fmt(response,
            "<!doctype html><html><head><title>%s</title>"
            "<meta name=\"turbo-prefetch\" content=\"false\"></head><body>"
            "<main id=\"%s\" data-controller=\"remote\">%s</main></body></html>",
            saved ? "Saved" : "Next page", saved ? "saved" : "page", saved ? "Saved Hello Hotwire" : "Next page");
        http_response_set_header(response, "Content-Type", "text/html; charset=utf-8");
        http_response_send(response);
    } else if (!strcmp(request->path, "/save") && request->method == HTTP_POST) {
        HttpHeader* fields = body_parse_form(request->body, request->body_len);
        const char* title = http_header_find(fields, "title");
        const char* commit = http_header_find(fields, "commit");
        if (title && commit && !strcmp(title, "Hello Hotwire") && !strcmp(commit, "Save"))
            http_response_redirect(response, "/saved", 303);
        else http_response_error(response, 400, "Unexpected form data");
        http_header_free(fields);
    } else if (!strcmp(request->path, "/invalid") && request->method == HTTP_POST) {
        http_response_html(response, 422,
            "<!doctype html><html><head><title>Validation error</title>"
            "<meta name=\"turbo-prefetch\" content=\"false\"></head><body>"
            "<p id=\"validation-error\">Title is required</p></body></html>");
    } else if (!strcmp(request->path, "/stream") && request->method == HTTP_POST) {
        http_response_set_header(response, "Content-Type", "text/vnd.turbo-stream.html; charset=utf-8");
        http_response_write_str(response,
            "<turbo-stream action=\"append\" target=\"notifications\"><template>"
            "<p id=\"notification\" data-controller=\"remote\">Created over HTTP</p>"
            "</template></turbo-stream>");
        http_response_send(response);
    } else if (!strcmp(request->path, "/audit")) {
        http_response_set_header(response, "Content-Type", "text/plain");
        uv_mutex_lock(&state->mutex);
        http_response_write(response, state->requests->str, state->requests->length);
        uv_mutex_unlock(&state->mutex);
        http_response_send(response);
    } else {
        http_response_error(response, 404, "Unknown Hotwire test endpoint");
    }
}

static int hotwire_server_main(const char* ready_path) {
    ServerConfig config = server_config_default();
    config.bind_address = serve_strdup("127.0.0.1");
    Server* server = server_create(&config);
    if (!server) { server_config_cleanup(&config); return 1; }
    HotwireServerState state = {};
    uv_mutex_init(&state.mutex);
    state.requests = strbuf_new();
    int status = state.requests ? server_all(server, "/*", hotwire_route, &state) : -1;
    if (!status) status = server_start(server, 0);
    if (!status) {
        char ready[32];
        snprintf(ready, sizeof(ready), "%d\n", server->config.port);
        status = write_text_file_atomic(ready_path, ready);
    }
    if (!status) status = server_run(server);
    server_destroy(server);
    strbuf_free(state.requests);
    uv_mutex_destroy(&state.mutex);
    return status ? 1 : 0;
}

class HotwireHttpTest : public testing::TestWithParam<const char*> {
protected:
    ShellProcess* server = NULL;
    char directory[256] = {};
    char document_url[128] = {};

    void SetUp() override {
        snprintf(directory, sizeof(directory), "temp/hotwire/server-%ld-%llu",
            (long)uv_os_getpid(), (unsigned long long)uv_hrtime());
        ASSERT_TRUE(create_dir(directory));
        char ready_path[320];
        snprintf(ready_path, sizeof(ready_path), "%s/ready", directory);
        const char* arguments[] = {"./test/test_serve_gtest.exe", "--hotwire-server", ready_path, NULL};
        server = shell_spawn(arguments[0], arguments, NULL);
        ASSERT_NE(server, nullptr);
        uint64_t deadline = uv_hrtime() + 5000000000ULL;
        while (uv_hrtime() < deadline && !shell_process_poll(server)) {
            char* ready = file_stat(ready_path).exists ? read_text_file(ready_path) : NULL;
            if (ready) {
                int port = atoi(ready);
                free(ready);
                ASSERT_GT(port, 0);
                snprintf(document_url, sizeof(document_url), "http://127.0.0.1:%d/", port);
                return;
            }
            uv_sleep(10);
        }
        FAIL() << "Lambda's Hotwire fixture server did not become ready";
    }

    void TearDown() override {
        if (server) {
            shell_process_kill(server);
            ShellResult result = shell_process_wait(server, 5000);
            EXPECT_FALSE(result.timed_out);
            EXPECT_EQ(result.exit_code, 0) << (result.stderr_buf ? result.stderr_buf : "");
            shell_result_free(&result);
            shell_process_free(server);
        }
        if (directory[0]) file_delete_recursive(directory);
    }
};

TEST_P(HotwireHttpTest, ClientAndServer) {
    const char* arguments[] = {"./lambda.exe", "js", "test/js/hotwire/http.bundle.js",
        "--document", document_url, "--no-log", NULL};
    ShellEnvEntry environment[] = {{"JS_EXEC_BACKEND", GetParam()}, {NULL, NULL}};
    ShellOptions options = {};
    options.env = environment;
    options.timeout_ms = 30000;
    ShellResult result = shell_exec(arguments[0], arguments, &options);
    char* golden = read_text_file("test/js/hotwire/http.txt");
    ASSERT_NE(golden, nullptr);
    EXPECT_FALSE(result.timed_out);
    EXPECT_EQ(result.exit_code, 0) << (result.stderr_buf ? result.stderr_buf : "");
    EXPECT_STREQ(result.stdout_buf, golden) << (result.stderr_buf ? result.stderr_buf : "");
    free(golden);
    shell_result_free(&result);
}

INSTANTIATE_TEST_SUITE_P(Tiers, HotwireHttpTest, testing::Values("ast", "mir", "auto"));

TEST(HotwireServerTest, EphemeralListenersAreDistinct) {
    ServerConfig config = server_config_default();
    config.bind_address = serve_strdup("127.0.0.1");
    Server* first = server_create(&config);
    ASSERT_NE(first, nullptr);
    config.bind_address = serve_strdup("127.0.0.1");
    Server* second = server_create(&config);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(server_start(first, 0), 0);
    EXPECT_EQ(server_start(second, 0), 0);
    EXPECT_GT(first->config.port, 0);
    EXPECT_GT(second->config.port, 0);
    EXPECT_NE(first->config.port, second->config.port);
    server_destroy(second);
    server_destroy(first);
}

int main(int argc, char** argv) {
    if (argc == 3 && !strcmp(argv[1], "--hotwire-server")) {
        return hotwire_server_main(argv[2]);
    }
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
