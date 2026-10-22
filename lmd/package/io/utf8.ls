// S2.5.8: text scalars are code points; decode without allocating document Input arenas.
pub fn scalar_value(bytes) {
    if (len(bytes) == 1) bytes[0]
    else if (len(bytes) == 2) (bytes[0] - 192) * 64 + (bytes[1] - 128)
    else if (len(bytes) == 3)
        (bytes[0] - 224) * 4096 + (bytes[1] - 128) * 64 + (bytes[2] - 128)
    else (bytes[0] - 240) * 262144 + (bytes[1] - 128) * 4096 +
        (bytes[2] - 128) * 64 + (bytes[3] - 128)
}

pub fn valid_scalar(cp, byte_count) {
    let minimum = if (byte_count == 1) 0 else if (byte_count == 2) 128
        else if (byte_count == 3) 2048 else 65536;
    cp >= minimum and cp <= 1114111 and not (cp >= 55296 and cp <= 57343)
}

fn decode_at(bytes: binary, at: int, result: string) string^ {
    if (at == len(bytes)) result
    else {
        let head = bytes[at]
        // ASCII needs neither a temporary byte slice nor multi-byte scalar checks.
        if (head < 128) decode_at(bytes,at + 1,result ++ chr(head))^
        else {
        let count = if (head < 128) 1 else if (head >= 194 and head <= 223) 2
            else if (head >= 224 and head <= 239) 3 else if (head >= 240 and head <= 244) 4 else 0;
        if (count == 0 or at + count > len(bytes) or
            not all([for (i in 1 to count - 1) bytes[at + i] >= 128 and bytes[at + i] <= 191]))
            raise error("utf8: invalid byte sequence")
        else {
            let cp = scalar_value(slice(bytes,at,at + count)^);
            if (not valid_scalar(cp,count)) raise error("utf8: invalid scalar")
            else decode_at(bytes,at + count,result ++ chr(cp))^
        }
        }
    }
}

pub fn decode(bytes: binary) string^ {
    // A token's ASCII lane joins characters once instead of allocating per-byte recursive state.
    if (all([for (b in bytes) b < 128])) join([for (b in bytes) chr(b)],"")
    else decode_at(bytes,0,"")^
}
