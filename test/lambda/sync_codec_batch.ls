import codec: lambda.sync.codec
import hex: lambda.security.tuf.metadata
let values = [for (i in 0 to 127) {id:i,kind:'chunk',bytes:binary([i])^,none:null,flag:i%2 == 0}]
let encoded = codec.encode(values)^
let one = codec.encode([1,2])^
let injected = replace(one,"[1]","[1],[2]")^
let wrong_type = replace(one,"[1]","[[1]]")^;
[hex.hex(hash(encoded,'sha256')^)^ == "6b393b886fb3d8807f2b223ae4e12c5991242d79b051cdf1fd918ac6accc479f",
 codec.decode(encoded)^ == values,
 codec.decode(injected) ^ { ^ is error },codec.decode(wrong_type) ^ { ^ is error },
 codec.decode(codec.encode([])^)^ == [],codec.decode(codec.encode({})^)^ == {},
 codec.encode(symbol("href","namespace")) ^ { ^.message == "sync codec: scalar is not losslessly portable" }]
