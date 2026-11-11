import w: lambda.sync.workspace
import c: lambda.sync.core
import p: lambda.sync.protocol
import codec: lambda.sync.codec
import store: lambda.sync.store

let data = w.content("one")^
let base = w.start("workspace")^
let edited = w.transact(base,"alice",[
    {kind:'create',type:'dir',parent:w.root,name:"docs",as:"docs"},
    {kind:'create',type:'file',parent:"docs",name:"a.txt",content:data.reference},
    {kind:'create',type:'file',parent:"docs",name:"empty",content:w.content(null)^.reference}
])^
let packet = p.next(edited.endpoint,w.have(base)^)^
let received = p.accept(base,codec.encode(packet)^,{actors:["alice"],checkpoints:[]})^
let restored = store.load(store.seal(received)^,"fresh")^
let moved = w.transact(edited.endpoint,"alice",[{kind:'move',entry:["alice",2],parent:w.root,name:"renamed.txt"}])^.endpoint
let published = w.transact(moved,"alice",[{kind:'publish',entry:["alice",2],content:w.content("two")^.reference}])^.endpoint;
all([
    packet.kind == 'transaction',len(packet.body.operations) == 3,
    c.compact(received.replica) == c.compact(edited.endpoint.replica),
    w.path(received.replica,["alice",2])^ == "docs/a.txt",
    len(w.created(edited)^) == 3,
    c.compact(restored.endpoint.replica) == c.compact(received.replica),
    p.accept(received,codec.encode(packet)^,{actors:["alice"],checkpoints:[]})^ == received,
    w.path(published.replica,["alice",2])^ == "renamed.txt",
    len(w.entry(published.replica,["alice",2])^.heads)^ == 1,
    len(w.roots(published.replica)^.revisions)^ == 3,
    w.payload(data.reference,[data.object])^ == binary("one")^,
    w.transact(base,"bad",[{kind:'create',type:'dir',parent:w.root,name:"ok"},
        {kind:'move',entry:w.root,name:"bad"}]) ^ { ^.message != "" },
    len(base.replica.atoms) == 0,
    store.load(store.seal(received)^,"alice") ^ { ^.message == "sync store: restoration requires a fresh writer actor" }
])
