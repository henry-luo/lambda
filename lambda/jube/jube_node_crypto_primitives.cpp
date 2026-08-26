// Host-owned digest entry points used by the Lambda JIT's legacy SHA imports.
// Node crypto's namespace and state live in node-crypto; these three functions
// remain here because the core JIT runtime is linked into the host executable.
#include "../js/js_runtime.h"
#include "../js/js_typed_array.h"
#include "../lambda-data.hpp"
#include "../../lib/digest.h"
#include "../../lib/log.h"

#include <cstring>

static Item jube_crypto_native_sha(Item data_item, Item offset_item, Item length_item,
        int bits) {
    if (!js_is_typed_array(data_item)) {
        log_error("JUBE_CRYPTO: native SHA input is not a typed array");
        return (Item){.item = ITEM_NULL};
    }
    int offset = (int)it2i(offset_item);
    int length = (int)it2i(length_item);
    int byte_length = js_typed_array_byte_length(data_item);
    const uint8_t* data = (const uint8_t*)js_typed_array_current_data_ptr(data_item);
    if (!data && byte_length > 0) return (Item){.item = ITEM_NULL};
    if (offset < 0) offset = 0;
    if (offset > byte_length) offset = byte_length;
    if (length < 0 || length > byte_length - offset) length = byte_length - offset;

    int output_length = bits == DIGEST_SHA256 ? 32 : bits == DIGEST_SHA384 ? 48 : 64;
    uint8_t output[64] = {};
    const uint8_t* input = data ? data + offset : NULL;
    if (!digest_compute_bits(bits, input, (size_t)length, output, (size_t)output_length)) {
        log_error("JUBE_CRYPTO: native SHA digest computation failed");
        return (Item){.item = ITEM_NULL};
    }
    Item result = js_typed_array_new(JS_TYPED_UINT8, output_length);
    uint8_t* destination = (uint8_t*)js_typed_array_prepare_write_ptr(result);
    if (destination) memcpy(destination, output, (size_t)output_length);
    return result;
}

extern "C" Item js_native_sha256(Item data_item, Item offset_item, Item length_item) {
    return jube_crypto_native_sha(data_item, offset_item, length_item, DIGEST_SHA256);
}

extern "C" Item js_native_sha384(Item data_item, Item offset_item, Item length_item) {
    return jube_crypto_native_sha(data_item, offset_item, length_item, DIGEST_SHA384);
}

extern "C" Item js_native_sha512(Item data_item, Item offset_item, Item length_item) {
    return jube_crypto_native_sha(data_item, offset_item, length_item, DIGEST_SHA512);
}
