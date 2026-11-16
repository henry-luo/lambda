import codec: lambda.sync.codec
let values = [null,true,false,"",0,1,-1,inf,-inf,1.0,0.0,-0.0,1n,1.0m,1.00m,'name',"name",
    b'00FF',binary([])^,t'2026-11-16',i8(-1),u8(1),i16(1),u16(1),i32(1),u32(1),
    i64("9223372036854775807"),18446744073709551615u64,f16(1),f32(1),
    [],[1],[1.0],{x:1,y:2},{y:2,x:1},{x:1.0,y:2},{["x"]:1,y:2},
    {a:[null,{x:1}],b:b'FF'},{a:[null,{x:1.0}],b:b'FF'}]
let encoded = [for (value in values) codec.encode(value)^]
// Portable equality keeps numeric kinds, ordered keys and scalar spellings.
all([for (i in 0 to (len(values)^-1)) for (j in 0 to (len(values)^-1))
    codec.same_encoding(values[i],values[j])^ == (encoded[i] == encoded[j])])
