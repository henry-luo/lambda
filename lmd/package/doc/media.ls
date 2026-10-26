// JSON binary formatting shares the runtime base64 encoder; callers choose a validated media type.
pub fn data_uri(bytes, mime) string^ {
    let encoded = format(bytes,'json')^;
    "data:" ++ mime ++ ";base64," ++ slice(encoded,1,len(encoded)-1)
}

// use the Mark binary reader's base64 codec for data URIs produced by data_uri().
pub fn data_uri_bytes(uri) binary^ {
    let comma = index_of(uri,",");
    if (not starts_with(uri,"data:") or comma < 0 or not ends_with(lower(slice(uri,0,comma)),";base64"))
        raise error("Media: expected a base64 data URI")
    else {
        let bytes = parse("b'\\64" ++ slice(uri,comma+1) ++ "'",'mark') ^ {raise error("Media: invalid base64 payload")};
        if (bytes is binary) bytes else raise error("Media: empty binary data URI")
    }
}
