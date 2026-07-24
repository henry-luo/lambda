#ifndef LAMBDA_BINARY_H
#define LAMBDA_BINARY_H

#include "../lambda.h"
#include "../../lib/byte_storage.h"

#ifdef __cplusplus
extern "C" {
#endif

// Returned spans are borrowed from the Binary and do not retain storage.
ByteSpan binary_span(const Binary* binary);
bool binary_init_storage(Binary* binary, ByteStorage* storage,
    size_t offset, size_t length, bool is_ascii);
bool binary_init_slice(Binary* binary, const Binary* source,
    size_t offset, size_t length);
void binary_release_storage(Binary* binary);
int64_t binary_payload_copy_count(void);
void binary_record_payload_copy(void);

#ifdef __cplusplus
}
#endif

#endif
