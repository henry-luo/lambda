/**
 * @file test_serve_gtest.cpp
 * @brief GTest unit tests for lambda/serve/ web server module
 *
 * Tests: serve_types, serve_utils, router, cookie, body_parser,
 *        mime, middleware, language_backend, static_handler, http_request
 */

#include <gtest/gtest.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "../lambda/serve/serve_types.hpp"
#include "../lambda/serve/serve_utils.hpp"
#include "../lambda/serve/router.hpp"
#include "../lambda/serve/cookie.hpp"
#include "../lambda/serve/body_parser.hpp"
#include "../lambda/serve/mime.hpp"
#include "../lambda/serve/middleware.hpp"
#include "../lambda/serve/language_backend.hpp"
#include "../lambda/serve/static_handler.hpp"
#include "../lambda/serve/http_request.hpp"
#include "../lambda/serve/http_response.hpp"

// ============================================================================
// serve_types: HttpHeader linked list
// ============================================================================

class ServeTypesTest : public ::testing::Test {};

TEST_F(ServeTypesTest, HeaderAddAndFind) {
    HttpHeader *list = NULL;
    list = http_header_add(list, "Content-Type", "text/html");
    list = http_header_add(list, "X-Custom", "hello");

    EXPECT_STREQ(http_header_find(list, "Content-Type"), "text/html");
    EXPECT_STREQ(http_header_find(list, "X-Custom"), "hello");
    EXPECT_EQ(http_header_find(list, "Missing"), nullptr);

    http_header_free(list);
}

TEST_F(ServeTypesTest, HeaderFindCaseInsensitive) {
    HttpHeader *list = NULL;
    list = http_header_add(list, "Content-Type", "application/json");

    EXPECT_STREQ(http_header_find(list, "content-type"), "application/json");
    EXPECT_STREQ(http_header_find(list, "CONTENT-TYPE"), "application/json");

    http_header_free(list);
}

TEST_F(ServeTypesTest, HeaderRemove) {
    HttpHeader *list = NULL;
    list = http_header_add(list, "A", "1");
    list = http_header_add(list, "B", "2");
    list = http_header_add(list, "C", "3");

    list = http_header_remove(list, "B");
    EXPECT_EQ(http_header_find(list, "B"), nullptr);
    EXPECT_STREQ(http_header_find(list, "A"), "1");
    EXPECT_STREQ(http_header_find(list, "C"), "3");

    http_header_free(list);
}

TEST_F(ServeTypesTest, HeaderRemoveHead) {
    HttpHeader *list = NULL;
    list = http_header_add(list, "Only", "value");
    list = http_header_remove(list, "Only");
    EXPECT_EQ(list, nullptr);
    EXPECT_EQ(http_header_find(list, "Only"), nullptr);
}

TEST_F(ServeTypesTest, HeaderRemoveNonExistent) {
    HttpHeader *list = NULL;
    list = http_header_add(list, "A", "1");
    list = http_header_remove(list, "Z"); // no-op
    EXPECT_STREQ(http_header_find(list, "A"), "1");
    http_header_free(list);
}

TEST_F(ServeTypesTest, HeaderFreeNull) {
    http_header_free(NULL); // should not crash
}

// ============================================================================
// serve_types: HttpMethod conversion
// ============================================================================

TEST_F(ServeTypesTest, MethodFromString) {
    EXPECT_EQ(http_method_from_string("GET"),     HTTP_GET);
    EXPECT_EQ(http_method_from_string("POST"),    HTTP_POST);
    EXPECT_EQ(http_method_from_string("PUT"),     HTTP_PUT);
    EXPECT_EQ(http_method_from_string("DELETE"),  HTTP_DELETE);
    EXPECT_EQ(http_method_from_string("HEAD"),    HTTP_HEAD);
    EXPECT_EQ(http_method_from_string("OPTIONS"), HTTP_OPTIONS);
    EXPECT_EQ(http_method_from_string("PATCH"),   HTTP_PATCH);
    EXPECT_EQ(http_method_from_string("CONNECT"), HTTP_CONNECT);
    EXPECT_EQ(http_method_from_string("TRACE"),   HTTP_TRACE);
    EXPECT_EQ(http_method_from_string("INVALID"), HTTP_UNKNOWN);
    EXPECT_EQ(http_method_from_string(NULL),      HTTP_UNKNOWN);
}

TEST_F(ServeTypesTest, MethodToString) {
    EXPECT_STREQ(http_method_to_string(HTTP_GET),     "GET");
    EXPECT_STREQ(http_method_to_string(HTTP_POST),    "POST");
    EXPECT_STREQ(http_method_to_string(HTTP_DELETE),  "DELETE");
    EXPECT_STREQ(http_method_to_string(HTTP_PATCH),   "PATCH");
    EXPECT_STREQ(http_method_to_string(HTTP_UNKNOWN), "UNKNOWN");
}

TEST_F(ServeTypesTest, MethodBitmask) {
    int all_methods = HTTP_GET | HTTP_POST | HTTP_PUT | HTTP_DELETE |
                      HTTP_HEAD | HTTP_OPTIONS | HTTP_PATCH |
                      HTTP_CONNECT | HTTP_TRACE;
    EXPECT_EQ(all_methods, HTTP_ALL);
}

// ============================================================================
// serve_types: HttpStatus string
// ============================================================================

TEST_F(ServeTypesTest, StatusString) {
    EXPECT_STREQ(http_status_string(200), "OK");
    EXPECT_STREQ(http_status_string(201), "Created");
    EXPECT_STREQ(http_status_string(204), "No Content");
    EXPECT_STREQ(http_status_string(301), "Moved Permanently");
    EXPECT_STREQ(http_status_string(302), "Found");
    EXPECT_STREQ(http_status_string(304), "Not Modified");
    EXPECT_STREQ(http_status_string(400), "Bad Request");
    EXPECT_STREQ(http_status_string(401), "Unauthorized");
    EXPECT_STREQ(http_status_string(403), "Forbidden");
    EXPECT_STREQ(http_status_string(404), "Not Found");
    EXPECT_STREQ(http_status_string(500), "Internal Server Error");
    EXPECT_STREQ(http_status_string(503), "Service Unavailable");
    EXPECT_STREQ(http_status_string(999), "Unknown");
}

// ============================================================================
// serve_types: ServerConfig
// ============================================================================

TEST_F(ServeTypesTest, ConfigDefault) {
    ServerConfig cfg = server_config_default();
    EXPECT_EQ(cfg.port, 8080);
    EXPECT_EQ(cfg.ssl_port, 0);
    EXPECT_EQ(cfg.max_connections, 1024);
    EXPECT_EQ(cfg.timeout_seconds, 60);
    EXPECT_EQ(cfg.max_header_size, 8192u);
    EXPECT_EQ(cfg.max_body_size, 10u * 1024 * 1024);
    EXPECT_EQ(cfg.keep_alive, 1);
    EXPECT_EQ(cfg.keep_alive_timeout, 5);
    EXPECT_EQ(cfg.max_requests_per_conn, 100);
}

TEST_F(ServeTypesTest, ConfigValidateDefault) {
    ServerConfig cfg = server_config_default();
    EXPECT_EQ(server_config_validate(&cfg), 0);
}

TEST_F(ServeTypesTest, ConfigValidateNull) {
    EXPECT_EQ(server_config_validate(NULL), -1);
}

TEST_F(ServeTypesTest, ConfigValidateNoPort) {
    ServerConfig cfg = server_config_default();
    cfg.port = 0;
    cfg.ssl_port = 0;
    EXPECT_EQ(server_config_validate(&cfg), -1);
}

TEST_F(ServeTypesTest, ConfigValidateInvalidPort) {
    ServerConfig cfg = server_config_default();
    cfg.port = 99999;
    EXPECT_EQ(server_config_validate(&cfg), -1);
}

TEST_F(ServeTypesTest, ConfigValidateSslNoCert) {
    ServerConfig cfg = server_config_default();
    cfg.ssl_port = 443;
    cfg.ssl_cert_file = NULL;
    cfg.ssl_key_file = NULL;
    EXPECT_EQ(server_config_validate(&cfg), -1);
}

TEST_F(ServeTypesTest, ConfigCleanup) {
    ServerConfig cfg = server_config_default();
    cfg.bind_address = serve_strdup("0.0.0.0");
    cfg.document_root = serve_strdup("/var/www");
    server_config_cleanup(&cfg);
    // after cleanup, pointers are zeroed
    EXPECT_EQ(cfg.port, 0);
    EXPECT_EQ(cfg.bind_address, nullptr);
    EXPECT_EQ(cfg.document_root, nullptr);
}

TEST_F(ServeTypesTest, ConfigCleanupNull) {
    server_config_cleanup(NULL); // should not crash
}

// ============================================================================
// serve_utils: string utilities
// ============================================================================

class ServeUtilsTest : public ::testing::Test {};

TEST_F(ServeUtilsTest, Strcasecmp) {
    EXPECT_EQ(serve_strcasecmp("hello", "HELLO"), 0);
    EXPECT_EQ(serve_strcasecmp("abc", "abc"), 0);
    EXPECT_NE(serve_strcasecmp("abc", "def"), 0);
    EXPECT_NE(serve_strcasecmp(NULL, "a"), 0);
    EXPECT_NE(serve_strcasecmp("a", NULL), 0);
}

TEST_F(ServeUtilsTest, Strtrim) {
    char buf1[] = "  hello  ";
    EXPECT_STREQ(serve_strtrim(buf1), "hello");

    char buf2[] = "no_whitespace";
    EXPECT_STREQ(serve_strtrim(buf2), "no_whitespace");

    char buf3[] = "   ";
    EXPECT_STREQ(serve_strtrim(buf3), "");

    EXPECT_EQ(serve_strtrim(NULL), nullptr);
}

TEST_F(ServeUtilsTest, UrlDecode) {
    char buf1[] = "hello%20world";
    serve_url_decode(buf1);
    EXPECT_STREQ(buf1, "hello world");

    char buf2[] = "a%2Fb%3Dc";
    serve_url_decode(buf2);
    EXPECT_STREQ(buf2, "a/b=c");

    char buf3[] = "hello+world";
    serve_url_decode(buf3);
    EXPECT_STREQ(buf3, "hello world");

    char buf4[] = "no_encoding";
    serve_url_decode(buf4);
    EXPECT_STREQ(buf4, "no_encoding");

    EXPECT_EQ(serve_url_decode(NULL), 0u);
}

TEST_F(ServeUtilsTest, UrlDecodePartialPercent) {
    char buf[] = "hello%2";
    serve_url_decode(buf);
    EXPECT_STREQ(buf, "hello%2"); // incomplete % sequence left as-is
}

TEST_F(ServeUtilsTest, GetFileExtension) {
    EXPECT_STREQ(serve_get_file_extension("test.html"), ".html");
    EXPECT_STREQ(serve_get_file_extension("file.tar.gz"), ".gz");
    EXPECT_STREQ(serve_get_file_extension("no_extension"), "");
    EXPECT_STREQ(serve_get_file_extension("/path/to/file.js"), ".js");
    EXPECT_STREQ(serve_get_file_extension("/path.dir/file"), "");
    EXPECT_STREQ(serve_get_file_extension(NULL), "");
}

// ============================================================================
// serve_utils: error handling
// ============================================================================

TEST_F(ServeUtilsTest, ErrorBuffer) {
    serve_clear_error();
    EXPECT_STREQ(serve_get_error(), "");

    serve_set_error("test error %d", 42);
    EXPECT_STREQ(serve_get_error(), "test error 42");

    serve_clear_error();
    EXPECT_STREQ(serve_get_error(), "");
}

// ============================================================================
// serve_utils: memory management
// ============================================================================

TEST_F(ServeUtilsTest, MallocAndFree) {
    void *p = serve_malloc(64);
    ASSERT_NE(p, nullptr);
    serve_free(p);
}

TEST_F(ServeUtilsTest, CallocZeros) {
    int *p = (int *)serve_calloc(4, sizeof(int));
    ASSERT_NE(p, nullptr);
    for (int i = 0; i < 4; i++) {
        EXPECT_EQ(p[i], 0);
    }
    serve_free(p);
}

TEST_F(ServeUtilsTest, Strdup) {
    char *s = serve_strdup("hello");
    ASSERT_NE(s, nullptr);
    EXPECT_STREQ(s, "hello");
    serve_free(s);

    EXPECT_EQ(serve_strdup(NULL), nullptr);
}

TEST_F(ServeUtilsTest, ReallocGrows) {
    char *p = (char *)serve_malloc(8);
    ASSERT_NE(p, nullptr);
    memcpy(p, "hello", 6);

    p = (char *)serve_realloc(p, 64);
    ASSERT_NE(p, nullptr);
    EXPECT_STREQ(p, "hello");
    serve_free(p);
}

TEST_F(ServeUtilsTest, FreeNull) {
    serve_free(NULL); // should not crash
}

// ============================================================================
// serve_utils: file utilities
// ============================================================================

TEST_F(ServeUtilsTest, FileExists) {
    EXPECT_EQ(serve_file_exists("Makefile"), 1);
    EXPECT_EQ(serve_file_exists("nonexistent_file_xyz"), 0);
    EXPECT_EQ(serve_file_exists(NULL), 0);
}

TEST_F(ServeUtilsTest, FileSize) {
    EXPECT_GT(serve_file_size("Makefile"), 0);
    EXPECT_EQ(serve_file_size("nonexistent_file_xyz"), -1);
    EXPECT_EQ(serve_file_size(NULL), -1);
}

TEST_F(ServeUtilsTest, ReadFile) {
    size_t size = 0;
    char *data = serve_read_file("Makefile", &size);
    ASSERT_NE(data, nullptr);
    EXPECT_GT(size, 0u);
    serve_free(data);
}

TEST_F(ServeUtilsTest, ReadFileNonExistent) {
    size_t size = 0;
    char *data = serve_read_file("nonexistent_file_xyz", &size);
    EXPECT_EQ(data, nullptr);
}

TEST_F(ServeUtilsTest, ReadFileNull) {
    EXPECT_EQ(serve_read_file(NULL, NULL), nullptr);
}

// ============================================================================
// serve_utils: HTTP date format
// ============================================================================

TEST_F(ServeUtilsTest, HttpDate) {
    char buf[64];
    serve_http_date(buf, sizeof(buf));
    // should contain "GMT"
    EXPECT_NE(strstr(buf, "GMT"), nullptr);
    // should have reasonable length
    EXPECT_GT(strlen(buf), 10u);
}

// ============================================================================
// Router: creation and destruction
// ============================================================================

class RouterTest : public ::testing::Test {
protected:
    Router *router = nullptr;

    void SetUp() override {
        router = router_create("");
    }

    void TearDown() override {
        if (router) router_destroy(router);
    }

    static void dummy_handler(HttpRequest *req, HttpResponse *resp, void *user_data) {
        (void)req; (void)resp;
        if (user_data) {
            (*(int *)user_data)++;
        }
    }

    static void other_handler(HttpRequest *req, HttpResponse *resp, void *user_data) {
        (void)req; (void)resp; (void)user_data;
    }
};

TEST_F(RouterTest, CreateDestroy) {
    ASSERT_NE(router, nullptr);
}

TEST_F(RouterTest, CreateWithPrefix) {
    Router *r = router_create("/api");
    ASSERT_NE(r, nullptr);
    router_destroy(r);
}

TEST_F(RouterTest, ExactMatch) {
    EXPECT_EQ(router_get(router, "/hello", dummy_handler, NULL), 0);

    HttpHeader *params = NULL;
    void *data = NULL;
    RequestHandler h = router_match(router, HTTP_GET, "/hello", &params, &data);
    EXPECT_EQ(h, dummy_handler);
    http_header_free(params);
}

TEST_F(RouterTest, NoMatch) {
    router_get(router, "/hello", dummy_handler, NULL);

    HttpHeader *params = NULL;
    void *data = NULL;
    RequestHandler h = router_match(router, HTTP_GET, "/world", &params, &data);
    EXPECT_EQ(h, nullptr);
    http_header_free(params);
}

TEST_F(RouterTest, WrongMethod) {
    router_get(router, "/hello", dummy_handler, NULL);

    HttpHeader *params = NULL;
    void *data = NULL;
    RequestHandler h = router_match(router, HTTP_POST, "/hello", &params, &data);
    EXPECT_EQ(h, nullptr);
    http_header_free(params);
}

TEST_F(RouterTest, NamedParameter) {
    router_get(router, "/users/:id", dummy_handler, NULL);

    HttpHeader *params = NULL;
    void *data = NULL;
    RequestHandler h = router_match(router, HTTP_GET, "/users/42", &params, &data);
    EXPECT_EQ(h, dummy_handler);
    EXPECT_STREQ(http_header_find(params, "id"), "42");
    http_header_free(params);
}

TEST_F(RouterTest, MultipleParameters) {
    router_get(router, "/users/:uid/posts/:pid", dummy_handler, NULL);

    HttpHeader *params = NULL;
    void *data = NULL;
    RequestHandler h = router_match(router, HTTP_GET, "/users/5/posts/99", &params, &data);
    EXPECT_EQ(h, dummy_handler);
    EXPECT_STREQ(http_header_find(params, "uid"), "5");
    EXPECT_STREQ(http_header_find(params, "pid"), "99");
    http_header_free(params);
}

TEST_F(RouterTest, WildcardCatchAll) {
    router_get(router, "/files/*path", dummy_handler, NULL);

    HttpHeader *params = NULL;
    void *data = NULL;
    RequestHandler h = router_match(router, HTTP_GET, "/files/docs/readme.md", &params, &data);
    EXPECT_EQ(h, dummy_handler);
    EXPECT_STREQ(http_header_find(params, "path"), "docs/readme.md");
    http_header_free(params);
}

TEST_F(RouterTest, MethodSpecific) {
    router_get(router, "/api", dummy_handler, NULL);
    router_post(router, "/api", other_handler, NULL);

    HttpHeader *params = NULL;
    void *data = NULL;

    RequestHandler h = router_match(router, HTTP_GET, "/api", &params, &data);
    EXPECT_EQ(h, dummy_handler);
    http_header_free(params);

    params = NULL;
    h = router_match(router, HTTP_POST, "/api", &params, &data);
    EXPECT_EQ(h, other_handler);
    http_header_free(params);
}

TEST_F(RouterTest, AllMethods) {
    router_all(router, "/any", dummy_handler, NULL);

    HttpHeader *params = NULL;
    void *data = NULL;

    EXPECT_NE(router_match(router, HTTP_GET, "/any", &params, &data), nullptr);
    http_header_free(params); params = NULL;

    EXPECT_NE(router_match(router, HTTP_POST, "/any", &params, &data), nullptr);
    http_header_free(params); params = NULL;

    EXPECT_NE(router_match(router, HTTP_DELETE, "/any", &params, &data), nullptr);
    http_header_free(params); params = NULL;

    EXPECT_NE(router_match(router, HTTP_PATCH, "/any", &params, &data), nullptr);
    http_header_free(params);
}

TEST_F(RouterTest, UserData) {
    int counter = 0;
    router_get(router, "/test", dummy_handler, &counter);

    HttpHeader *params = NULL;
    void *data = NULL;
    RequestHandler h = router_match(router, HTTP_GET, "/test", &params, &data);
    EXPECT_EQ(h, dummy_handler);
    EXPECT_EQ(data, &counter);
    http_header_free(params);
}

TEST_F(RouterTest, NestedPaths) {
    router_get(router, "/a/b/c", dummy_handler, NULL);

    HttpHeader *params = NULL;
    void *data = NULL;
    EXPECT_NE(router_match(router, HTTP_GET, "/a/b/c", &params, &data), nullptr);
    http_header_free(params); params = NULL;

    EXPECT_EQ(router_match(router, HTTP_GET, "/a/b", &params, &data), nullptr);
    http_header_free(params); params = NULL;

    EXPECT_EQ(router_match(router, HTTP_GET, "/a", &params, &data), nullptr);
    http_header_free(params);
}

TEST_F(RouterTest, PrefixedRouter) {
    Router *api = router_create("/api");
    ASSERT_NE(api, nullptr);
    router_get(api, "/users", dummy_handler, NULL);

    HttpHeader *params = NULL;
    void *data = NULL;
    RequestHandler h = router_match(api, HTTP_GET, "/api/users", &params, &data);
    EXPECT_EQ(h, dummy_handler);
    http_header_free(params);

    router_destroy(api);
}

TEST_F(RouterTest, MountSubRouter) {
    Router *sub = router_create("/v1");
    ASSERT_NE(sub, nullptr);
    router_get(sub, "/status", dummy_handler, NULL);

    Router *parent = router_create("/api");
    ASSERT_NE(parent, nullptr);
    EXPECT_EQ(router_mount(parent, sub), 0);

    // after mount, sub's prefix becomes "/api/v1"
    HttpHeader *params = NULL;
    void *data = NULL;
    RequestHandler h = router_match(sub, HTTP_GET, "/api/v1/status", &params, &data);
    EXPECT_EQ(h, dummy_handler);
    http_header_free(params);

    router_destroy(sub);
    router_destroy(parent);
}

TEST_F(RouterTest, NullInputs) {
    EXPECT_EQ(router_add(NULL, HTTP_GET, "/x", dummy_handler, NULL), -1);
    EXPECT_EQ(router_add(router, HTTP_GET, NULL, dummy_handler, NULL), -1);
    EXPECT_EQ(router_add(router, HTTP_GET, "/x", NULL, NULL), -1);
    EXPECT_EQ(router_match(NULL, HTTP_GET, "/x", NULL, NULL), nullptr);
    EXPECT_EQ(router_match(router, HTTP_GET, NULL, NULL, NULL), nullptr);
}

TEST_F(RouterTest, DestroyNull) {
    router_destroy(NULL); // should not crash
}

// ============================================================================
// Cookie: parsing
// ============================================================================

class CookieTest : public ::testing::Test {};

TEST_F(CookieTest, ParseSingle) {
    HttpHeader *cookies = cookie_parse("session=abc123");
    ASSERT_NE(cookies, nullptr);
    EXPECT_STREQ(http_header_find(cookies, "session"), "abc123");
    http_header_free(cookies);
}

TEST_F(CookieTest, ParseMultiple) {
    HttpHeader *cookies = cookie_parse("a=1; b=2; c=3");
    ASSERT_NE(cookies, nullptr);
    EXPECT_STREQ(http_header_find(cookies, "a"), "1");
    EXPECT_STREQ(http_header_find(cookies, "b"), "2");
    EXPECT_STREQ(http_header_find(cookies, "c"), "3");
    http_header_free(cookies);
}

TEST_F(CookieTest, ParseWithSpaces) {
    HttpHeader *cookies = cookie_parse("  name = value ; key = val2  ");
    ASSERT_NE(cookies, nullptr);
    // the parser trims whitespace from names and values
    EXPECT_NE(http_header_find(cookies, "name"), nullptr);
    http_header_free(cookies);
}

TEST_F(CookieTest, ParseNull) {
    EXPECT_EQ(cookie_parse(NULL), nullptr);
}

TEST_F(CookieTest, ParseEmpty) {
    HttpHeader *cookies = cookie_parse("");
    EXPECT_EQ(cookies, nullptr);
}

// ============================================================================
// Cookie: Set-Cookie generation
// ============================================================================

TEST_F(CookieTest, BuildSetCookieBasic) {
    char *result = cookie_build_set_cookie("token", "xyz", NULL);
    ASSERT_NE(result, nullptr);
    EXPECT_NE(strstr(result, "token=xyz"), nullptr);
    EXPECT_NE(strstr(result, "Path=/"), nullptr);
    EXPECT_NE(strstr(result, "HttpOnly"), nullptr);
    serve_free(result);
}

TEST_F(CookieTest, BuildSetCookieWithOptions) {
    CookieOptions opts = cookie_options_default();
    opts.max_age = 3600;
    opts.domain = ".example.com";
    opts.path = "/api";
    opts.secure = 1;
    opts.http_only = 1;
    opts.same_site = "Strict";

    char *result = cookie_build_set_cookie("session", "abc", &opts);
    ASSERT_NE(result, nullptr);
    EXPECT_NE(strstr(result, "session=abc"), nullptr);
    EXPECT_NE(strstr(result, "Max-Age=3600"), nullptr);
    EXPECT_NE(strstr(result, "Domain=.example.com"), nullptr);
    EXPECT_NE(strstr(result, "Path=/api"), nullptr);
    EXPECT_NE(strstr(result, "Secure"), nullptr);
    EXPECT_NE(strstr(result, "HttpOnly"), nullptr);
    EXPECT_NE(strstr(result, "SameSite=Strict"), nullptr);
    serve_free(result);
}

TEST_F(CookieTest, BuildSetCookieNullName) {
    EXPECT_EQ(cookie_build_set_cookie(NULL, "v", NULL), nullptr);
}

TEST_F(CookieTest, BuildClearCookie) {
    char *result = cookie_build_clear("token", "/", ".example.com");
    ASSERT_NE(result, nullptr);
    EXPECT_NE(strstr(result, "token="), nullptr);
    EXPECT_NE(strstr(result, "Max-Age=0"), nullptr);
    serve_free(result);
}

TEST_F(CookieTest, CookieOptionsDefault) {
    CookieOptions opts = cookie_options_default();
    EXPECT_EQ(opts.max_age, -1);
    EXPECT_STREQ(opts.path, "/");
    EXPECT_EQ(opts.http_only, 1);
    EXPECT_STREQ(opts.same_site, "Lax");
}

// ============================================================================
// Body Parser: JSON
// ============================================================================

class BodyParserTest : public ::testing::Test {};

TEST_F(BodyParserTest, ParseJsonValid) {
    const char *json = "{\"name\": \"test\", \"value\": 42}";
    ParsedJson *pj = body_parse_json(json, strlen(json));
    ASSERT_NE(pj, nullptr);
    EXPECT_STREQ(pj->json_str, json);
    EXPECT_EQ(pj->len, strlen(json));
    body_free_parsed(BODY_JSON, pj);
}

TEST_F(BodyParserTest, ParseJsonArray) {
    const char *json = "[1, 2, 3]";
    ParsedJson *pj = body_parse_json(json, strlen(json));
    ASSERT_NE(pj, nullptr);
    EXPECT_STREQ(pj->json_str, json);
    body_free_parsed(BODY_JSON, pj);
}

TEST_F(BodyParserTest, ParseJsonInvalid) {
    const char *bad = "not json at all";
    ParsedJson *pj = body_parse_json(bad, strlen(bad));
    EXPECT_EQ(pj, nullptr);
}

TEST_F(BodyParserTest, ParseJsonNull) {
    EXPECT_EQ(body_parse_json(NULL, 0), nullptr);
}

TEST_F(BodyParserTest, ParseJsonEmpty) {
    EXPECT_EQ(body_parse_json("", 0), nullptr);
}

TEST_F(BodyParserTest, ParseJsonUnbalanced) {
    const char *unbalanced = "{\"key\": [1, 2";
    ParsedJson *pj = body_parse_json(unbalanced, strlen(unbalanced));
    EXPECT_EQ(pj, nullptr);
}

// ============================================================================
// Body Parser: URL-encoded form
// ============================================================================

TEST_F(BodyParserTest, ParseFormBasic) {
    const char *form = "name=john&age=30";
    HttpHeader *fields = body_parse_form(form, strlen(form));
    ASSERT_NE(fields, nullptr);
    EXPECT_STREQ(http_header_find(fields, "name"), "john");
    EXPECT_STREQ(http_header_find(fields, "age"), "30");
    http_header_free(fields);
}

TEST_F(BodyParserTest, ParseFormEncoded) {
    const char *form = "msg=hello+world&path=%2Fhome";
    HttpHeader *fields = body_parse_form(form, strlen(form));
    ASSERT_NE(fields, nullptr);
    EXPECT_STREQ(http_header_find(fields, "msg"), "hello world");
    EXPECT_STREQ(http_header_find(fields, "path"), "/home");
    http_header_free(fields);
}

TEST_F(BodyParserTest, ParseFormNull) {
    EXPECT_EQ(body_parse_form(NULL, 0), nullptr);
}

TEST_F(BodyParserTest, ParseFormEmpty) {
    EXPECT_EQ(body_parse_form("", 0), nullptr);
}

// ============================================================================
// Body Parser: auto-dispatch via body_parse
// ============================================================================

TEST_F(BodyParserTest, BodyParseJsonDispatch) {
    const char *raw = "GET / HTTP/1.1\r\n"
                      "Content-Type: application/json\r\n"
                      "\r\n"
                      "{\"key\": \"val\"}";
    HttpRequest *req = http_request_parse(raw, strlen(raw));
    ASSERT_NE(req, nullptr);

    int rc = body_parse(req);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(req->parsed_body_type, BODY_JSON);
    ASSERT_NE(req->parsed_body, nullptr);

    ParsedJson *pj = (ParsedJson *)req->parsed_body;
    EXPECT_NE(strstr(pj->json_str, "key"), nullptr);

    http_request_destroy(req);
}

TEST_F(BodyParserTest, BodyParseFormDispatch) {
    const char *raw = "POST /submit HTTP/1.1\r\n"
                      "Content-Type: application/x-www-form-urlencoded\r\n"
                      "\r\n"
                      "user=alice&pass=secret";
    HttpRequest *req = http_request_parse(raw, strlen(raw));
    ASSERT_NE(req, nullptr);

    int rc = body_parse(req);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(req->parsed_body_type, BODY_FORM);

    http_request_destroy(req);
}

TEST_F(BodyParserTest, BodyParseNoBody) {
    const char *raw = "GET / HTTP/1.1\r\n\r\n";
    HttpRequest *req = http_request_parse(raw, strlen(raw));
    ASSERT_NE(req, nullptr);

    int rc = body_parse(req);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(req->parsed_body_type, BODY_NONE);

    http_request_destroy(req);
}

// ============================================================================
// MIME Detection
// ============================================================================

class MimeTest : public ::testing::Test {
protected:
    static MimeDetector *detector;

    static void SetUpTestSuite() {
        detector = mime_detector_create();
        ASSERT_NE(detector, nullptr);
    }

    static void TearDownTestSuite() {
        if (detector) {
            mime_detector_destroy(detector);
            detector = nullptr;
        }
    }
};

MimeDetector* MimeTest::detector = nullptr;

TEST_F(MimeTest, DetectFromFilenameHtml) {
    const char *mime = mime_detect_from_filename(detector, "page.html");
    ASSERT_NE(mime, nullptr);
    EXPECT_NE(strstr(mime, "html"), nullptr);
}

TEST_F(MimeTest, DetectFromFilenameJson) {
    const char *mime = mime_detect_from_filename(detector, "data.json");
    ASSERT_NE(mime, nullptr);
    EXPECT_NE(strstr(mime, "json"), nullptr);
}

TEST_F(MimeTest, DetectFromFilenameCss) {
    const char *mime = mime_detect_from_filename(detector, "style.css");
    ASSERT_NE(mime, nullptr);
    EXPECT_NE(strstr(mime, "css"), nullptr);
}

TEST_F(MimeTest, DetectFromFilenamePdf) {
    const char *mime = mime_detect_from_filename(detector, "doc.pdf");
    ASSERT_NE(mime, nullptr);
    EXPECT_NE(strstr(mime, "pdf"), nullptr);
}

TEST_F(MimeTest, DetectFromFilenamePng) {
    const char *mime = mime_detect_from_filename(detector, "image.png");
    ASSERT_NE(mime, nullptr);
    EXPECT_NE(strstr(mime, "png"), nullptr);
}

TEST_F(MimeTest, DetectFromFilenameJs) {
    const char *mime = mime_detect_from_filename(detector, "app.js");
    ASSERT_NE(mime, nullptr);
    EXPECT_NE(strstr(mime, "javascript"), nullptr);
}

TEST_F(MimeTest, DetectFromFilenameUnknown) {
    const char *mime = mime_detect_from_filename(detector, "file_without_ext");
    // may return NULL for unknown extension — just verify no crash
    (void)mime;
}

TEST_F(MimeTest, DetectFromContentPdf) {
    const char *content = "%PDF-1.4 fake content";
    const char *mime = mime_detect_from_content(detector, content, strlen(content));
    ASSERT_NE(mime, nullptr);
    EXPECT_NE(strstr(mime, "pdf"), nullptr);
}

TEST_F(MimeTest, DetectFromContentPng) {
    const char content[] = "\x89PNG\r\n\x1a\n fake png data";
    const char *mime = mime_detect_from_content(detector, content, sizeof(content) - 1);
    ASSERT_NE(mime, nullptr);
    EXPECT_NE(strstr(mime, "png"), nullptr);
}

TEST_F(MimeTest, DetectCombined) {
    const char *json_content = "{\"hello\": \"world\"}";
    const char *mime = mime_detect(detector, "test.json", json_content, strlen(json_content));
    ASSERT_NE(mime, nullptr);
    EXPECT_NE(strstr(mime, "json"), nullptr);
}

TEST_F(MimeTest, ExtensionForType) {
    EXPECT_STREQ(mime_extension_for_type("text/html"), ".html");
    EXPECT_STREQ(mime_extension_for_type("application/json"), ".json");
    EXPECT_STREQ(mime_extension_for_type("image/png"), ".png");
    EXPECT_STREQ(mime_extension_for_type("application/pdf"), ".pdf");
}

TEST_F(MimeTest, GlobMatch) {
    EXPECT_EQ(mime_match_glob("*.html", "page.html"), 1);
    EXPECT_EQ(mime_match_glob("*.html", "page.css"), 0);
    EXPECT_EQ(mime_match_glob("*.js", "app.js"), 1);
    EXPECT_EQ(mime_match_glob("test?", "test1"), 1);
    EXPECT_EQ(mime_match_glob("test?", "test"), 0);
    EXPECT_EQ(mime_match_glob(NULL, "test"), 0);
    EXPECT_EQ(mime_match_glob("*.html", NULL), 0);
}

TEST_F(MimeTest, MagicMatch) {
    const char *pdf_magic = "%PDF-1.4";
    EXPECT_EQ(mime_match_magic("%PDF", 4, pdf_magic, strlen(pdf_magic), 0), 1);
    EXPECT_EQ(mime_match_magic("%PDF", 4, pdf_magic, strlen(pdf_magic), 1), 0);
    EXPECT_EQ(mime_match_magic(NULL, 0, pdf_magic, strlen(pdf_magic), 0), 0);
    EXPECT_EQ(mime_match_magic("%PDF", 4, NULL, 0, 0), 0);
}

// ============================================================================
// Middleware
// ============================================================================

class MiddlewareTest : public ::testing::Test {
protected:
    MiddlewareStack *stack = nullptr;

    void SetUp() override {
        stack = middleware_stack_create();
    }

    void TearDown() override {
        if (stack) middleware_stack_destroy(stack);
    }

    static void counting_middleware(HttpRequest *req, HttpResponse *resp,
                                    void *user_data, MiddlewareContext *ctx) {
        (void)req; (void)resp;
        if (user_data) (*(int *)user_data)++;
        middleware_next(ctx);
    }

    static void stopping_middleware(HttpRequest *req, HttpResponse *resp,
                                     void *user_data, MiddlewareContext *ctx) {
        (void)req; (void)resp; (void)ctx;
        if (user_data) (*(int *)user_data)++;
        // intentionally NOT calling middleware_next — stops the chain
    }
};

TEST_F(MiddlewareTest, CreateDestroy) {
    ASSERT_NE(stack, nullptr);
    EXPECT_EQ(stack->count, 0);
}

TEST_F(MiddlewareTest, Use) {
    int counter = 0;
    EXPECT_EQ(middleware_stack_use(stack, counting_middleware, &counter), 0);
    EXPECT_EQ(stack->count, 1);
}

TEST_F(MiddlewareTest, UseMultiple) {
    int c1 = 0, c2 = 0, c3 = 0;
    middleware_stack_use(stack, counting_middleware, &c1);
    middleware_stack_use(stack, counting_middleware, &c2);
    middleware_stack_use(stack, counting_middleware, &c3);
    EXPECT_EQ(stack->count, 3);

    // run the chain with a minimal request
    const char *raw = "GET / HTTP/1.1\r\n\r\n";
    HttpRequest *req = http_request_parse(raw, strlen(raw));
    ASSERT_NE(req, nullptr);

    // we can't create a real HttpResponse without uv_tcp_t,
    // so we manually build a minimal one for testing
    HttpResponse resp_obj;
    memset(&resp_obj, 0, sizeof(resp_obj));
    resp_obj.status_code = 200;

    middleware_stack_run(stack, req, &resp_obj, NULL);

    EXPECT_EQ(c1, 1);
    EXPECT_EQ(c2, 1);
    EXPECT_EQ(c3, 1);

    http_request_destroy(req);
}

TEST_F(MiddlewareTest, StopsChain) {
    int c1 = 0, c2 = 0;
    middleware_stack_use(stack, stopping_middleware, &c1);
    middleware_stack_use(stack, counting_middleware, &c2);

    const char *raw = "GET / HTTP/1.1\r\n\r\n";
    HttpRequest *req = http_request_parse(raw, strlen(raw));
    ASSERT_NE(req, nullptr);

    HttpResponse resp_obj;
    memset(&resp_obj, 0, sizeof(resp_obj));

    middleware_stack_run(stack, req, &resp_obj, NULL);

    EXPECT_EQ(c1, 1); // first middleware ran
    EXPECT_EQ(c2, 0); // second middleware did NOT run

    http_request_destroy(req);
}

TEST_F(MiddlewareTest, UsePath) {
    int c1 = 0;
    middleware_stack_use_path(stack, "/api", counting_middleware, &c1);

    // request to /api should match
    const char *raw1 = "GET /api/users HTTP/1.1\r\n\r\n";
    HttpRequest *req1 = http_request_parse(raw1, strlen(raw1));
    ASSERT_NE(req1, nullptr);

    HttpResponse resp1;
    memset(&resp1, 0, sizeof(resp1));
    middleware_stack_run(stack, req1, &resp1, NULL);
    EXPECT_EQ(c1, 1);
    http_request_destroy(req1);

    // request to /other should NOT match
    c1 = 0;
    const char *raw2 = "GET /other HTTP/1.1\r\n\r\n";
    HttpRequest *req2 = http_request_parse(raw2, strlen(raw2));
    ASSERT_NE(req2, nullptr);

    HttpResponse resp2;
    memset(&resp2, 0, sizeof(resp2));
    middleware_stack_run(stack, req2, &resp2, NULL);
    EXPECT_EQ(c1, 0);
    http_request_destroy(req2);
}

TEST_F(MiddlewareTest, NullInputs) {
    EXPECT_EQ(middleware_stack_use(NULL, counting_middleware, NULL), -1);
    EXPECT_EQ(middleware_stack_use(stack, NULL, NULL), -1);
    EXPECT_EQ(middleware_stack_use_path(stack, NULL, counting_middleware, NULL), -1);
    middleware_stack_destroy(NULL); // should not crash
}

TEST_F(MiddlewareTest, CorsOptionsDefault) {
    CorsOptions opts = cors_options_default();
    EXPECT_STREQ(opts.origin, "*");
    EXPECT_NE(strstr(opts.methods, "GET"), nullptr);
    EXPECT_NE(strstr(opts.methods, "POST"), nullptr);
    EXPECT_NE(strstr(opts.headers, "Content-Type"), nullptr);
    EXPECT_EQ(opts.max_age, 86400);
    EXPECT_EQ(opts.credentials, 0);
}

TEST_F(MiddlewareTest, BuiltinFactoriesReturn) {
    EXPECT_NE(middleware_logger(), nullptr);
    EXPECT_NE(middleware_cors(), nullptr);
    EXPECT_NE(middleware_error_handler(), nullptr);
    EXPECT_NE(middleware_static(), nullptr);
}

// ============================================================================
// Language Backend Registry
// ============================================================================

class BackendRegistryTest : public ::testing::Test {
protected:
    BackendRegistry *registry = nullptr;

    void SetUp() override {
        registry = backend_registry_create();
    }

    void TearDown() override {
        if (registry) backend_registry_destroy(registry);
    }
};

TEST_F(BackendRegistryTest, CreateDestroy) {
    ASSERT_NE(registry, nullptr);
    EXPECT_EQ(registry->count, 0);
}

TEST_F(BackendRegistryTest, AddAndFind) {
    static const char *lambda_exts[] = {".ls"};
    LanguageBackend backend;
    memset(&backend, 0, sizeof(backend));
    backend.name = "lambda";
    backend.extensions = lambda_exts;
    backend.extension_count = 1;

    EXPECT_EQ(backend_registry_add(registry, &backend), 0);
    EXPECT_EQ(registry->count, 1);

    LanguageBackend *found = backend_registry_find(registry, "lambda");
    EXPECT_NE(found, nullptr);
    EXPECT_STREQ(found->name, "lambda");
}

TEST_F(BackendRegistryTest, FindByExtension) {
    static const char *py_exts[] = {".py", ".pyw"};
    LanguageBackend backend;
    memset(&backend, 0, sizeof(backend));
    backend.name = "python";
    backend.extensions = py_exts;
    backend.extension_count = 2;

    backend_registry_add(registry, &backend);

    LanguageBackend *found = backend_registry_find_by_ext(registry, ".py");
    EXPECT_NE(found, nullptr);
    EXPECT_STREQ(found->name, "python");

    found = backend_registry_find_by_ext(registry, ".pyw");
    EXPECT_NE(found, nullptr);
    EXPECT_STREQ(found->name, "python");

    found = backend_registry_find_by_ext(registry, ".js");
    EXPECT_EQ(found, nullptr);
}

TEST_F(BackendRegistryTest, FindNotFound) {
    EXPECT_EQ(backend_registry_find(registry, "nonexistent"), nullptr);
}

TEST_F(BackendRegistryTest, NullInputs) {
    EXPECT_EQ(backend_registry_add(NULL, NULL), -1);
    EXPECT_EQ(backend_registry_find(NULL, "x"), nullptr);
    EXPECT_EQ(backend_registry_find_by_ext(NULL, ".x"), nullptr);
    backend_registry_destroy(NULL); // should not crash
}

// ============================================================================
// Static Handler
// ============================================================================

class StaticHandlerTest : public ::testing::Test {};

TEST_F(StaticHandlerTest, ConfigDefault) {
    StaticHandlerConfig cfg = static_handler_config_default();
    EXPECT_EQ(cfg.root_dir, nullptr); // not set by default — must be configured
    EXPECT_STREQ(cfg.index_file, "index.html");
    EXPECT_EQ(cfg.enable_etag, 1);
    EXPECT_EQ(cfg.enable_last_modified, 1);
    EXPECT_EQ(cfg.enable_range, 1);
}

TEST_F(StaticHandlerTest, GenerateEtag) {
    // create a temp file for ETag generation
    const char *test_file = "temp/test_etag_file.txt";

    // ensure temp dir exists
    FILE *f = fopen(test_file, "w");
    if (f) {
        fputs("hello etag test", f);
        fclose(f);

        char *etag = static_generate_etag(test_file);
        ASSERT_NE(etag, nullptr);
        EXPECT_GT(strlen(etag), 0u);
        serve_free(etag);

        remove(test_file);
    }
}

TEST_F(StaticHandlerTest, GenerateEtagNonexistent) {
    char *etag = static_generate_etag("nonexistent_file_xyz");
    EXPECT_EQ(etag, nullptr);
}

// ============================================================================
// HTTP Request Parsing
// ============================================================================

class HttpRequestTest : public ::testing::Test {};

TEST_F(HttpRequestTest, ParseGet) {
    const char *raw = "GET /index.html HTTP/1.1\r\n"
                      "Host: example.com\r\n"
                      "Accept: text/html\r\n"
                      "\r\n";
    HttpRequest *req = http_request_parse(raw, strlen(raw));
    ASSERT_NE(req, nullptr);

    EXPECT_EQ(req->method, HTTP_GET);
    EXPECT_STREQ(req->path, "/index.html");
    EXPECT_EQ(req->http_version_major, 1);
    EXPECT_EQ(req->http_version_minor, 1);
    EXPECT_STREQ(http_request_header(req, "Host"), "example.com");
    EXPECT_STREQ(http_request_header(req, "Accept"), "text/html");

    http_request_destroy(req);
}

TEST_F(HttpRequestTest, ParsePost) {
    const char *raw = "POST /api/data HTTP/1.1\r\n"
                      "Content-Type: application/json\r\n"
                      "Content-Length: 13\r\n"
                      "\r\n"
                      "{\"key\":\"val\"}";
    HttpRequest *req = http_request_parse(raw, strlen(raw));
    ASSERT_NE(req, nullptr);

    EXPECT_EQ(req->method, HTTP_POST);
    EXPECT_STREQ(req->path, "/api/data");
    EXPECT_STREQ(http_request_body(req), "{\"key\":\"val\"}");
    EXPECT_EQ(req->body_len, 13u);
    EXPECT_STREQ(http_request_content_type(req), "application/json");
    EXPECT_EQ(http_request_content_length(req), 13);

    http_request_destroy(req);
}

TEST_F(HttpRequestTest, ParseQueryString) {
    const char *raw = "GET /search?q=hello&page=2 HTTP/1.1\r\n"
                      "Host: example.com\r\n"
                      "\r\n";
    HttpRequest *req = http_request_parse(raw, strlen(raw));
    ASSERT_NE(req, nullptr);

    EXPECT_STREQ(req->path, "/search");
    EXPECT_STREQ(req->query_string, "q=hello&page=2");
    EXPECT_STREQ(http_request_query(req, "q"), "hello");
    EXPECT_STREQ(http_request_query(req, "page"), "2");
    EXPECT_EQ(http_request_query(req, "missing"), nullptr);

    http_request_destroy(req);
}

TEST_F(HttpRequestTest, ParseFragment) {
    const char *raw = "GET /page#section HTTP/1.1\r\n\r\n";
    HttpRequest *req = http_request_parse(raw, strlen(raw));
    ASSERT_NE(req, nullptr);

    EXPECT_STREQ(req->path, "/page");
    EXPECT_STREQ(req->fragment, "section");

    http_request_destroy(req);
}

TEST_F(HttpRequestTest, ParseCookies) {
    const char *raw = "GET / HTTP/1.1\r\n"
                      "Cookie: session=abc; user=jane\r\n"
                      "\r\n";
    HttpRequest *req = http_request_parse(raw, strlen(raw));
    ASSERT_NE(req, nullptr);

    EXPECT_STREQ(http_request_cookie(req, "session"), "abc");
    EXPECT_STREQ(http_request_cookie(req, "user"), "jane");
    EXPECT_EQ(http_request_cookie(req, "missing"), nullptr);

    http_request_destroy(req);
}

TEST_F(HttpRequestTest, AcceptsJson) {
    const char *raw = "GET / HTTP/1.1\r\n"
                      "Accept: application/json\r\n"
                      "\r\n";
    HttpRequest *req = http_request_parse(raw, strlen(raw));
    ASSERT_NE(req, nullptr);

    EXPECT_EQ(http_request_accepts_json(req), 1);
    http_request_destroy(req);
}

TEST_F(HttpRequestTest, AcceptsJsonWildcard) {
    const char *raw = "GET / HTTP/1.1\r\n"
                      "Accept: */*\r\n"
                      "\r\n";
    HttpRequest *req = http_request_parse(raw, strlen(raw));
    ASSERT_NE(req, nullptr);

    EXPECT_EQ(http_request_accepts_json(req), 1);
    http_request_destroy(req);
}

TEST_F(HttpRequestTest, DoesNotAcceptJson) {
    const char *raw = "GET / HTTP/1.1\r\n"
                      "Accept: text/html\r\n"
                      "\r\n";
    HttpRequest *req = http_request_parse(raw, strlen(raw));
    ASSERT_NE(req, nullptr);

    EXPECT_EQ(http_request_accepts_json(req), 0);
    http_request_destroy(req);
}

TEST_F(HttpRequestTest, IsSecureViaProxy) {
    const char *raw = "GET / HTTP/1.1\r\n"
                      "X-Forwarded-Proto: https\r\n"
                      "\r\n";
    HttpRequest *req = http_request_parse(raw, strlen(raw));
    ASSERT_NE(req, nullptr);

    EXPECT_EQ(http_request_is_secure(req), 1);
    http_request_destroy(req);
}

TEST_F(HttpRequestTest, IsNotSecure) {
    const char *raw = "GET / HTTP/1.1\r\n\r\n";
    HttpRequest *req = http_request_parse(raw, strlen(raw));
    ASSERT_NE(req, nullptr);

    EXPECT_EQ(http_request_is_secure(req), 0);
    http_request_destroy(req);
}

TEST_F(HttpRequestTest, FullUrl) {
    const char *raw = "GET /path?q=1 HTTP/1.1\r\n"
                      "Host: example.com\r\n"
                      "\r\n";
    HttpRequest *req = http_request_parse(raw, strlen(raw));
    ASSERT_NE(req, nullptr);

    char *url = http_request_full_url(req);
    ASSERT_NE(url, nullptr);
    EXPECT_STREQ(url, "http://example.com/path?q=1");
    serve_free(url);

    http_request_destroy(req);
}

TEST_F(HttpRequestTest, KeepAliveHttp11) {
    const char *raw = "GET / HTTP/1.1\r\n\r\n";
    HttpRequest *req = http_request_parse(raw, strlen(raw));
    ASSERT_NE(req, nullptr);
    EXPECT_EQ(req->_keep_alive, 1); // HTTP/1.1 defaults to keep-alive
    http_request_destroy(req);
}

TEST_F(HttpRequestTest, KeepAliveExplicitClose) {
    const char *raw = "GET / HTTP/1.1\r\nConnection: close\r\n\r\n";
    HttpRequest *req = http_request_parse(raw, strlen(raw));
    ASSERT_NE(req, nullptr);
    EXPECT_EQ(req->_keep_alive, 0);
    http_request_destroy(req);
}

TEST_F(HttpRequestTest, ParseNull) {
    EXPECT_EQ(http_request_parse(NULL, 0), nullptr);
}

TEST_F(HttpRequestTest, ParseEmpty) {
    EXPECT_EQ(http_request_parse("", 0), nullptr);
}

TEST_F(HttpRequestTest, DestroyNull) {
    http_request_destroy(NULL); // should not crash
}

TEST_F(HttpRequestTest, AccessorsOnNull) {
    EXPECT_EQ(http_request_header(NULL, "X"), nullptr);
    EXPECT_EQ(http_request_query(NULL, "q"), nullptr);
    EXPECT_EQ(http_request_param(NULL, "id"), nullptr);
    EXPECT_EQ(http_request_cookie(NULL, "c"), nullptr);
    EXPECT_EQ(http_request_body(NULL), nullptr);
    EXPECT_EQ(http_request_content_type(NULL), nullptr);
    EXPECT_EQ(http_request_content_length(NULL), -1);
    EXPECT_EQ(http_request_accepts_json(NULL), 0);
    EXPECT_EQ(http_request_is_secure(NULL), 0);
    EXPECT_EQ(http_request_full_url(NULL), nullptr);
}

// ============================================================================
// HTTP Response (limited — can't do uv_tcp_t in unit tests)
// ============================================================================

class HttpResponseTest : public ::testing::Test {};

TEST_F(HttpResponseTest, CreateNullClient) {
    // http_response_create requires non-NULL client
    HttpResponse *resp = http_response_create(NULL);
    EXPECT_EQ(resp, nullptr);
}

TEST_F(HttpResponseTest, CookieOptionsDefault) {
    CookieOptions opts = cookie_options_default();
    EXPECT_EQ(opts.max_age, -1);
    EXPECT_STREQ(opts.path, "/");
    EXPECT_EQ(opts.http_only, 1);
    EXPECT_STREQ(opts.same_site, "Lax");
    EXPECT_EQ(opts.secure, 0);
}

TEST_F(HttpResponseTest, DestroyNull) {
    http_response_destroy(NULL); // should not crash
}

TEST_F(HttpResponseTest, StatusOnNull) {
    // should handle NULL gracefully (return NULL)
    HttpResponse *result = http_response_status(NULL, 200);
    EXPECT_EQ(result, nullptr);
}
