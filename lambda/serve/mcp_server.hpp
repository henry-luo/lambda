#pragma once
#include "server.hpp"
#include "../io/mcp.hpp"

// the registry and its callbacks must outlive the mounted server.
int server_mount_mcp(Server* server, const char* path, const McpServer* registry);
int exec_serve_mcp(int argc, char** argv);
