/**
 * @file example_server.c
 * @brief simple example HTTP/HTTPS server
 *
 * this example demonstrates how to use the server library to create
 * a basic web server with both HTTP and HTTPS support.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "../../lib/serve/server.h"
#include "../../lib/serve/http_handler.h"
#include "../../lib/serve/tls_handler.h"
#include "../../lib/serve/utils.h"

// server instance for signal handling
static server_t *g_server = NULL;

/**
 * signal handler for graceful shutdown
 */
void handle_signal(int sig) {
    printf("\nreceived signal %d, shutting down...\n", sig);
    if (g_server) {
        server_stop(g_server);
    }
}

/**
 * hello world request handler
 */
void hello_handler(struct evhttp_request *req, void *user_data) {
    const char *uri = evhttp_request_get_uri(req);
    enum evhttp_cmd_type method = evhttp_request_get_command(req);
    
    SERVE_LOG_INFO("received %s request for %s", 
                   http_method_string(method), uri);
    
    // create response
    http_response_t *response = http_response_create(req);
    if (!response) {
        http_send_error(req, 500, "failed to create response");
        return;
    }
    
    // set content type
    http_response_set_header(response, "Content-Type", "text/html");
    
    // add content
    http_response_add_string(response, "<!DOCTYPE html>\n");
    http_response_add_string(response, "<html><head><title>hello world</title></head>\n");
    http_response_add_string(response, "<body>\n");
    http_response_add_string(response, "<h1>hello world!</h1>\n");
    http_response_add_printf(response, "<p>method: %s</p>\n", 
                            http_method_string(method));
    http_response_add_printf(response, "<p>uri: %s</p>\n", uri);
    http_response_add_string(response, "<p>this is a simple example server.</p>\n");
    http_response_add_string(response, "</body></html>\n");
    
    // send response
    http_response_destroy(response);
}

/**
 * api handler for json responses
 */
void api_handler(struct evhttp_request *req, void *user_data) {
    enum evhttp_cmd_type method = evhttp_request_get_command(req);
    
    // only allow get requests
    if (method != EVHTTP_REQ_GET) {
        http_send_error(req, 405, "method not allowed");
        return;
    }
    
    // create json response
    http_response_t *response = http_response_create(req);
    if (!response) {
        http_send_error(req, 500, "failed to create response");
        return;
    }
    
    http_response_set_header(response, "Content-Type", "application/json");
    http_response_add_string(response, "{\n");
    http_response_add_string(response, "  \"message\": \"hello from api\",\n");
    http_response_add_string(response, "  \"version\": \"1.0\",\n");
    http_response_add_string(response, "  \"server\": \"jubily\"\n");
    http_response_add_string(response, "}\n");
    
    http_response_destroy(response);
}

/**
 * file serving handler
 */
void file_handler(struct evhttp_request *req, void *user_data) {
    const char *uri = evhttp_request_get_uri(req);
    const char *document_root = (const char *)user_data;
    
    // simple security check - prevent directory traversal
    if (strstr(uri, "..") || strstr(uri, "//")) {
        http_send_error(req, 403, "forbidden");
        return;
    }
    
    // construct file path
    char filepath[1024];
    snprintf(filepath, sizeof(filepath), "%s%s", 
             document_root ? document_root : ".", uri);
    
    // if uri ends with /, append index.html
    if (uri[strlen(uri) - 1] == '/') {
        strncat(filepath, "index.html", sizeof(filepath) - strlen(filepath) - 1);
    }
    
    // try to serve the file
    if (http_send_file(req, filepath) != 0) {
        http_send_error(req, 404, "file not found");
    }
}

/**
 * default handler for unmatched requests
 */
void default_handler(struct evhttp_request *req, void *user_data) {
    const char *uri = evhttp_request_get_uri(req);
    
    SERVE_LOG_INFO("unmatched request for %s", uri);
    
    http_response_t *response = http_response_create(req);
    if (!response) {
        http_send_error(req, 500, "failed to create response");
        return;
    }
    
    http_response_set_status(response, 404);
    http_response_set_header(response, "Content-Type", "text/html");
    
    http_response_add_string(response, "<!DOCTYPE html>\n");
    http_response_add_string(response, "<html><head><title>not found</title></head>\n");
    http_response_add_string(response, "<body>\n");
    http_response_add_string(response, "<h1>404 - not found</h1>\n");
    http_response_add_printf(response, "<p>the requested uri '%s' was not found.</p>\n", uri);
    http_response_add_string(response, "<p><a href=\"/\">go back to home</a></p>\n");
    http_response_add_string(response, "</body></html>\n");
    
    http_response_destroy(response);
}

/**
 * print usage information
 */
void print_usage(const char *program_name) {
    printf("usage: %s [options]\n", program_name);
    printf("options:\n");
    printf("  -p PORT     http port (default: 8080)\n");
    printf("  -s PORT     https port (default: 8443)\n");
    printf("  -a ADDRESS  bind address (default: 0.0.0.0)\n");
    printf("  -c CERT     ssl certificate file\n");
    printf("  -k KEY      ssl private key file\n");
    printf("  -d DIR      document root directory\n");
    printf("  -v          verbose logging\n");
    printf("  -h          show this help\n");
}

/**
 * main function
 */
int main(int argc, char *argv[]) {
    // default configuration
    server_config_t config = server_config_default();
    int verbose = 0;
    
    // parse command line arguments
    int opt;
    while ((opt = getopt(argc, argv, "p:s:a:c:k:d:vh")) != -1) {
        switch (opt) {
            case 'p':
                config.port = atoi(optarg);
                break;
            case 's':
                config.ssl_port = atoi(optarg);
                break;
            case 'a':
                config.bind_address = serve_strdup(optarg);
                break;
            case 'c':
                config.ssl_cert_file = serve_strdup(optarg);
                break;
            case 'k':
                config.ssl_key_file = serve_strdup(optarg);
                break;
            case 'd':
                config.document_root = serve_strdup(optarg);
                break;
            case 'v':
                verbose = 1;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    // set log level
    serve_set_log_level(verbose ? LOG_DEBUG : LOG_INFO);
    
    // if https is enabled but no certificate files specified, generate self-signed
    if (config.ssl_port > 0 && (!config.ssl_cert_file || !config.ssl_key_file)) {
        printf("https enabled but no certificate files specified\n");
        printf("generating self-signed certificate for testing...\n");
        
        config.ssl_cert_file = serve_strdup("/tmp/server_cert.pem");
        config.ssl_key_file = serve_strdup("/tmp/server_key.pem");
        
        if (tls_generate_self_signed_cert(config.ssl_cert_file, config.ssl_key_file,
                                         365, "localhost") != 0) {
            fprintf(stderr, "failed to generate self-signed certificate\n");
            return 1;
        }
        
        printf("self-signed certificate generated:\n");
        printf("  certificate: %s\n", config.ssl_cert_file);
        printf("  private key: %s\n", config.ssl_key_file);
    }
    
    // create server
    g_server = server_create(&config);
    if (!g_server) {
        fprintf(stderr, "failed to create server: %s\n", server_get_error());
        return 1;
    }
    
    // setup signal handlers
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    // setup request handlers
    server_set_handler(g_server, "/", hello_handler, NULL);
    server_set_handler(g_server, "/hello", hello_handler, NULL);
    server_set_handler(g_server, "/api", api_handler, NULL);
    server_set_handler(g_server, "/files", file_handler, config.document_root);
    server_set_default_handler(g_server, default_handler, NULL);
    
    // start server
    if (server_start(g_server) != 0) {
        fprintf(stderr, "failed to start server: %s\n", server_get_error());
        server_destroy(g_server);
        return 1;
    }
    
    // print server information
    printf("server started successfully\n");
    if (config.port > 0) {
        printf("http server: http://%s:%d/\n", 
               config.bind_address ? config.bind_address : "localhost", 
               config.port);
    }
    if (config.ssl_port > 0) {
        printf("https server: https://%s:%d/\n", 
               config.bind_address ? config.bind_address : "localhost", 
               config.ssl_port);
    }
    printf("press ctrl+c to stop\n\n");
    
    // run server
    int result = server_run(g_server);
    
    // cleanup
    server_destroy(g_server);
    server_config_cleanup(&config);
    tls_cleanup();
    
    printf("server shutdown complete\n");
    return result;
}
