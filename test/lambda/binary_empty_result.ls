// S2.2.2v2: only nonempty byte payloads become Binary values.
let bytes = b'\x007f';
[binary("") is null, binary(symbol.empty) is null, binary([]) is null,
 slice(bytes, 1, 1) is null, take(bytes, 0) is null, drop(bytes, 2) is null,
 (bytes that false) is null, len("") == 0, "" is string,
 binary([0]) == b'\x00', slice(bytes, 1, 2) == b'\x7f']
