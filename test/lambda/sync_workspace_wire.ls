import c: lambda.sync.core
import w: lambda.sync.workspace
import p: lambda.sync.protocol
import codec: lambda.sync.codec
import store: lambda.sync.store

let empty = w.start("wire-workspace")^
let seed = w.transact(empty,"seed",[{kind:'create',type:'dir',parent:w.root,name:"base"}])^.endpoint
let alice = w.transact(seed,"alice",[
    {kind:'create',type:'file',parent:["seed",1],name:"a",content:w.content("a")^.reference},
    {kind:'move',entry:["seed",1],name:"renamed"}
])^.endpoint
let packet = p.next(alice,w.have(seed)^)^
let admission = {actors:["seed","alice","offline"],checkpoints:[]}
let waiting = p.accept(empty,codec.encode(packet)^,admission)^
let resumed = store.load(store.seal(waiting)^,"fresh")^.endpoint
let complete = p.accept(resumed,codec.encode(p.next(seed,w.have(empty)^)^)^,admission)^
let offline = w.transact(seed,"offline",[{kind:'create',type:'dir',parent:w.root,name:"offline"}])^.endpoint
let compacted = p.compact(alice)^
let chunks = p.frames(compacted.replica,w.model,4096)^
let digest = codec.digest(p.checkpoint(compacted.replica))^
fn recover(endpoint, frames, digest) any^ {
    var result = endpoint
    var i = len(frames)-1
    while (i >= 0) {
        result = p.accept(result,codec.encode(frames[i])^,{actors:["seed","alice","offline"],checkpoints:[digest]})^
        i = i-1
    }
    return result
}
let recovered = recover(offline,chunks,digest)^
let data = w.content("linked")^
let link = w.document(c.snapshot(seed.replica)^)^
let linked = w.transact(seed,"link",[{kind:'create',type:'file',parent:w.root,name:"nested.zip",content:data.reference,document:link}])^.endpoint;
all([
    len(waiting.replica.atoms) == 0,len(waiting.transactions) == 1,
    not p.fresh_writer(waiting,"alice")^,p.fresh_writer(waiting,"fresh")^,
    c.compact(complete.replica) == c.compact(alice.replica),
    p.next(compacted,w.have(offline)^)^.kind == 'checkpoint_required',
    c.compact(recovered.replica) == c.compact(c.merge(alice.replica,offline.replica)^),
    len(recovered.transactions) == 0,
    len(w.read(recovered.replica)^.entries) == 3,
    not p.status(recovered).cloud,
    p.durable(linked,'cloud',linked.replica.clock,[data.reference.manifest]) ^ {
        ^.message == "sync protocol: referenced blobs are not durable" },
    p.status(p.durable(linked,'cloud',linked.replica.clock,[data.reference.manifest,link.checkpoint])^).cloud,
    p.start(seed.replica) ^ { ^.message == "sync protocol: workspace schema mismatch" },
    c.validate(c.restore(c.snapshot(recovered.replica)^)^)^
])
