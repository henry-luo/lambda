import chunks: lambda.sync.chunks
let config = {*:chunks.default_profile,minimum:16,target:32,maximum:64}
let bytes = binary([for (index in 0 to 1023) index%251])^
let whole = chunks.split(bytes,config)^
let a = chunks.feed(chunks.start(config)^,slice(bytes,0,17)^)^
let b = chunks.feed(a.state,slice(bytes,17,198)^)^
let c = chunks.feed(b.state,slice(bytes,198,512)^)^
let d = chunks.feed(c.state,slice(bytes,512)^,true)^
let pieces = [*a.pieces,*b.pieces,*c.pieces,*d.pieces]
let empty = chunks.feed(chunks.start(config)^,null,true)^;
[pieces == whole,binary([for (piece in pieces) for (byte in piece) byte])^ == bytes,d.state.tail == null,d.state.fingerprint == 0,
 empty.pieces == [],chunks.split(null,config)^ == [],
 [for (piece in whole) len(piece)^] == [28, 64, 64, 30, 33, 60, 64, 64, 30, 33, 60, 64, 64, 30, 33, 60, 64, 64, 30, 33, 52]]
