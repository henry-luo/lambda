/**
 * @file rdb_tunnel.h
 * @brief Internal: the host TLS bridge for RDB drivers (RDB11).
 *
 * A tunnel listens on a private Unix socket. Each local connection the client
 * library makes is bridged to a fresh host-owned upstream connection to one
 * authorised target, upgraded to TLS with the host's mbedTLS according to the
 * wire protocol, then relayed. Vendor clients therefore never open network
 * sockets or carry a TLS library (JA16).
 */

#ifndef LIB_RDB_TUNNEL_H
#define LIB_RDB_TUNNEL_H

#include "rdb_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RdbTunnel RdbTunnel;

/**
 * Start a tunnel to `target`. On success writes the local endpoint into
 * `out_endpoint` (a directory for PostgreSQL, a socket path for MySQL).
 * Returns NULL and logs the reason on failure.
 */
RdbTunnel* rdb_tunnel_open(const RdbTarget* target, const RdbTunnelSpec* spec,
                           char* out_endpoint, size_t cap);

/** Stop the listener, end every bridged session, and remove the endpoint. */
void rdb_tunnel_close(RdbTunnel* tunnel);

#ifdef __cplusplus
}
#endif

#endif /* LIB_RDB_TUNNEL_H */
