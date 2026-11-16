import local: lambda.sync.local
import crypto: lambda.io.crypto
import exchange: lambda.sync.peer_exchange
import h: ~~.mod_sync_local_helpers
pn main() {
    let area = "temp/sync-ack-staging-" ++ string(sys.proc.self.pid#)
    io.mkdir(area ++ "/a")^; io.mkdir(area ++ "/b")^
    let secret = crypto.key()^
    let a = local.create(area ++ "/a.sqlite",secret,"ack-staging",area ++ "/a","a")^
    let b = local.create(area ++ "/b.sqlite",secret,"ack-staging",area ++ "/b","b")^
    output("contents",area ++ "/a/file")^
    local.scan(a)^; h.transfer(a,b)^
    let initial = local.status(b)^
    let ids = initial.state.staged_objects
    let release = take(ids,len(ids)^-1)^
    let outgoing = {ids:release,summary:{clock:initial.state.endpoint.replica.clock}}
    exchange.request(b,null,{id:"peer"},outgoing,null,{kind:'ack',body:[]})^
    let before = local.status(b)^
    let rejected = local.acknowledge(b,"peer",[{actor:"unknown",counter:1}],ids) ^ { ^ }
    let held = local.status(b)^
    exchange.request(b,null,{id:"peer"},outgoing,null,{kind:'ack',body:before.state.endpoint.replica.clock})^
    let accepted = local.status(b)^
    let duplicate = local.acknowledge(b,"peer",before.state.endpoint.replica.clock,release)^
    let reopened = local.open_repository(area ++ "/b.sqlite",secret,"ack-staging")^
    let durable = local.status(reopened)^
    local.materialize(reopened)^
    let result = [len(ids)^ >= 2,before.state.staged_objects == ids,rejected is error,
        held.generation == before.generation and held.state.staged_objects == ids,
        accepted.generation == before.generation+1,
        accepted.state.staged_objects == [ids[last]],duplicate.generation == accepted.generation,
        durable.state.endpoint.peers == accepted.state.endpoint.peers,
        durable.state.staged_objects == [ids[last]],input(area ++ "/b/file",'text')^ == "contents"]
    io.delete(area)^
    return all(result)
}
