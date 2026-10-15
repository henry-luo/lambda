#pragma once
#include "mark_builder.hpp"
#include "../../lib/digest.h"
#include "../../lib/hex.h"

// Source fingerprints identify immutable bytes, independently of paths or publication metadata.
inline Item input_snapshot_identity(MarkBuilder& builder, const void* bytes, size_t length) {
    uint8_t digest[32];
    if (!digest_sha256(bytes, length, digest)) return ItemNull;
    char text[65];
    hex_encode(digest, sizeof(digest), text);
    return builder.createStringItem(text);
}
