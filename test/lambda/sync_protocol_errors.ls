import c: lambda.sync.core
import t: lambda.sync.text
import p: lambda.sync.protocol
import k: lambda.sync.codec
import store: lambda.sync.store

fn fails(result:any) => result is error
let initial = p.start(t.empty("wire-errors"))^
let changed = p.local_change(initial,"a",[{kind:'text',start:0,count:0,value:"hello"}])^.endpoint
let packet = p.next(changed,p.have(initial.replica)^,128,8192)^
let cp = p.checkpoint(changed.replica)
let digest = k.digest(cp)^
let frames = p.frames(cp,p.default_schema,2048,digest)^
let admitted = {actors:["a"],checkpoints:[digest]}
let partial = p.accept(initial,k.encode(frames[0])^,admitted)^
let conflicting_frame = {*:frames[0],body:{*:frames[0].body,text:"different"}}
let wrong_total = {*:frames[0],body:{*:frames[0].body,total:frames[0].body.total+1}}
let bad_tx = {*:packet,body:{*:packet.body,frontier:[]}}
let invalid = {*:packet,version:2}
let bad_context = {*:packet.body.operations[0],context:[{actor:"unseen",counter:1}]}
let pending = c.receive(initial.replica,[bad_context])^
let saved = c.snapshot(changed.replica)^;
all([fails(p.accept(initial,k.encode(invalid)^,admitted)),
 fails(p.accept(initial,k.encode(bad_tx)^,admitted)),
 fails(p.accept(partial,k.encode(conflicting_frame)^,admitted)),
 fails(p.accept(partial,k.encode(wrong_total)^,admitted)),
 fails(p.accept(partial,k.encode(frames[0])^,admitted,1)),
 fails(p.acknowledge(initial,"peer",changed.replica.clock)),
 fails(p.record_durable(initial,'cloud',saved)),
 fails(p.frames(t.insert(changed.replica,"a",5,"!")^,p.default_schema,2048,digest)),
 fails(p.reply(changed.replica,p.have(initial.replica)^,p.default_schema,1025)),
 fails(store.load(store.seal(changed)^,"a")),
 c.validate(pending)^,
 len(p.cancel_transfer(partial,digest).transfers) == 0])
