#include "zlib_exact.h"
#include <limits.h>
#include <zlib.h>

bool zlib_inflate_exact(const uint8_t* input, size_t input_size, uint8_t* output, size_t output_size,
        size_t* consumed) {
    if (!input || (!output && output_size) || input_size > UINT_MAX || output_size > UINT_MAX) return false;
    z_stream stream = {0};
    stream.next_in = (Bytef*)input; stream.avail_in = (uInt)input_size;
    stream.next_out = output; stream.avail_out = (uInt)output_size;
    if (inflateInit(&stream) != Z_OK) return false;
    // Z_FINISH with a full-size window either ends the stream or reports that
    // it needs more room, which is the "would produce more" failure.
    int status = inflate(&stream, Z_FINISH);
    bool complete = status == Z_STREAM_END && stream.total_out == output_size;
    if (complete && consumed) *consumed = input_size - stream.avail_in;
    inflateEnd(&stream);
    return complete;
}
