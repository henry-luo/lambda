#include <gtest/gtest.h>
#include "../lambda/input/input.hpp"
#include "../lambda/network/http_client.h"
#include "../lib/url.h"
#include "../lib/string.h"
#include "../lib/mempool.h"
#include "../lib/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct HttpHeaderCaptureServer {
    pid_t pid;
    int request_fd;
    int port;
} HttpHeaderCaptureServer;

static bool http_header_capture_server_start(HttpHeaderCaptureServer* server,
                                             unsigned int response_delay_seconds = 0) {
    if (!server) return false;
    memset(server, 0, sizeof(*server));
    server->request_fd = -1;

    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) return false;
    int reuse_address = 1;
    if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
                   &reuse_address, sizeof(reuse_address)) != 0) {
        close(listener);
        return false;
    }
    struct sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listener, (struct sockaddr*)&address, sizeof(address)) != 0 ||
        listen(listener, 1) != 0) {
        close(listener);
        return false;
    }
    socklen_t address_size = sizeof(address);
    if (getsockname(listener, (struct sockaddr*)&address, &address_size) != 0) {
        close(listener);
        return false;
    }

    int pipe_fds[2] = {-1, -1};
    if (pipe(pipe_fds) != 0) {
        close(listener);
        return false;
    }
    pid_t pid = fork();
    if (pid < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        close(listener);
        return false;
    }
    if (pid == 0) {
        close(pipe_fds[0]);
        int client = accept(listener, NULL, NULL);
        char request[8192] = {};
        size_t request_size = 0;
        while (client >= 0 && request_size + 1 < sizeof(request)) {
            ssize_t read_size = recv(client, request + request_size,
                                     sizeof(request) - request_size - 1, 0);
            if (read_size <= 0) break;
            request_size += (size_t)read_size;
            if (strstr(request, "\r\n\r\n")) break;
        }
        if (request_size > 0) write(pipe_fds[1], request, request_size);
        const char response[] = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok";
        if (client >= 0) {
            if (response_delay_seconds > 0) sleep(response_delay_seconds);
            send(client, response, sizeof(response) - 1, 0);
            close(client);
        }
        close(pipe_fds[1]);
        close(listener);
        _exit(0);
    }

    close(pipe_fds[1]);
    close(listener);
    server->pid = pid;
    server->request_fd = pipe_fds[0];
    server->port = ntohs(address.sin_port);
    return true;
}

class HttpInputTest : public ::testing::Test {
protected:
    Pool* pool;

    void SetUp() override {
        // Initialize logging
        log_init(NULL);
        // Create memory pool for tests
        pool = pool_create();
        ASSERT_NE(pool, nullptr) << "Failed to create memory pool";
    }

    void TearDown() override {
        // Cleanup memory pool
        if (pool) {
            pool_destroy(pool);
        }
    }
};

TEST_F(HttpInputTest, SendsBrowserNavigationHeadersForDocumentDownloads) {
    HttpHeaderCaptureServer server;
    ASSERT_TRUE(http_header_capture_server_start(&server));

    char url[128];
    int url_size = snprintf(url, sizeof(url), "http://127.0.0.1:%d/document", server.port);
    ASSERT_GT(url_size, 0);
    ASSERT_LT(url_size, (int)sizeof(url));

    size_t content_size = 0;
    char* content = download_http_content(url, &content_size, NULL);

    char request[8192] = {};
    ssize_t request_size = read(server.request_fd, request, sizeof(request) - 1);
    close(server.request_fd);
    int status = 0;
    ASSERT_EQ(server.pid, waitpid(server.pid, &status, 0));

    ASSERT_NE(nullptr, content);
    EXPECT_EQ(2u, content_size);
    EXPECT_STREQ("ok", content);
    EXPECT_TRUE(WIFEXITED(status));
    EXPECT_EQ(0, WEXITSTATUS(status));
    ASSERT_GT(request_size, 0);
    EXPECT_NE(nullptr, strstr(request, "User-Agent: " RADIANT_HTTP_CLIENT_USER_AGENT));
    EXPECT_NE(nullptr, strstr(request, RADIANT_HTTP_DOCUMENT_ACCEPT_HEADER));
    EXPECT_NE(nullptr, strstr(request, RADIANT_HTTP_DOCUMENT_LANGUAGE_HEADER));
    EXPECT_EQ(nullptr, strstr(request, "Radiant/1.0"));
    mem_free(content);
}

TEST_F(HttpInputTest, TopLevelNavigationUsesPageLoadTimeout) {
    HttpHeaderCaptureServer server;
    // A 31-second first-byte delay exceeds the generic resource limit but is
    // valid within the document navigation's 60-second page-load budget.
    ASSERT_TRUE(http_header_capture_server_start(&server, 31));

    char url[128];
    int url_size = snprintf(url, sizeof(url), "http://127.0.0.1:%d/document", server.port);
    ASSERT_GT(url_size, 0);
    ASSERT_LT(url_size, (int)sizeof(url));

    size_t content_size = 0;
    char* content = download_http_content_with_cookie_jar(url, &content_size, NULL);

    char request[8192] = {};
    ssize_t request_size = read(server.request_fd, request, sizeof(request) - 1);
    close(server.request_fd);
    int status = 0;
    ASSERT_EQ(server.pid, waitpid(server.pid, &status, 0));

    ASSERT_NE(nullptr, content);
    EXPECT_EQ(2u, content_size);
    EXPECT_STREQ("ok", content);
    EXPECT_TRUE(WIFEXITED(status));
    EXPECT_EQ(0, WEXITSTATUS(status));
    EXPECT_GT(request_size, 0);
    mem_free(content);
}

// Test HTTP functionality
TEST_F(HttpInputTest, test_http_download) {
    // Test downloading a simple JSON file from GitHub API
    const char* test_url = "https://api.github.com/zen";

    printf("Testing HTTP download from: %s\n", test_url);

    // Create temp directory first
    system("mkdir -p ./temp/cache");

    // Test just the HTTP download function directly
    size_t content_size;
    char* content = download_http_content(test_url, &content_size, NULL);

    // Verify we got content
    ASSERT_NE(content, nullptr) << "HTTP download should not return null";
    ASSERT_GT(content_size, 0) << "Content size should be greater than 0";

    printf("Downloaded %zu bytes successfully\n", content_size);

    // Cleanup
    free(content);
}

TEST_F(HttpInputTest, test_http_cache) {
    // Test that caching works by downloading the same URL twice
    const char* test_url = "https://api.github.com/octocat";

    printf("Testing HTTP caching with: %s\n", test_url);

    // First download
    char* content1 = download_to_cache(test_url, "./temp/cache", NULL);
    ASSERT_NE(content1, nullptr) << "First download should succeed";

    // Second download (should use cache)
    char* content2 = download_to_cache(test_url, "./temp/cache", NULL);
    ASSERT_NE(content2, nullptr) << "Second download should succeed";

    // Note: UUID content will be different each time, so don't check for equality
    // This test just verifies that the caching mechanism doesn't crash

    printf("HTTP caching test completed successfully\n");

    // Cleanup
    free(content1);
    free(content2);
}

TEST_F(HttpInputTest, test_https_ssl) {
    // Test HTTPS with SSL verification
    const char* test_url = "https://api.github.com/zen";

    printf("Testing HTTPS with SSL verification: %s\n", test_url);

    size_t content_size;
    char* content = download_http_content(test_url, &content_size, NULL);

    ASSERT_NE(content, nullptr) << "HTTPS download should succeed";
    ASSERT_GT(content_size, 0) << "Content size should be greater than 0";

    printf("HTTPS SSL test completed successfully\n");
    printf("Downloaded %zu bytes: %.100s%s\n",
           content_size, content, content_size > 100 ? "..." : "");

    // Cleanup
    free(content);
}

TEST_F(HttpInputTest, test_http_error_handling) {
    // Test error handling with invalid URL
    const char* invalid_url = "https://api.github.com/this-definitely-does-not-exist-404";

    printf("Testing HTTP error handling with: %s\n", invalid_url);

    char* content = download_http_content(invalid_url, NULL, NULL);

    // Should return NULL for 404 error
    ASSERT_EQ(content, nullptr) << "404 URL should return null";

    printf("HTTP error handling test completed successfully\n");
}
