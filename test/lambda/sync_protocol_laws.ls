import c: lambda.sync.core
import t: lambda.sync.text
import p: lambda.sync.protocol
import k: lambda.sync.codec
import store: lambda.sync.store

fn permutations(items) any^ => if (len(items) == 0) [[]] else
    [for (i in 0 to (len(items)-1)) for (tail in permutations([for (j,item in items where j != i) item])^) [items[i],*tail]]

fn deliver(initial, packets) any^ {
    var endpoint = initial
    var i = 0
    while (i < len(packets)) {
        endpoint = p.accept(endpoint,k.encode(packets[i])^,{actors:["seed","a","b"],checkpoints:[]})^
        i = i+1
    }
    return endpoint
}

let seed = p.start(t.empty("network-laws"))^
let seeded = p.local_change(seed,"seed",[{kind:'text',start:0,count:0,value:"ab"}])^.endpoint
let a = p.local_change(seeded,"a",[{kind:'text',start:1,count:0,value:"AX"}])^.endpoint
let b = p.local_change(seeded,"b",[{kind:'text',start:2,count:0,value:"B"}])^.endpoint
let seed_packet = p.next(seeded,p.have(seed.replica)^,128,8192)^
let a_packet = p.next(a,p.have(seeded.replica)^,128,8192)^
let b_packet = p.next(b,p.have(seeded.replica)^,128,8192)^
let outcomes = [for (schedule in permutations([seed_packet,a_packet,b_packet])^)
    deliver(seed,[*schedule,*reverse(schedule)])^]
let expected = c.compact(c.merge(a.replica,b.replica)^)
let partial = deliver(seed,[a_packet])^
let persisted = store.load(store.seal(partial)^,"restart")^.endpoint
let resumed = deliver(persisted,[b_packet,seed_packet])^
let ack = p.ack(resumed)^
let sender_ack = p.accept_ack(p.replace_replica(a,b.replica)^,k.encode(ack)^,"receiver")^;
all([seed_packet.kind == 'transaction',a_packet.kind == 'transaction',b_packet.kind == 'transaction',
 all([for (result in outcomes) c.compact(result.replica) == expected and len(result.transactions) == 0]),
 t.read(partial.replica)^ == "",len(partial.transactions) == 1,
 c.compact(resumed.replica) == expected,p.status(sender_ack).peer,
 not p.status(sender_ack).cloud,
 p.next(resumed,p.have(seeded.replica)^,128,8192)^.kind == 'transaction',
 len(p.compact(resumed).journal) == 0,
 p.next(p.compact(resumed),p.have(seeded.replica)^)^.kind == 'checkpoint_required'])
