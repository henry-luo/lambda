import k: lambda.sync.codec
let values = [null,true,42,-7,3.14,900719925474099312345n,1.2300m,'name',
    b'\x00FF',t'2026-11-11',i8(-12),u16(65535),i64("9223372036854775807"),18446744073709551615u64,f16(1.5),f32(0.125)]
let original = {z:values,a:["🦀é\n",{nested:"value"}]}
let encoded = k.encode(original)^
let decoded = k.decode(encoded)^
let types = [for (value in decoded.z) string(type(value))];
all([decoded == original,
 types == [for (value in values) string(type(value))],
 [for (key at decoded) key] == ['z','a'],
 k.encode(decoded)^ == encoded,
 k.digest({x:1,y:2})^ != k.digest({y:2,x:1})^,
 k.decode(encoded,1) ^ { ^.message == "sync codec: size limit" },
 k.decode("[]") ^ { ^.message == "sync codec: incompatible encoding" },
 k.decode(" " ++ encoded) ^ { ^.message == "sync codec: noncanonical encoding" },
 k.encode((x)=>x) ^ { ^.message == "sync codec: unsupported value type" }])
