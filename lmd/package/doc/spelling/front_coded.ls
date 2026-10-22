// adopted resource §4.3; prefix counts are code points (S2.5.8), seeks use bytes (S17.4.1).
import search: lambda.doc.diff.common
import utf8: lambda.io.utf8

fn failure(message) error => error({code:7821,message:"spelling front coding: " ++ message})
fn require(ok,message) bool^ => if (ok) true else raise failure(message)

fn token_end(raw: binary, at: int, limit: int) int =>
    if (at >= limit or raw[at] == 59 or raw[at] == 124) at
    else token_end(raw,at + 1,limit)

fn token_text(raw: binary, at: int, end: int) string^ {
    let value = utf8.decode(slice(raw,at,end)^) ^ { raise failure("invalid UTF-8") }
    let valid = require(len(value) > 0,"empty token")^;
    value
}

// validation and lookup share the same decoder, retaining only the current word.
fn next_word(raw: binary, at: int, end: int, previous: string, delta: bool) string^ {
    let token = token_text(raw,at,end)^
    let comma = index_of(token,",");
    if (not delta) {
        let valid = require(comma == null,"comma in a complete word")^;
        token
    } else if (comma == null) previous ++ token
    else {
        let digits = slice(token,0,comma)
        let valid = require(len(digits) > 0 and digits[0] != "0" and
            all([for (b in binary(digits)) b >= 48 and b <= 57]),"invalid prefix length")^
        let keep = int(digits) ^ { raise failure("invalid prefix length") }
        let bounded = require(keep <= len(previous) and comma + 1 < len(token),"prefix or suffix out of bounds")^;
        slice(previous,0,keep) ++ slice(token,comma + 1)
    }
}

fn inspect_line(raw: binary, at: int, limit: int, previous: string = "",
    delta: bool = false, count: int = 0, first = null, oracle = null, base: int = 0) map^ {
    let end = token_end(raw,at,limit)
    let word = next_word(raw,at,end,previous,delta)^
    let valid = require(count < 64 and normalize(word,"nfc") == word and
        not contains(word,",") and (count == 0 or previous < word) and
        (oracle == null or oracle[base + count] == word),
        "too many, non-NFC, unsorted or reference-mismatching words")^
    let initial = if (count == 0) word else first;
    if (end == limit) {count:count + 1,first:initial,last:word}
    else inspect_line(raw,end + 1,limit,word,raw[end] == 59,count + 1,initial,oracle,base)^
}

fn line_end(vocabulary, line: int) int =>
    if (line + 1 < len(vocabulary.offsets)) int(vocabulary.offsets[line + 1]) - 1
    else len(vocabulary.bytes) - 1

fn line_bytes(vocabulary, line: int) binary^ =>
    slice(vocabulary.bytes,int(vocabulary.offsets[line]),line_end(vocabulary,line))^

// compare restart keys in place, without allocating strings or scanning UTF-8 prefixes.
fn first_compare(raw: binary, at: int, key: binary, i: int = 0) int {
    let b = raw[at + i]
    let end = b == 59 or b == 124 or b == 10;
    if (i == len(key)) (if (end) 0 else 1)
    else if (end or b < key[i]) -1
    else if (b > key[i]) 1
    else first_compare(raw,at,key,i + 1)
}

pub fn from_bytes(source, expected_count = null) map^ {
    let valid = require(source is binary and len(source) > 0 and len(source) <= 4294967295,
        "expected nonempty bytes within the u32 offset range")^
    let terminated = require(source[len(source) - 1] == 10 and not (0 in source) and not (13 in source) and
        slice(source,0,3) != b'\xEFBBBF',
        "missing final LF or reserved control byte")^
    let offsets: u32[] = [0u32,*[for (i,b in source where b == 10 and i + 1 < len(source)) u32(i + 1)]]
    let indexed = {bytes:source,offsets:offsets}
    // load-time validation streams each block; decoded words are never kept in the checker.
    let summaries = [for (i,at in offsets) {
        let raw = line_bytes(indexed,i)^;
        inspect_line(raw,0,len(raw))^
    }]
    let ordered = require(all([for (i,row in summaries)
        (i == len(summaries) - 1 or row.count == 64) and
        (i == 0 or summaries[i - 1].last < row.first)]),"invalid line count or block order")^
    let count = (len(offsets) - 1) * 64 + summaries[len(summaries) - 1].count
    let counted = require(expected_count == null or expected_count == count,"entry count mismatch")^;
    {format_version:1,bytes:source,offsets:offsets,count:count}
}

fn find_in_line(raw: binary, at: int, limit: int, key: string, previous: string,
    delta: bool, id: int) int?^ {
    let end = token_end(raw,at,limit)
    let word = next_word(raw,at,end,previous,delta)^;
    if (word == key) id
    else if (word > key or end == limit) null
    else find_in_line(raw,end + 1,limit,key,word,raw[end] == 59,id + 1)^
}

// exact NFC membership; case admission and suggestions remain separate package work.
pub fn lookup(vocabulary: map, word: string) int?^ {
    // construction validates the index once; a structural array contract here would revisit it per word.
    let raw: binary = vocabulary.bytes
    let key = normalize(word,"nfc")
    let bytes = binary(key)^
    let at = search.lower_bound(vocabulary.offsets,0,0,null,
        (offset: int) => first_compare(raw,offset,bytes));
    if (at < len(vocabulary.offsets) and first_compare(raw,int(vocabulary.offsets[at]),bytes) == 0) at * 64
    else if (at == 0) null
    else {
        let line = at - 1
        let raw = line_bytes(vocabulary,line)^;
        find_in_line(raw,0,len(raw),key,"",false,line * 64)^
    }
}

pub pn read(path, expected_count = null) map^ {
    let bytes = input(path,"binary")^;
    return from_bytes(bytes,expected_count)^
}

// Independent source words are a qualification oracle, never part of a loaded checker.
pub fn verify(vocabulary: map, words: array) bool^ {
    let valid = require(len(words) == vocabulary.count,"reference count mismatch")^
    let checked = [for (i,offset in vocabulary.offsets) {
        let raw = line_bytes(vocabulary,i)^;
        inspect_line(raw,0,len(raw),"",false,0,null,words,i * 64)^.count
    }];
    sum(checked) == len(words)
}
