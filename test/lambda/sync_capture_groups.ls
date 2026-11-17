import cap: lambda.sync.workspace_capture
import w: lambda.sync.workspace
import p: lambda.sync.protocol
import c: lambda.sync.core
import codec: lambda.sync.codec
import wire: lambda.sync.object_wire

fn receive(sender, endpoint) any^ {
    var remote = endpoint
    var bounded = true
    var groups = 0
    while (not c.dominates(remote.replica.clock,sender.replica.clock)^) {
        let packet = p.next(sender,w.have(remote)^,128,8192)^
        let encoded = codec.encode(packet)^
        bounded = bounded and len(binary(encoded)^)^ <= 8192 and len(packet.body.operations)^ <= 8
        remote = p.accept(remote,encoded,{actors:["writer"],checkpoints:[]})^
        groups = groups+1
    }
    return {replica:remote.replica,bounded:bounded,groups:groups}
}
let initial = w.start("capture-groups")^
let content = w.content("shared content")^.reference
let commands = [for (i in 1 to 24) {kind:'create',type:'file',parent:w.root,
    name:"λ-" ++ string(i) ++ join([for (j in 1 to (if (i == 13) 1500 else 20)) "x"],""),content:content}]
let expected = w.transact(initial,"writer",commands,false)^.endpoint
let grouped = cap.apply_groups(initial,"writer",commands,8,8192)^
let remote = receive(grouped,initial)^
let oversized = [{kind:'create',type:'dir',parent:w.root,name:join([for (i in 1 to 10000) "x"],"")}]
let pieces = [binary([0,255])^,null,binary("λ")^,binary([1])^,binary([2,3,4])^];
all([c.compact(grouped.replica) == c.compact(expected.replica),
    c.compact(remote.replica) == c.compact(expected.replica),remote.bounded,remote.groups > 3,
    cap.apply_groups(initial,"writer",commands,0,8192) ^ { ^.message == "sync ZIP: invalid capture group limits" },
    cap.apply_groups(initial,"writer",oversized,8,8192) ^ { ^.message == "sync ZIP: event exceeds transfer budget" },
    wire.concat(pieces)^ == pieces[0] ++ pieces[1] ++ pieces[2] ++ pieces[3] ++ pieces[4],
    wire.concat([])^ == null,wire.concat([binary([255])^])^ == binary([255])^,
    codec.decode(codec.encode({s:"λ",i:42,b:true,n:null,items:[1,"x",false]})^)^ ==
        {s:"λ",i:42,b:true,n:null,items:[1,"x",false]}])
