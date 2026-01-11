/**
 * @file cmd_webdriver.cpp
 * @brief CLI command handler for WebDriver server
 * 
 * Implements the `lambda webdriver` command to start a W3C WebDriver server
 * for automated testing of Radiant HTML/CSS rendering.
 */

#include "webdriver.hpp"
#include "../../lib/log.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <signal.h>

static WebDriverServer* g_server = nullptr;

static void signal_handler(int sig) {
    (void)sig;
    if (g_server) {
        log_info("webdriver: received signal, stopping server");
        webdriver_server_stop(g_server);
    }
}

static void print_help(const char* prog_name) {
    printf("Lambda WebDriver Server v1.0\n\n");
    printf("Usage: %s webdriver [options]\n", prog_name);
    printf("\nDescription:\n");
    printf("  Starts a W3C WebDriver-compatible server for automated testing\n");
    printf("  of Radiant HTML/CSS rendering. Compatible with Selenium, Puppeteer,\n");
    printf("  and other WebDriver client libraries.\n");
    printf("\nOptions:\n");
    printf("  -p, --port <port>       Port to listen on (default: 4444)\n");
    printf("  --host <address>        Bind address (default: localhost)\n");
    printf("  -h, --help              Show this help message\n");
    printf("\nExamples:\n");
    printf("  %s webdriver                     # Start on port 4444\n", prog_name);
    printf("  %s webdriver --port 9515         # Start on port 9515\n", prog_name);
    printf("  %s webdriver --host 0.0.0.0      # Listen on all interfaces\n", prog_name);
    printf("\nEndpoints:\n");
    printf("  POST   /session                  Create new session\n");
    printf("  DELETE /session/:id              Delete session\n");
    printf("  POST   /session/:id/url          Navigate to URL\n");
    printf("  GET    /session/:id/url          Get current URL\n");
    printf("  POST   /session/:id/element      Find element\n");
    printf("  POST   /session/:id/elements     Find elements\n");
    printf("  POST   /session/:id/element/:id/click    Click element\n");
    printf("  POST   /session/:id/element/:id/value    Send keys to element\n");
    printf("  GET    /session/:id/screenshot   Take screenshot (base64 PNG)\n");
    printf("  GET    /status                   Server status\n");
    printf("\nSelenium Client Example (Python):\n");
    printf("  from selenium import webdriver\n");
    printf("  options = webdriver.ChromeOptions()  # Use generic options\n");
    printf("  driver = webdriver.Remote(\n");
    printf("      command_executor='http://localhost:4444',\n");
    printf("      options=options\n");
    printf("  )\n");
    printf("  driver.get('file:///path/to/test.html')\n");
    printf("  elem = driver.find_element('css selector', '#button')\n");
    printf("  elem.click()\n");
    printf("  driver.quit()\n");
}

/**
 * @brief Main entry point for the webdriver command
 * @param argc Argument count (after 'webdriver' has been stripped)
 * @param argv Argument vector (after 'webdriver' has been stripped)
 * @return 0 on success, non-zero on error
 */
int cmd_webdriver(int argc, char** argv) {
    int port = 4444;
    const char* host = "localhost";
    
    // Parse arguments
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_help("lambda");
            return 0;
        } else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) {
            if (i + 1 < argc) {
                port = atoi(argv[++i]);
                if (port <= 0 || port > 65535) {
                    printf("Error: Invalid port number '%s'\n", argv[i]);
                    return 1;
                }
            } else {
                printf("Error: --port requires a port number\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--host") == 0) {
            if (i + 1 < argc) {
                host = argv[++i];
            } else {
                printf("Error: --host requires an address\n");
                return 1;
            }
        } else if (argv[i][0] == '-') {
            printf("Error: Unknown option '%s'\n", argv[i]);
            printf("Use 'lambda webdriver --help' for usage information\n");
            return 1;
        }
    }
    
    // Set up signal handlers for graceful shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Create and start server
    g_server = webdriver_server_create(host, port);
    if (!g_server) {
        printf("Error: Failed to create WebDriver server\n");
        return 1;
    }
    
    printf("Lambda WebDriver Server v1.0\n");
    printf("Listening on http://%s:%d\n", host, port);
    printf("Press Ctrl+C to stop\n\n");
    
    // Run server (blocks until stopped)
    int result = webdriver_server_run(g_server);
    
    // Cleanup
    webdriver_server_destroy(g_server);
    g_server = nullptr;
    
    return result;
}
