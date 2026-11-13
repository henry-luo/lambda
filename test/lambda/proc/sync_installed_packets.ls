import identity: lambda.sync.peer_identity
import exchange: lambda.sync.peer_exchange
import local: lambda.sync.local
import crypto: lambda.io.crypto
import workspace: lambda.sync.workspace
import core: lambda.sync.core
import codec: lambda.sync.codec
import owner: lambda.sync.task_owner
import h: ~~.mod_sync_local_helpers
pn main() {
    let area = "temp/sync-installed-packets-" ++ string(sys.proc.self.pid#)
    for (name in ["a","b","c"]) { io.mkdir(area ++ "/" ++ name)^ }
    let alice = identity.create("installed-packets",crypto.key()^)^
    let receiver = identity.create(alice.space,alice.secret)^
    let a = local.create(area ++ "/a.sqlite",alice.secret,alice.space,area ++ "/a","a")^
    let raw_b = local.create(area ++ "/b.sqlite",alice.secret,alice.space,area ++ "/b","b")^
    let cache_b = {*:raw_b,keys:{*:raw_b.keys,cache_enabled:true}}
    let initial = local.status(cache_b)^
    let broker = start(owner.run,[exchange.token(initial.state)^,exchange.token,initial.repository_cache])
    let b = {*:cache_b,keys:{*:cache_b.keys,owner:broker,token_cached:true}}
    let c = local.create(area ++ "/c.sqlite",alice.secret,alice.space,area ++ "/c","c")^
    for (i in 0 to 4) { output("saved " ++ string(i),area ++ "/a/file-" ++ string(i))^ }
    local.scan(a)^
    let first = exchange.offer(a,alice,true,true,exchange.have_frontier(alice.space,exchange.current_token(b)^)^)^
    exchange.receive_bundle(b,receiver,alice.device,exchange.bundle(a,first.batch,first.ids)^)^
    let installed = local.materialize(b)^
    output("next saved version",area ++ "/a/file-0")^
    local.scan(a)^
    let next = exchange.offer(a,alice,true,true,exchange.have_frontier(alice.space,exchange.current_token(b)^)^)^
    let ids = local.missing_objects(b,next.ids)^
    let applied = exchange.receive_bundle(b,receiver,alice.device,exchange.bundle(a,next.batch,ids)^)^
    let before_bad = applied.generation
    let old = first.batch[0]
    let op = old.packet.body.operations[0]
    let forged = {base:old.packet.body.base,frontier:old.packet.body.frontier,
        operations:[{*:op,time:op.time+1},*slice(old.packet.body.operations,1)^]}
    let corrupt = exchange.attest_delta(alice,{*:old.packet,body:{id:codec.digest([
        old.packet.document,old.packet.profile,old.packet.epoch,forged])^,*:forged}})^
    let rejected = exchange.receive_frames(b,receiver,alice.device,[corrupt]) ^ { ^ }
    let stable = local.status(b)^
    local.materialize(b)^
    let fallback = exchange.offer(b,receiver,true,true,exchange.have_frontier(alice.space,exchange.current_token(c)^)^)^
    exchange.receive_bundle(c,alice,receiver.device,exchange.bundle(b,fallback.batch,fallback.ids)^)^
    local.materialize(c)^
    let entry = h.at_path(stable.state,"file-0")^
    let private = local.missing_objects(b,[stable.id])^
    let history = local.history(b,entry.id)^
    let result = [first.summary.delta == true and len(installed.state.endpoint.replica.log)^ == 5,
        next.summary.delta == true,len(applied.state.endpoint.replica.log)^ == 1,
        len(applied.state.endpoint.journal)^ == 1,
        core.dominates(applied.state.endpoint.replica.floor,installed.state.base.checkpoint.frontier)^,
        len(history)^ == 2,rejected is error and stable.generation == before_bad,
        fallback.summary.delta == false,input(area ++ "/c/file-0",'text')^ == "next saved version",
        len(private)^ == 1 and private[0] == stable.id,
        len(local.missing_objects(b,[]))^ == 0]
    send(broker,{stop:true})^; wait(broker)^
    io.delete(area)^
    return all(result)
}
