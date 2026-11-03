#pragma once
#include <stddef.h>

// OS-seeded bytes shared by identifiers and WebSocket nonces/masking keys.
bool crypto_random(unsigned char* out, size_t length);
