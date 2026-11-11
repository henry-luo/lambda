import identity: lambda.sync.peer_identity
import exchange: lambda.sync.peer_exchange
import local: lambda.sync.local
import crypto: lambda.io.crypto
import c: lambda.sync.core
pn main() {
    let area = "temp/sync-peer-exchange-" ++ string(sys.proc.self.pid#)
    io.mkdir(area ++ "/a")^
    io.mkdir(area ++ "/b")^
    let alice = identity.create("exchange",crypto.key()^)^
    let recipient = identity.create("pending",crypto.key()^)^
    let enrolled = identity.enroll(recipient,identity.invite(alice,recipient.device)^,alice.device.id)^
    let a = local.create(area ++ "/a.sqlite",alice.secret,alice.space,area ++ "/a","Alice")^
    let b = local.create(area ++ "/b.sqlite",enrolled.secret,enrolled.space,area ++ "/b","Bob")^
    output("network contents",area ++ "/a/note")^
    local.scan(a)^
    let offered = exchange.offer(a,alice)^
    let first = {packet:offered.frames[0],proof:offered.proof}
    let invalid = exchange.receive_frame(b,enrolled,alice.device,
        {*:first,proof:{*:first.proof,signature:b'00'}}) ^ { ^ }
    let scope = exchange.receive_frame(b,{*:enrolled,space:"wrong"},alice.device,first) ^ { ^ }
    let skipped = exchange.import_fragment(b,null,{id:offered.ids[0],offset:1,total:2,text:"x"}) ^ { ^ }
    for (id in offered.ids) {
        let text = exchange.object_text(a,id)^
        var pending = null
        var index = 0
        while (index < len(text)) {
            let part = exchange.fragment(id,text,index)^
            pending = exchange.import_fragment(b,pending,part)^
            index = index+len(part.text)^
        }
    }
    for (packet in offered.frames) { exchange.receive_frame(b,enrolled,alice.device,{packet:packet,proof:offered.proof})^ }
    let saved = local.status(b)^.state.endpoint.replica
    for (packet in offered.frames) { exchange.receive_frame(b,enrolled,alice.device,{packet:packet,proof:offered.proof})^ }
    let duplicated = local.status(b)^.state.endpoint.replica
    let installed = local.materialize(b)^
    let value = input(area ++ "/b/note",'text')^
    let checked = local.verify(b)^
    io.delete(area)^
    return [invalid is error,scope is error,skipped is error,c.compact(saved) == c.compact(duplicated),
        value == "network contents",installed.state.installation == null,checked.verified]
}
