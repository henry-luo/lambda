// public_suffix.h
// Minimal public suffix list check for cookie domain validation (RFC 6265)

#ifndef PUBLIC_SUFFIX_H
#define PUBLIC_SUFFIX_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Check if a domain is a public suffix (e.g., "com", "co.uk", "github.io").
// Used to prevent cookies from being set on TLD/public suffix domains.
// Returns true if the domain is a known public suffix.
bool is_public_suffix(const char* domain);

#ifdef __cplusplus
}
#endif

#endif // PUBLIC_SUFFIX_H
