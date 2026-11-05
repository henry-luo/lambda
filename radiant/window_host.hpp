#pragma once

struct RenderPagedOptions;
struct NetworkThreadPool;
struct EnhancedFileCache;
struct Pool;
struct Url;

// The process window is a singleton; its host remains owned across a refused close.
bool radiant_window_host_begin(const RenderPagedOptions* options);
const RenderPagedOptions* radiant_window_host_options();
void radiant_window_host_loop_started();
bool radiant_window_host_finish(NetworkThreadPool* pool, EnhancedFileCache* cache,
    Pool* load_pool, Url* cwd, bool log_memory, bool reset_virtual_clock);
bool radiant_window_host_retry();
bool radiant_window_host_pending();
// Process-wide teardown must not reclaim services while a host is still owned.
bool radiant_window_host_process_shutdown_ready();
