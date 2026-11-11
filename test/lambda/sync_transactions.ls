import c: lambda.sync.core
import t: lambda.sync.text
import x: lambda.sync.change
import p: lambda.sync.protocol
import k: lambda.sync.codec

let initial = t.empty("transactions")
let base = t.from_text("transactions","seed","ab")^
let tx = x.transaction(base,"alice",[{kind:'text',start:1,count:0,value:"XY"}])^
let packet = p.transaction(base,tx.replica)^
let admitted = {actors:["seed","alice"],checkpoints:[]}
let waiting = p.accept(p.start(initial)^,k.encode(packet)^,admitted)^
let complete = p.accept(waiting,k.encode(p.reply(base,p.have(initial)^)^)^,admitted)^
let duplicate = p.accept(complete,k.encode(packet)^,admitted)^
let restored = k.decode(k.encode(waiting)^)^
let recovered = p.accept(restored,k.encode(p.reply(base,p.have(initial)^)^)^,admitted)^;
all([t.read(waiting.replica)^ == "",len(waiting.transactions) == 1,
 len(waiting.replica.pending) == 0,
 t.read(complete.replica)^ == "aXYb",len(complete.transactions) == 0,
 duplicate == complete,c.compact(recovered.replica) == c.compact(complete.replica),
 p.transaction(base,c.compact(tx.replica)) ^ { ^.message == "sync change: transaction packets unavailable or endpoints differ" }])
