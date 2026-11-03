#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
// Process-wide initialization shared by ordinary downloads and isolated pulls.
bool curl_runtime_initialize(void);
// False if this shared runtime was initialized with ambient TLS key logging,
// even if the environment was later cleared. No environment mutation or cleanup.
bool curl_runtime_private_tls_ready(void);
#ifdef __cplusplus
}
#endif
