#pragma once

#include "../lambda.h"

// The IO parser builds an arena/pool-owned Mark result; active runtime callers
// supply their current pool rather than making document parsing read rt TLS.
#ifdef __cplusplus
extern "C" {
#endif
Item pdf_parse_content_stream_io(Pool* pool, Item bytes_item);
#ifdef __cplusplus
}
#endif
