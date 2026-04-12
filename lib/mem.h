#ifndef MEM_H
#define MEM_H

/**
 * Unified Memory Header for Lambda/Radiant
 *
 * Include this instead of <stdlib.h> in lambda/ and radiant/ source files.
 * Provides:
 *   - Tracked allocation API (mem_alloc, mem_free, etc.)
 *   - All non-allocation stdlib functions (strtol, atoi, getenv, qsort, etc.)
 *
 * When MEMTRACK_POISON_RAW_ALLOC is defined, direct calls to
 * malloc/calloc/realloc/free/strdup/strndup become compile errors.
 */

// tracked allocation API
#include "memtrack.h"

// re-export non-allocation stdlib functions (strtol, atoi, abs, getenv, qsort, exit, etc.)
#include <stdlib.h>

// poison raw allocation functions so lambda/radiant code cannot use them directly
#if defined(MEMTRACK_POISON_RAW_ALLOC)
#pragma GCC poison malloc calloc realloc free strdup strndup
#endif

#endif // MEM_H
