#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Inflate one complete zlib stream into exactly `output_size` bytes. Fails when
// the stream is malformed, ends short, or would produce more; the output size
// is the caller's declared bound, so compression amplification cannot exceed
// it. On success `*consumed` (when given) receives the input bytes the stream
// used, so each format decides which trailing bytes it tolerates.
bool zlib_inflate_exact(const uint8_t* input, size_t input_size, uint8_t* output, size_t output_size,
    size_t* consumed);

#ifdef __cplusplus
}
#endif
