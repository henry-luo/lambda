// S17.4.1: prepared resource indexes retain byte offsets, not decoded rows.
pub fn from_bytes(source) map^ {
    if (not (source is binary and len(source) > 0 and len(source) < 4294967296))
        raise error("line index: expected nonempty bytes within the u32 offset range")
    else if (source[len(source) - 1] != 10) raise error("line index: missing final LF")
    else {
        let offsets: u32[] = [0u32,*[for (i,b in source where b == 10 and i + 1 < len(source)) u32(i + 1)]];
        {bytes:source,offsets:offsets}
    }
}

pub fn line_end(table, line: int) int =>
    if (line + 1 < len(table.offsets)) int(table.offsets[line + 1]) - 1
    else len(table.bytes) - 1

pub fn line_bytes(table, line: int) binary^ =>
    slice(table.bytes,int(table.offsets[line]),line_end(table,line))^
