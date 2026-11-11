import c: lambda.sync.core
import t: lambda.sync.text
import p: lambda.sync.protocol
import k: lambda.sync.codec

fn checkpoint_merge(endpoint, source) any^ {
    let digest = k.digest(p.checkpoint(source.replica))^
    let chunks = p.frames(source.replica,p.default_schema,4096,digest)^
    var result = endpoint
    var i = len(chunks)-1
    while (i >= 0) {
        // this example models the host's already authenticated checkpoint admission
        result = p.accept(result,k.encode(chunks[i])^,{actors:["alice","bob"],checkpoints:[digest]})^
        i = i-1
    }
    return result
}

let base = t.from_text("example","seed","Hello world")^
let alice = p.local_change(p.start(base)^,"alice",[{kind:'text',start:5,count:0,value:"!"}])^.endpoint
let bob = p.local_change(p.start(base)^,"bob",[{kind:'text',start:11,count:0,value:"?"}])^.endpoint
let direct = p.next(alice,p.have(bob.replica)^)^
let together = p.accept(bob,k.encode(direct)^,{actors:["alice"],checkpoints:[]})^
let offline = p.local_change(p.start(base)^,"offline",[{kind:'text',start:0,count:0,value:"Offline: "}])^.endpoint
let compacted = p.compact(together)^
let recovered = checkpoint_merge(offline,compacted)^;
["Direct: " ++ t.read(together.replica)^,
 "Recovery: " ++ string(p.next(compacted,p.have(offline.replica)^)^.kind),
 "Offline merged: " ++ t.read(recovered.replica)^,
 "Cloud acknowledged: " ++ string(p.status(recovered).cloud)]
