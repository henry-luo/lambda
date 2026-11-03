#pragma once
#include "../../lib/endian.h"
#include <string.h>

static inline void safety_channel_header(uint8_t* bytes, uint64_t session,
        uint16_t operation, uint32_t size, uint64_t sequence = 1) {
    memset(bytes, 0, 32); memcpy(bytes, "LSAF", 4); write_le16(bytes + 4, 1);
    write_le16(bytes + 6, operation); write_le64(bytes + 8, sequence);
    write_le64(bytes + 16, session); write_le32(bytes + 24, size);
}
