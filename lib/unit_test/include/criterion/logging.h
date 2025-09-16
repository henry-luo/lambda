#ifndef CRITERION_LOGGING_H
#define CRITERION_LOGGING_H

// Criterion logging compatibility header
// This provides compatibility for <criterion/logging.h>

#include "../criterion.h"

#ifdef __cplusplus
extern "C" {
#endif

// Logging functions - minimal implementation for compatibility
#define cr_log_info(...)    printf(__VA_ARGS__)
#define cr_log_warn(...)    printf("WARNING: " __VA_ARGS__)
#define cr_log_error(...)   printf("ERROR: " __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif // CRITERION_LOGGING_H
