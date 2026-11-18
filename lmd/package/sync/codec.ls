// versioned typed tuples; map order is data under S2.3.1v2, never JSON numbers.
pub let version = "lambda-crdt-json-1"
let sized = ["i8","u8","i16","u16","i32","u32","i64","u64","f16","f32"]
let casts = {i8:(v) any^ =>i8(v),u8:(v) any^ =>u8(v),i16:(v) any^ =>i16(v),u16:(v) any^ =>u16(v),
    i32:(v) any^ =>i32(v),u32:(v) any^ =>u32(v),i64:(v) any^ =>i64(v),u64:(v) any^ =>u64(v),
    f16:(v) any^ =>f16(v),f32:(v) any^ =>f32(v)}
let scalars = ["null","bool","int","float","integer","decimal","symbol","binary","datetime",*sized]
let direct_tuples = ["string","null","bool","int","u64"]
let mark_free = ["null","bool","u64","binary"]
let exact_values = ["null","bool","string","symbol","binary","int","integer",
    "i8","u8","i16","u16","i32","u32","i64","u64"]

// decode the unsigned decimal wire field directly in its full-width domain.
fn unsigned64(payload) any^ {
    if (not (payload is string) or len(payload) == 0) raise error("sync codec: invalid u64")
    var result = 0u64
    var i = 0
    while (i < len(payload)) {
        let digit = (ord(payload[i])^-48)^
        if (digit < 0 or digit > 9 or result > 1844674407370955161u64 or
            (result == 1844674407370955161u64 and digit > 5)) raise error("sync codec: invalid u64")
        result = result*10u64+u64(digit)
        i = i+1
    }
    if (string(result) != payload) raise error("sync codec: noncanonical u64")
    return result
}

fn scalar_value(tag, value) any^ {
    let restored = if (contains(sized,tag)) casts[tag](value)^ else value
    if (string(type(restored)) != tag) raise error("sync codec: scalar type mismatch")
    return restored
}
fn scalar_values(value, depth=0) any^ {
    if (depth > 128) raise error("sync codec: nesting limit")
    if (value is string) { return [] }
    if (value is array) { return [for (item in value) *scalar_values(item,depth+1)^] }
    // The ordered entry iterator already supplies the stored value.
    if (value is map) { return [for (key,member in value) *[*scalar_values(key,depth+1)^,*scalar_values(member,depth+1)^]] }
    let tag = string(type(value))
    if (not contains(scalars,tag)^) raise error("sync codec: unsupported value type")
    // Binary hex is exact for every byte; numeric poison keeps the lossless check (S4.1.1, S4.2.2).
    return if (contains(mark_free,tag)^ or
        (tag == "int" and value >= -9007199254740991 and value <= 9007199254740991)) [] else [value]
}
fn validate_scalars(value) any^ {
    let originals = scalar_values(value)^
    // exact primitive tuples need no Mark round-trip or retained Input arena.
    if (len(originals)^ == 0) { return true }
    // one parsed document per value avoids one retained Input arena per scalar.
    let decoded = parse(format(originals,'mark')^,'mark')^
    if (not (decoded is array) or len(decoded)^ != len(originals)^)
        raise error("sync codec: scalar is not losslessly portable")
    for (index in 0 to (len(originals)^-1)) {
        if (scalar_value(string(type(originals[index])),decoded[index])^ != originals[index])
            raise error("sync codec: scalar is not losslessly portable")
    }
    return true
}
fn tuple_pair(pair) any^ {
    if (not (pair is array) or len(pair) != 2) raise error("sync codec: invalid map entry")
    return pair
}
fn scalar_tuples(tuple, depth=0) any^ {
    if (depth > 128) raise error("sync codec: nesting limit")
    if (not (tuple is array) or len(tuple)^ != 2 or not (tuple[0] is string))
        raise error("sync codec: invalid typed tuple")
    let tag = tuple[0]
    if (tag == "array" or tag == "map") {
        if (not (tuple[1] is array)) raise error("sync codec: invalid container")
        if (tag == "array") { return [for (item in tuple[1]) *scalar_tuples(item,depth+1)^] }
        return [for (pair in tuple[1], field in tuple_pair(pair)^) *scalar_tuples(field,depth+1)^]
    }
    if (contains(direct_tuples,tag)^) { return [] }
    if (not contains(scalars,tag)^ or not (tuple[1] is string)) raise error("sync codec: unknown type")
    return [tuple]
}
fn decoded_scalars(tuple) any^ {
    let selected = scalar_tuples(tuple)^
    if (len(selected)^ == 0) { return {} }
    let values = parse("[" ++ join([for (item in selected) item[1]],",")^ ++ "]",'mark')^
    if (not (values is array) or len(values)^ != len(selected)^) raise error("sync codec: invalid scalar literal")
    var result = {}
    var index = 0
    while (index < len(selected)^) {
        let parsed = values[index]
        if (not (parsed is array) or len(parsed)^ != 1) raise error("sync codec: invalid scalar literal")
        let item = selected[index]
        result[item[0] ++ ":" ++ item[1]] = scalar_value(item[0],parsed[0])^
        index = index+1
    }
    return result
}

fn pack(value, depth) any^ {
    if (depth > 128) raise error("sync codec: nesting limit")
    if (value is string) { return ["string",value] }
    if (value is array) { return ["array",[for (item in value) pack(item,depth+1)^]] }
    if (value is map) {
        return ["map",[for (key,member in value) [pack(key,depth+1)^,pack(member,depth+1)^]]]
    }
    let tag = string(type(value))
    if (tag == "u64") { return [tag,string(value)] }
    if (not contains(scalars,tag)) raise error("sync codec: unsupported value type")
    let literal = if (tag == "null") "[null]" else if (tag == "bool")
        (if (value) "[true]" else "[false]") else if (tag == "int" or tag == "binary") "[" ++ string(value) ++ "]"
        else format([value],'mark')^
    return [tag,literal]
}

fn unpack(tuple, depth, decoded) any^ {
    if (depth > 128) raise error("sync codec: nesting limit")
    if (not (tuple is array) or len(tuple) != 2 or not (tuple[0] is string))
        raise error("sync codec: invalid typed tuple")
    let tag = tuple[0]
    let payload = tuple[1]
    if (tag == "u64") { return unsigned64(payload)^ }
    if (tag == "string") {
        if (not (payload is string)) raise error("sync codec: invalid string")
        return payload
    }
    if (tag == "array") {
        if (not (payload is array)) raise error("sync codec: invalid array")
        return [for (item in payload) unpack(item,depth+1,decoded)^]
    }
    if (tag == "map") {
        if (not (payload is array)) raise error("sync codec: invalid map")
        var result = {}
        var i = 0
        while (i < len(payload)^) {
            let pair = tuple_pair(payload[i])^
            let key = unpack(pair[0],depth+1,decoded)^
            if (not (key is string or key is symbol) or contains(result,key)^)
                raise error("sync codec: invalid or duplicate map key")
            // The fresh map owns its fields, so adding a key needs no prefix copy.
            result[key] = unpack(pair[1],depth+1,decoded)^
            i = i+1
        }
        return result
    }
    if (not contains(scalars,tag)^ or not (payload is string)) raise error("sync codec: unknown type")
    // Canonical re-encoding below also rejects saturated, padded or non-integer spellings.
    if (tag == "null") {
        if (payload != "[null]") raise error("sync codec: invalid scalar literal")
        return null
    }
    if (tag == "bool") {
        if (payload != "[true]" and payload != "[false]") raise error("sync codec: invalid scalar literal")
        return payload == "[true]"
    }
    if (tag == "int") {
        if (payload == "[inf]") { return inf }
        if (payload == "[-inf]") { return -inf }
        if (len(payload)^ < 3 or not starts_with(payload,"[")^ or not ends_with(payload,"]")^)
            raise error("sync codec: invalid scalar literal")
        return int(slice(payload,1,len(payload)^-1)^)^
    }
    return decoded[tag ++ ":" ++ payload]
}

pub fn encode(value) any^ {
    validate_scalars(value)^
    return format([version,pack(value,0)^],{type:'json',compact:true})^
}

fn preflight(encoded) any^ {
    var depth = 0
    var quoted = false
    var escaped = false
    var i = 0
    while (i < len(encoded)) {
        let char = encoded[i]
        if (quoted) {
            if (escaped) { escaped = false }
            else if (char == 92) { escaped = true }
            else if (char == 34) { quoted = false }
        } else {
            if (char == 34) { quoted = true }
            if (char == 91 or char == 123) { depth = depth+1 }
            if (char == 93 or char == 125) { depth = depth-1 }
            if (depth > 1024 or depth < 0) raise error("sync codec: syntax nesting limit")
        }
        i = i+1
    }
    if (quoted or depth != 0) raise error("sync codec: incomplete syntax")
    return true
}

pub fn decode(encoded, max_bytes=8388608) any^ {
    if (not (encoded is string or encoded is binary)) raise error("sync codec: expected encoded bytes or text")
    let bytes = binary(encoded)^
    if (len(bytes)^ > max_bytes) raise error("sync codec: size limit")
    // The JSON parser already bounds nesting and rejects unbalanced syntax.
    // Keep the codec's diagnostics for inputs rejected by that parser.
    let envelope = parse(encoded,'json') ^ { preflight(bytes)^; raise ^ }
    if (not (envelope is array) or len(envelope) != 2 or envelope[0] != version)
        raise error("sync codec: incompatible encoding")
    let value = unpack(envelope[1],0,decoded_scalars(envelope[1])^)^
    let canonical = encode(value)^
    if ((if (encoded is binary) binary(canonical)^ else canonical) != encoded) raise error("sync codec: noncanonical encoding")
    return value
}

pub fn digest(value) any^ => hash(encode(value)^,'sha256')^

// known is already portable and validated; this compares reuse evidence, not arbitrary validity.
pub fn same_encoding(value, known, depth=0) bool^ {
    if (depth > 128) raise error("sync codec: nesting limit")
    if (value is array) {
        if (not (known is array) or len(value)^ != len(known)^) { return false }
        var i = 0
        while (i < len(value)^) {
            if (not (same_encoding(value[i],known[i],depth+1)^)) { return false }
            i = i+1
        }
        return true
    }
    if (value is map) {
        if (not (known is map) or len(value)^ != len(known)^) { return false }
        let keys = [for (key at value) key]
        let old_keys = [for (key at known) key]
        var i = 0
        while (i < len(keys)^) {
            if (not (same_encoding(keys[i],old_keys[i],depth+1)^) or
                not (same_encoding(value[keys[i]],known[old_keys[i]],depth+1)^)) { return false }
            i = i+1
        }
        return true
    }
    if (known is array or known is map) { return false }
    let tag = string(type(value))
    if (tag != string(type(known))) { return false }
    if (contains(exact_values,tag)^)
        { return value == known }
    return encode(value)^ == encode(known)^
}
