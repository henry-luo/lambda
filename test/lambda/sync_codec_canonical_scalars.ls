import codec: lambda.sync.codec
let values = [null,true,false,0,1,-1,9007199254740991,-9007199254740991,inf,-inf,
    1.5,i8(-128),u16(65535),i64("9223372036854775807"),18446744073709551615u64,
    f16(0.5),f32(1.25),1.2300m,'name',b'00FF',t'2026-11-14']
// Legacy wire images use the native Mark formatter for each typed scalar.
let compatible = all([for (value in values where string(type(value)) != "u64") {
    let legacy = format([codec.version,[string(type(value)),format([value],'mark')^]],{type:'json',compact:true})^
    codec.encode(value)^ == legacy and codec.encode(codec.decode(legacy)^)^ == legacy
}])
let invalid = [for (tuple in [["null","[false]"],["bool","[1]"],["int","[1.0]"],
    ["int","[01]"],["int","[-0]"],["int","[9007199254740992]"],
    ["int","[1,2]"],["int","1"],["int",1],["int","[nan]"]])
    codec.decode(format([codec.version,tuple],{type:'json',compact:true})^) ^ { ^ }]
let nested = {first:[null,false,0,-1],second:{maximum:9007199254740991,minimum:-9007199254740991}}
all([compatible,all([for (value in invalid) value is error]),codec.encode(nan) ^ { ^ is error },
    codec.encode(codec.decode(codec.encode(nested)^)^)^ == codec.encode(nested)^])
