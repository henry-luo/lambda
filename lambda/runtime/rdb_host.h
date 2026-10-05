#pragma once

// Binds lib/rdb to the runtime (JA16.1–JA16.3, RDB4): every RDB connection
// becomes a row in the active context's rid table, so context teardown closes
// whatever an owner left open, and drivers outside the host resolve through
// the Jube rdb provider index. Idempotent; called from runtime_init.
void rdb_host_install(void);
