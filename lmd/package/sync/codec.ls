// versioned typed tuples; map order is data under S2.3.1v2, never JSON numbers.
pub let version = "lambda-crdt-json-1"
let sized = ["i8","u8","i16","u16","i32","u32","i64","u64","f16","f32"]
let casts = {i8:(v) any^ =>i8(v),u8:(v) any^ =>u8(v),i16:(v) any^ =>i16(v),u16:(v) any^ =>u16(v),
    i32:(v) any^ =>i32(v),u32:(v) any^ =>u32(v),i64:(v) any^ =>i64(v),u64:(v) any^ =>u64(v),
    f16:(v) any^ =>f16(v),f32:(v) any^ =>f32(v)}
let scalars = ["null","bool","int","float","integer","decimal","symbol","binary","datetime",*sized]

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
    if (value is map) { return [for (key at value) *[*scalar_values(key,depth+1)^,*scalar_values(value[key],depth+1)^]] }
    let tag = string(type(value))
    if (not contains(scalars,tag)^) raise error("sync codec: unsupported value type")
    return if (tag == "u64") [] else [value]
}
fn validate_scalars(value) any^ {
    let originals = scalar_values(value)^
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
fn scalar_tuples(tuple, depth=0) any^ {
    if (depth > 128) raise error("sync codec: nesting limit")
    if (not (tuple is array) or len(tuple)^ != 2 or not (tuple[0] is string))
        raise error("sync codec: invalid typed tuple")
    let tag = tuple[0]
    if (tag == "array" or tag == "map") {
        if (not (tuple[1] is array)) raise error("sync codec: invalid container")
        if (tag == "array") { return [for (item in tuple[1]) *scalar_tuples(item,depth+1)^] }
        var result = []
        var index = 0
        while (index < len(tuple[1])^) {
            let pair = tuple[1][index]
            if (not (pair is array) or len(pair)^ != 2) raise error("sync codec: invalid map entry")
            result = [*result,*scalar_tuples(pair[0],depth+1)^,*scalar_tuples(pair[1],depth+1)^]
            index = index+1
        }
        return result
    }
    if (tag == "string" or tag == "u64") { return [] }
    if (not contains(scalars,tag)^ or not (tuple[1] is string)) raise error("sync codec: unknown type")
    return [tuple]
}
fn decoded_scalars(tuple) any^ {
    let selected = scalar_tuples(tuple)^
    let values = parse("[" ++ join([for (item in selected) item[1]],",")^ ++ "]",'mark')^
    if (not (values is array) or len(values)^ != len(selected)^) raise error("sync codec: invalid scalar literal")
    var result = {}
    var index = 0
    while (index < len(selected)^) {
        let parsed = values[index]
        if (not (parsed is array) or len(parsed)^ != 1) raise error("sync codec: invalid scalar literal")
        let item = selected[index]
        result = {*:result,[item[0] ++ ":" ++ item[1]]:scalar_value(item[0],parsed[0])^}
        index = index+1
    }
    return result
}

fn pack(value, depth) any^ {
    if (depth > 128) raise error("sync codec: nesting limit")
    if (value is string) { return ["string",value] }
    if (value is array) { return ["array",[for (item in value) pack(item,depth+1)^]] }
    if (value is map) {
        return ["map",[for (key at value) [pack(key,depth+1)^,pack(value[key],depth+1)^]]]
    }
    let tag = string(type(value))
    if (tag == "u64") { return [tag,string(value)] }
    if (not contains(scalars,tag)) raise error("sync codec: unsupported value type")
    let literal = format([value],'mark')^
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
            let pair = payload[i]
            if (not (pair is array) or len(pair)^ != 2) raise error("sync codec: invalid map entry")
            let key = unpack(pair[0],depth+1,decoded)^
            if (not (key is string or key is symbol) or contains(result,key)^)
                raise error("sync codec: invalid or duplicate map key")
            result = {*:result,[key]:unpack(pair[1],depth+1,decoded)^}
            i = i+1
        }
        return result
    }
    if (not contains(scalars,tag)^ or not (payload is string)) raise error("sync codec: unknown type")
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
    // syntax markers are ASCII bytes; repeated Unicode scalar indexing rescans prefixes.
    preflight(bytes)^
    let envelope = parse(encoded,'json')^
    if (not (envelope is array) or len(envelope) != 2 or envelope[0] != version)
        raise error("sync codec: incompatible encoding")
    let value = unpack(envelope[1],0,decoded_scalars(envelope[1])^)^
    let canonical = encode(value)^
    if ((if (encoded is binary) binary(canonical)^ else canonical) != encoded) raise error("sync codec: noncanonical encoding")
    return value
}

pub fn digest(value) any^ => hash(encode(value)^,'sha256')^
