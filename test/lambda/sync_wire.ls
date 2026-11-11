import c: lambda.sync.core
import t: lambda.sync.text
import p: lambda.sync.protocol
import k: lambda.sync.codec

fn exchange(sender, endpoint, size) any^ {
    var result = endpoint
    var more = true
    var rounds = 0
    while (more and rounds < 20) {
        let packet = p.reply(sender,p.have(result.replica)^,p.default_schema,size,4096)^
        result = p.accept(result,k.encode(packet)^,{actors:["seed","a","b"],checkpoints:[]})^
        more = packet.body.more
        rounds = rounds+1
    }
    if (more) raise error("test exchange did not finish")
    return result
}

let base = t.from_text("wire","seed","ab")^
let a = t.insert(base,"a",1,"A!")^
let b = t.insert(base,"b",2,"B")^
let endpoint = p.start(b)^
let synced = exchange(a,endpoint,1)^
let checkpoint = c.compact(a)
let required = p.reply(checkpoint,p.have(b)^)^
let frames = p.frames(checkpoint,p.default_schema,2048)^
let admitted = {actors:["seed","a","b"],checkpoints:[required.body.checkpoint]}
fn receive_frames(endpoint, frames, admitted) any^ {
    var result = endpoint
    var i = len(frames)-1
    while (i >= 0) {
        result = p.accept(result,k.encode(frames[i])^,admitted)^
        i = i-1
    }
    return result
}
let recovered = receive_frames(endpoint,frames,admitted)^
let acknowledged = p.acknowledge(synced,"peer-b",synced.replica.clock)^
let local = p.durable(acknowledged,'local',synced.replica.clock)^
let cloud = p.durable(local,'cloud',synced.replica.clock)^;
all([t.read(synced.replica)^ == "aA!bB",
 c.compact(synced.replica) == c.compact(c.merge(a,b)^),
 required.kind == 'checkpoint_required',len(frames)>1,
 all([for (frame in frames) len(binary(k.encode(frame)^))^ <= 2048]),
 c.compact(recovered.replica) == c.compact(synced.replica),
 len(recovered.transfers) == 0,
 p.accept(recovered,k.encode(frames[0])^,admitted)^.replica == recovered.replica,
 p.status(endpoint) == {local:false,peer:false,cloud:false,pending:0},
 p.status(local) == {local:true,peer:true,cloud:false,pending:0},
 p.status(cloud).cloud,
 p.accept(endpoint,k.encode(frames[0])^,{actors:[],checkpoints:[]}) ^ { ^.message == "sync protocol: checkpoint is not admitted" },
 p.accept(endpoint,k.encode(p.reply(a,p.have(b)^)^)^,{actors:[],checkpoints:[]}) ^ { ^.message == "sync protocol: actor is not admitted" },
 p.reply(a,p.have(b,["different",1])^) ^ { ^.message == "sync protocol: incompatible version, document, profile, schema, root or epoch" }])
