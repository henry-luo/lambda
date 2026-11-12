import disk: lambda.io.disk
import crypto: lambda.io.crypto

pn produce(acc) any^ {
    if (acc.next == len(acc.parts)^) { return {bytes:null,state:acc,done:true} }
    return {bytes:acc.parts[acc.next],state:{*:acc,next:acc.next+1},done:false}
}
pn main() {
    let area = "temp/sync-disk-stream-" ++ string(sys.proc.self.pid#)
    io.mkdir(area)^
    disk.install(area,"source",binary("abcdefg")^,null)^
    let initial = disk.inspect(area,"source")^
    let captured = disk.read_stream(area,"source",{state:{bytes:binary([])^,calls:0},block_bytes:3,token:initial.token},
        pn (acc,bytes)=> { return {bytes:acc.bytes ++ bytes,calls:acc.calls+1} })^
    let parts = {parts:[binary("abc")^,binary("defg")^],next:0}
    let written = disk.write_stream(area,"copy",{state:parts,expected:null,size:7,hash:captured.hash},produce)^
    let digested = crypto.hash_stream({state:parts,size:7},produce)^
    let bad = disk.write_stream(area,"bad",{state:parts,size:7,hash:hash("wrong",'sha256')^},produce) ^ { ^ }
    let short = disk.write_stream(area,"short",{state:parts,size:8,hash:captured.hash},produce) ^ { ^ }
    let over = disk.write_stream(area,"over",{state:parts,size:6,hash:captured.hash},produce) ^ { ^ }
    let empty = disk.write_stream(area,"empty",{state:{parts:[],next:0},size:0,hash:hash(null,'sha256')^},produce)^
    let changed_source = disk.read_stream(area,"source",{state:0,block_bytes:3,token:initial.token},
        pn (acc,bytes)=> { disk.install(area,"source",binary("external")^,initial.token)^; return acc+len(bytes)^ }) ^ { ^ }
    let destination = disk.inspect(area,"copy")^
    let changed_destination = disk.write_stream(area,"copy",{state:parts,size:7,hash:captured.hash,expected:destination.token},
        pn (acc)=> {
            if (acc.next == 0) { disk.install(area,"copy",binary("external")^,destination.token)^ }
            return produce(acc)^
        }) ^ { ^ }
    let copy = disk.capture(area,"copy",32)^
    let clean = len([for (entry in disk.entries(area,"",32)^) entry.name])^ == 3
    io.delete(area)^
    return [captured.state.bytes == binary("abcdefg")^,captured.state.calls == 3,
        captured.source.token == initial.token,captured.hash == hash("abcdefg",'sha256')^,
        written.state.next == 2,written.hash == captured.hash,bad is error,short is error,over is error,
        empty.hash == hash(null,'sha256')^,changed_source is error,changed_destination is error,
        copy.bytes == binary("external")^,clean,digested.hash == captured.hash,digested.state.next == 2]
}
