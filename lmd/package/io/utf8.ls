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

fn continuation(byte) => byte>=128 and byte<=191
fn scalar_width(head) => if (head<128) 1 else if (head>=194 and head<=223) 2
    else if (head>=224 and head<=239) 3 else if (head>=240 and head<=244) 4 else 0
// UTF-8 has at most three continuation bytes; lookbehind never grows with the input.
fn continuation_owner(bytes,at,distance=1) => if (distance>3 or at<distance) false
    else if (continuation(bytes[at-distance])) continuation_owner(bytes,at,distance+1)
    else scalar_width(bytes[at-distance])>distance

fn decode_piece(bytes,at) string^ {
    let head = bytes[at];
    if (head<128) chr(head)
    else if (continuation(head)) {
        if (continuation_owner(bytes,at)) "" else raise error("utf8: invalid byte sequence")
    } else {
        let count = scalar_width(head);
        if (count==0 or at+count>len(bytes) or
            not all([for (i in 1 to count-1) continuation(bytes[at+i])])) raise error("utf8: invalid byte sequence")
        else {
            let cp = scalar_value(slice(bytes,at,at+count)^);
            if (not valid_scalar(cp,count)) raise error("utf8: invalid scalar") else chr(cp)
        }
    }
}

pub fn decode(bytes: binary) string^ {
    // A token's ASCII lane joins characters once instead of allocating per-byte recursive state.
    if (all([for (b in bytes) b < 128])) join([for (b in bytes) chr(b)],"")
    // one join keeps long multilingual responses within a background task's stack (S13.3.1v2).
    else join([for (at in 0 to len(bytes)-1) decode_piece(bytes,at)^],"")
}
