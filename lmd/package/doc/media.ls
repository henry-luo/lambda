// JSON binary formatting shares the runtime base64 encoder; callers choose a validated media type.
pub fn data_uri(bytes, mime) string^ {
    let encoded = format(bytes,'json')^;
    "data:" ++ mime ++ ";base64," ++ slice(encoded,1,len(encoded)-1)
}
