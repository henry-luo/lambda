import c: .common
import index: lambda.io.line_index
import search: lambda.doc.diff.common
import utf8: lambda.io.utf8

pub fn from_bytes(manifest,lexicon,abbrev,profile = "zh-Hans") map^ {
    let header = c.require(manifest is map and manifest.format == "lambda.ime.chinese.pack" and
        manifest.format_version == 1 and manifest.profile == profile,
        "unsupported pack format, version or profile")^
    let sizes = c.require(lexicon is binary and abbrev is binary and
        len(lexicon) == manifest.files.lexicon.bytes and len(abbrev) == manifest.files.abbrev.bytes,
        "table byte size mismatch; prepare the pack again")^
    let full = index.from_bytes(lexicon) ^ { raise c.failure(^.message) }
    let short = index.from_bytes(abbrev) ^ { raise c.failure(^.message) };
    {manifest:manifest,profile:profile,lexicon:full,abbrev:short}
}

// key probes compare ASCII in place; only returned postings are decoded.
fn compare_at(raw: binary,at: int,key: binary,i: int = 0) int {
    let byte = raw[at + i]
    let end = byte == 9 or byte == 10;
    if (i == len(key)) (if (end) 0 else 1)
    else if (end or byte < key[i]) -1
    else if (byte > key[i]) 1 else compare_at(raw,at,key,i + 1)
}

pub fn lower_bound(table,key: string) int^ {
    let bytes = binary(key)^
    let raw: binary = table.bytes;
    search.lower_bound(table.offsets,0,0,null,(at: int) => compare_at(raw,at,bytes))
}

pub fn equal(table,line: int,key: string) bool^ =>
    line < len(table.offsets) and compare_at(table.bytes,int(table.offsets[line]),binary(key)^) == 0

fn prefix_at(raw: binary,at: int,key: binary,i: int = 0) bool =>
    if (i == len(key)) true
    else if (raw[at + i] != key[i]) false else prefix_at(raw,at,key,i + 1)

pub fn prefix(table,line: int,key: string) bool^ =>
    line < len(table.offsets) and prefix_at(table.bytes,int(table.offsets[line]),binary(key)^)

pub fn token_end(raw: binary,at: int,end: int,separator: int) int =>
    if (at >= end or raw[at] == separator) at else token_end(raw,at + 1,end,separator)

pub fn line_key(table,line: int) string^ {
    let start = int(table.offsets[line])
    let end = token_end(table.bytes,start,index.line_end(table,line),9);
    utf8.decode(slice(table.bytes,start,end)^)^
}

pub fn posting_start(table,line: int) int =>
    token_end(table.bytes,int(table.offsets[line]),index.line_end(table,line),9) + 1

pub fn posting(table,line: int,at: int,abbreviation: bool) map^ {
    let end = token_end(table.bytes,at,index.line_end(table,line),9)
    let text = utf8.decode(slice(table.bytes,at,end)^)^
    let parts = split(text," ");
    {text:parts[0],reading:if (abbreviation) parts[1] else slice(line_key(table,line)^,2),
        weight:int(parts[len(parts) - 1])^,next:end + 1}
}

pub fn end(table,line: int) int => index.line_end(table,line)
