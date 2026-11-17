// Shared binary framing for loose objects, pack entries and binary transfers.
pub let kinds = ["chunk","manifest","state","snapshot","checkpoint","document","version","history","retention","maintenance","cache","file_page","locator","chunk_raw"]
pub let maximum = 16777216
pub fn encode_u32(value) any^ {
    if (not (value is int) or value < 0 or value > 4294967295) raise error("sync wire: invalid length")
    return binary([value%256,int(value/256)^%256,int(value/65536)^%256,int(value/16777216)^])^
}
pub fn decode_u32(bytes, offset) any^ {
    if (offset < 0 or offset+4 > len(bytes)^) raise error("sync wire: truncated length")
    return bytes[offset]+bytes[offset+1]*256+bytes[offset+2]*65536+bytes[offset+3]*16777216
}
pub fn encode(record) any^ {
    if (not contains(kinds,record.kind)^ or not (record.id is binary) or len(record.id)^ != 32 or
        not (record.box is binary) or len(record.box)^ < 32) raise error("sync wire: invalid object")
    let bytes = binary("LRP1")^ ++ binary([index_of(kinds,record.kind)^])^ ++ record.id ++ record.box
    if (len(bytes)^ > maximum) raise error("sync wire: object limit")
    return bytes
}
pub fn decode(bytes) any^ {
    if (not (bytes is binary) or len(bytes)^ < 69 or len(bytes)^ > maximum or
        slice(bytes,0,4)^ != binary("LRP1")^ or bytes[4] >= len(kinds)^)
        raise error("sync wire: invalid physical envelope")
    return {id:slice(bytes,5,37)^,kind:kinds[bytes[4]],box:slice(bytes,37,len(bytes)^)^}
}
// balanced joins copy each blob logarithmically instead of copying every accumulated prefix.
fn concatenate(parts, lo, hi) any^ {
    if (lo == hi) { return null }
    if (hi-lo == 1) { return parts[lo] }
    let middle = lo+(hi-lo) div 2
    return concatenate(parts,lo,middle)^ ++ concatenate(parts,middle,hi)^
}
pub fn concat(parts) any^ => concatenate(parts,0,len(parts)^)^
