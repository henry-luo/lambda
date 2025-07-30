/**
 * @file test_server.c
 * @brief basic tests for the HTTP/HTTPS server
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>

// include server components
#include "../../lib/serve/server.h"
#include "../../lib/serve/utils.h"
#include "../../lib/serve/http_handler.h"
#include "../../lib/serve/tls_handler.h"

// test configuration
#define TEST_HTTP_PORT 18080
#define TEST_HTTPS_PORT 18443
#define TEST_CERT_FILE "/tmp/test_cert.pem"
#define TEST_KEY_FILE "/tmp/test_key.pem"

// test state
static int test_count = 0;
static int test_passed = 0;

// test macros
#define TEST_START(name) \
    do { \
        printf("running test: %s\n", name); \
        test_count++; \
    } while (0)

#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            printf("  FAIL: %s\n", message); \
            return; \
        } \
    } while (0)

#define TEST_PASS() \
    do { \
        printf("  PASS\n"); \
        test_passed++; \
    } while (0)

/**
 * test utility functions
 */
void test_utils(void) {
    TEST_START("utils");
    
    // test string duplication
    char *dup = serve_strdup("hello world");
    TEST_ASSERT(dup != NULL, "serve_strdup should not return null");
    TEST_ASSERT(strcmp(dup, "hello world") == 0, "serve_strdup should duplicate correctly");
    serve_free(dup);
    
    // test null string duplication
    dup = serve_strdup(NULL);
    TEST_ASSERT(dup == NULL, "serve_strdup(null) should return null");
    
    // test case-insensitive comparison
    TEST_ASSERT(serve_strcasecmp("hello", "HELLO") == 0, "case-insensitive comparison should work");
    TEST_ASSERT(serve_strcasecmp("hello", "world") != 0, "different strings should not match");
    
    // test string trimming
    char str[] = "  hello world  ";
    char *trimmed = serve_strtrim(str);
    TEST_ASSERT(strcmp(trimmed, "hello world") == 0, "string trimming should work");
    
    // test url decoding
    char url[] = "hello%20world%21";
    size_t len = serve_url_decode(url);
    TEST_ASSERT(strcmp(url, "hello world!") == 0, "url decoding should work");
    TEST_ASSERT(len == 12, "url decode should return correct length");
    
    // test file extension
    const char *ext = serve_get_file_extension("test.html");
    TEST_ASSERT(ext != NULL && strcmp(ext, ".html") == 0, "file extension should be extracted");
    
    ext = serve_get_file_extension("noextension");
    TEST_ASSERT(ext == NULL, "no extension should return null");
    
    // test mime type
    const char *mime = serve_get_mime_type(".html");
    TEST_ASSERT(strcmp(mime, "text/html") == 0, "html mime type should be correct");
    
    mime = serve_get_mime_type(".unknown");
    TEST_ASSERT(strcmp(mime, "application/octet-stream") == 0, "unknown extension should return default");
    
    TEST_PASS();
}

/**
 * test server configuration
 */
void test_server_config(void) {
    TEST_START("server_config");
    
    // test default configuration
    server_config_t config = server_config_default();
    TEST_ASSERT(config.port == 8080, "default http port should be 8080");
    TEST_ASSERT(config.ssl_port == 8443, "default https port should be 8443");
    TEST_ASSERT(config.max_connections == 1024, "default max connections should be 1024");
    TEST_ASSERT(config.timeout_seconds == 60, "default timeout should be 60");
    
    // test invalid configuration
    config.port = 0;
    config.ssl_port = 0;
    TEST_ASSERT(server_config_validate(&config) != 0, "config with no ports should be invalid");
    
    // test valid http-only configuration
    config.port = TEST_HTTP_PORT;
    config.ssl_port = 0;
    TEST_ASSERT(server_config_validate(&config) == 0, "http-only config should be valid");
    
    TEST_PASS();
}

/**
 * test ssl certificate generation
 */
void test_ssl_generation(void) {
    TEST_START("ssl_generation");
    
    // initialize ssl
    TEST_ASSERT(tls_init() == 0, "ssl initialization should succeed");
    
    // generate self-signed certificate
    int result = tls_generate_self_signed_cert(TEST_CERT_FILE, TEST_KEY_FILE, 
                                              30, "localhost");
    TEST_ASSERT(result == 0, "certificate generation should succeed");
    
    // check if files were created
    TEST_ASSERT(serve_file_exists(TEST_CERT_FILE), "certificate file should exist");
    TEST_ASSERT(serve_file_exists(TEST_KEY_FILE), "key file should exist");
    
    // validate certificate and key
    TEST_ASSERT(tls_is_valid_certificate(TEST_CERT_FILE), "certificate should be valid");
    TEST_ASSERT(tls_is_valid_private_key(TEST_KEY_FILE), "private key should be valid");
    TEST_ASSERT(tls_certificate_key_match(TEST_CERT_FILE, TEST_KEY_FILE), 
               "certificate and key should match");
    
    TEST_PASS();
}

/**
 * test http handler functionality
 */
void test_http_handler(void) {
    TEST_START("http_handler");
    
    // test method string conversion
    const char *method = http_method_string(EVHTTP_REQ_GET);
    TEST_ASSERT(strcmp(method, "GET") == 0, "get method string should be correct");
    
    method = http_method_string(EVHTTP_REQ_POST);
    TEST_ASSERT(strcmp(method, "POST") == 0, "post method string should be correct");
    
    // test status string conversion
    const char *status = http_status_string(200);
    TEST_ASSERT(strcmp(status, "OK") == 0, "200 status string should be correct");
    
    status = http_status_string(404);
    TEST_ASSERT(strcmp(status, "Not Found") == 0, "404 status string should be correct");
    
    // test method allowed checking
    int allowed = HTTP_METHOD_GET | HTTP_METHOD_POST;
    TEST_ASSERT(http_method_allowed(EVHTTP_REQ_GET, allowed), "get should be allowed");
    TEST_ASSERT(http_method_allowed(EVHTTP_REQ_POST, allowed), "post should be allowed");
    TEST_ASSERT(!http_method_allowed(EVHTTP_REQ_PUT, allowed), "put should not be allowed");
    
    TEST_PASS();
}

/**
 * test server creation and destruction
 */
void test_server_lifecycle(void) {
    TEST_START("server_lifecycle");
    
    // create server configuration
    server_config_t config = server_config_default();
    config.port = TEST_HTTP_PORT;
    config.ssl_port = TEST_HTTPS_PORT;
    config.ssl_cert_file = serve_strdup(TEST_CERT_FILE);
    config.ssl_key_file = serve_strdup(TEST_KEY_FILE);
    
    // create server
    server_t *server = server_create(&config);
    TEST_ASSERT(server != NULL, "server creation should succeed");
    
    // test server start
    int result = server_start(server);
    TEST_ASSERT(result == 0, "server start should succeed");
    
    // test server stop
    server_stop(server);
    
    // test server destruction
    server_destroy(server);
    
    // cleanup config
    server_config_cleanup(&config);
    
    TEST_PASS();
}

/**
 * simple request handler for testing
 */
void test_request_handler(struct evhttp_request *req, void *user_data) {
    const char *message = (const char *)user_data;
    
    if (!message) {
        message = "hello from test server";
    }
    
    http_send_simple_response(req, 200, "text/plain", message);
}

/**
 * test request handling
 */
void test_request_handling(void) {
    TEST_START("request_handling");
    
    // create server configuration
    server_config_t config = server_config_default();
    config.port = TEST_HTTP_PORT;
    config.ssl_port = 0; // disable https for this test
    
    // create server
    server_t *server = server_create(&config);
    TEST_ASSERT(server != NULL, "server creation should succeed");
    
    // set request handlers
    int result = server_set_handler(server, "/test", test_request_handler, 
                                   (void *)"test response");
    TEST_ASSERT(result == 0, "setting handler should succeed");
    
    result = server_set_default_handler(server, test_request_handler, 
                                       (void *)"default response");
    TEST_ASSERT(result == 0, "setting default handler should succeed");
    
    // note: we don't actually start the server here since we would need
    // to make actual http requests to test it properly, which would
    // require additional dependencies like libcurl
    
    // cleanup
    server_destroy(server);
    
    TEST_PASS();
}

/**
 * cleanup test files
 */
void cleanup_test_files(void) {
    unlink(TEST_CERT_FILE);
    unlink(TEST_KEY_FILE);
}

/**
 * main test function
 */
int main(void) {
    printf("starting server tests\n\n");
    
    // set log level for testing
    serve_set_log_level(LOG_WARN);
    
    // run tests
    test_utils();
    test_server_config();
    test_ssl_generation();
    test_http_handler();
    test_server_lifecycle();
    test_request_handling();
    
    // cleanup
    cleanup_test_files();
    tls_cleanup();
    
    // print results
    printf("\ntest results: %d/%d passed\n", test_passed, test_count);
    
    if (test_passed == test_count) {
        printf("all tests passed!\n");
        return 0;
    } else {
        printf("some tests failed!\n");
        return 1;
    }
}
