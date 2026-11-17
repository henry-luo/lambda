// Immutable independently encrypted blobs followed by an authenticated recovery index.
import wire: .object_wire
import crypto: lambda.io.crypto
import hex: lambda.security.tuf.metadata
pub let target = 16777216
pub let maximum = 33554432
pub let count_limit = 4096
fn context(keys) => "lambda-repository-pack-index-1:" ++ keys.space
pub fn is_pack(bytes) any^ => bytes is binary and len(bytes)^ >= 4 and slice(bytes,0,4)^ == binary("LPK1")^
pub pn encode(keys, records) any^ {
    if (len(records)^ < 1 or len(records)^ > count_limit or
        len(unique([for (record in records) record.id]))^ != len(records)^)
        raise error("sync pack: invalid record count or duplicate")
    var parts = [binary("LPK1")^]
    var body_size = 4
    var header = binary("LPI1")^ ++ wire.encode_u32(len(records)^)^
    var entries = []
    var position = 0
    while (position < len(records)^) {
        let record = records[position]
        position = position+1
        let bytes = wire.encode(record)^
        let entry = {id:record.id,offset:body_size,length:len(bytes)^}
        entries = [*entries,entry]
        header = header ++ record.id ++ wire.encode_u32(entry.offset)^ ++ wire.encode_u32(entry.length)^
        parts = [*parts,bytes]
        body_size = body_size+len(bytes)^
        if (body_size > maximum-1048576) raise error("sync pack: byte limit")
    }
    let sealed = crypto.seal(keys.encryption,header,context(keys))^
    let bytes = wire.concat([*parts,sealed,wire.encode_u32(len(sealed)^)^])^
    return {bytes:bytes,entries:entries,id:hex.hex(hash(bytes,'sha256')^)^}
}
pub fn index(keys, tail, size) any^ {
    if (not (tail is binary) or len(tail)^ < 44 or size > maximum or size < len(tail)^+4)
        raise error("sync pack: invalid size")
    let n = wire.decode_u32(tail,len(tail)^-4)^
    if (n != len(tail)^-4) raise error("sync pack: invalid index length")
    let header = crypto.open(keys.encryption,slice(tail,0,n)^,context(keys))^
    if (len(header)^ < 8 or slice(header,0,4)^ != binary("LPI1")^) raise error("sync pack: invalid index")
    let count = wire.decode_u32(header,4)^
    if (count < 1 or count > count_limit or len(header)^ != 8+count*40)
        raise error("sync pack: invalid index count")
    var entries = []
    var offset = 4
    var i = 0
    while (i < count) {
        let at = 8+i*40
        let entry = {id:slice(header,at,at+32)^,offset:wire.decode_u32(header,at+32)^,length:wire.decode_u32(header,at+36)^}
        if (entry.offset != offset or entry.length < 69 or entry.length > wire.maximum)
            raise error("sync pack: invalid record range")
        offset = offset+entry.length
        entries = [*entries,entry]
        i = i+1
    }
    if (offset != size-len(tail)^ or len(unique([for (entry in entries) entry.id]))^ != count)
        raise error("sync pack: overlapping, duplicate or incomplete index")
    return entries
}
pub fn decode(keys, bytes, expected=null) any^ {
    if (not is_pack(bytes)^ or len(bytes)^ > maximum or len(bytes)^ < 48 or
        (expected != null and hex.hex(hash(bytes,'sha256')^)^ != expected))
        raise error("sync pack: invalid physical identity")
    let n = wire.decode_u32(bytes,len(bytes)^-4)^
    if (n > 1048576 or n+8 > len(bytes)^) raise error("sync pack: invalid index length")
    let entries = index(keys,slice(bytes,len(bytes)^-n-4)^,len(bytes)^)^
    return [for (entry in entries) entry_record(bytes,entry)^]
}
fn entry_record(bytes, entry) any^ {
    let record = wire.decode(slice(bytes,entry.offset,entry.offset+entry.length)^)^
    if (record.id != entry.id) raise error("sync pack: record/index mismatch")
    return record
}
