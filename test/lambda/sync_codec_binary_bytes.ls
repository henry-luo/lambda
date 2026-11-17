import codec: lambda.sync.codec
let all_bytes = binary([for (i in 0 to 255) i])^
let values = [binary([])^,b'00',b'000000FF',b'0001027F80FEFF',all_bytes,
    binary([for (i in 0 to 1048575) i%256])^]
let invalid = ["[b'0']","[b'GG']","[b'00',b'01']","[b'00'] trailing"]
let portable = all([all([for (value in values) {
    let encoded = codec.encode(value)^
    let decoded = codec.decode(encoded)^
    decoded == value and codec.encode(decoded)^ == encoded
}]), all([for (literal in invalid) {
    let wire = format([codec.version,["binary",literal]],{type:'json',compact:true})^
    codec.decode(wire) ^ { ^ is error }
}])])

let overhead = len(codec.encode(b'00')^)^-2
let maximum = binary(fill((8388608-overhead) div 2,0))^
let encoded_maximum = codec.encode(maximum)^
let too_large = codec.decode(codec.encode(maximum ++ b'00')^) ^ { ^ }
portable and len(encoded_maximum)^ <= 8388608 and codec.decode(encoded_maximum)^ == maximum and too_large is error
