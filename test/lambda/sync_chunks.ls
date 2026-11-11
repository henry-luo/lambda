import chunks: lambda.sync.chunks

let config = {algorithm:"lambda-gear32-1",minimum:16,target:64,maximum:256}
let bytes = binary([for (i in 0 to 2047) hash(string(i),'sha256')^[0]])^
let original = chunks.split(bytes,config)^
let shifted = chunks.split(binary([1,2,3,4,5,6,7])^ ++ bytes,config)^
fn assemble(parts) any^ {
    var result = null
    var i = 0
    while (i < len(parts)) { result = result ++ parts[i]; i = i+1 }
    return result
};
[assemble(original)^ == bytes,original == chunks.split(bytes,config)^,
 all([for (piece in original) len(piece) <= config.maximum]),
 all([for (piece in take(original,len(original)-1)^) len(piece) >= config.minimum]),
 len([for (piece in shifted where contains(original,piece)^) piece]) > len(original)/2,
 chunks.split(null,config)^ == [],chunks.split(binary([0,255])^,config)^ == [binary([0,255])^],
 chunks.profile({*:config,target:63}) ^ { true }]
