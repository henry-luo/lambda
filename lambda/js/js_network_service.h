#pragma once

#include "../jube/jube.h"

#include <stddef.h>

int js_node_network_ip_family(const char* address);
bool js_node_network_lookup_sync(const char* hostname, char* address, size_t address_size);
int js_node_stream_tcp_create(void* session, Item owner, uint32_t* out_resource_id);
int js_node_stream_tcp_bind(void* session, uint32_t resource_id, const char* address,
                            int port, bool ipv6_only, bool reuse_port);
int js_node_stream_tcp_address(void* session, uint32_t resource_id, char* address,
                               size_t address_size, int* out_port, int* out_family);
int js_node_stream_tcp_fd(void* session, uint32_t resource_id, int* out_fd);
int js_node_stream_tcp_adopt_fd(void* session, uint32_t resource_id, int* out_fd);
int js_node_stream_resource_close(void* session, uint32_t resource_id);
int js_node_stream_resource_ref(void* session, uint32_t resource_id, bool referenced);
bool js_node_stream_resource_is_live(void* session, uint32_t resource_id);
