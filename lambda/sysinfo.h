/**
 * sysinfo.h - System information provider for sys.* paths
 *
 * Provides system information through Lambda's lazy path resolution mechanism.
 * Accessed via sys.* paths like sys.os.name, sys.cpu.cores, sys.memory.total.
 *
 * All functions use C linkage for compatibility with path.c
 */

#ifndef LAMBDA_SYSINFO_H
#define LAMBDA_SYSINFO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lambda.h"

// Forward declarations
typedef struct Path Path;

/**
 * Initialize sysinfo module.
 * Must be called before any sysinfo_resolve_* functions.
 */
void sysinfo_init(void);

/**
 * Set command line arguments for sys.proc.self.args access.
 * Should be called early in main() before any sys path resolution.
 *
 * @param argc Argument count from main()
 * @param argv Argument vector from main()
 */
void sysinfo_set_args(int argc, char** argv);

/**
 * Shutdown sysinfo module and free resources.
 */
void sysinfo_shutdown(void);

/**
 * Resolve a sys.* path to its value.
 *
 * @param path The sys.* path to resolve (e.g., sys.os.name)
 * @return The resolved value, or ITEM_NULL if not found
 *
 * Path structure:
 *   sys              → Map{os, cpu, memory, proc, time, lambda, home, temp}
 *   sys.os           → Map{name, version, kernel, machine, ...}
 *   sys.os.name      → String "Darwin", "Linux", "Windows"
 *   sys.cpu          → Map{cores, threads, arch, ...}
 *   sys.memory       → Map{total, free, used, ...}
 *   sys.proc.self    → Map{pid, cwd, args, env}
 *   sys.proc.self.env.PATH → String (environment variable)
 *   sys.time         → Map{now, uptime}
 *   sys.lambda       → Map{version}
 *   sys.home         → Path (user home directory)
 *   sys.temp         → Path (temp directory)
 */
Item sysinfo_resolve_path(Path* path);

/**
 * Invalidate all cached sysinfo data.
 * Forces fresh data on next access.
 */
void sysinfo_invalidate_cache(void);

#ifdef __cplusplus
}
#endif

#endif // LAMBDA_SYSINFO_H
