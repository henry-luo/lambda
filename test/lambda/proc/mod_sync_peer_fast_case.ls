import fast: lambda.sync.peer_fast
import exchange: lambda.sync.peer_exchange
import identity: lambda.sync.peer_identity
import local: lambda.sync.local
import crypto: lambda.io.crypto
import h: ~~.mod_sync_local_helpers
pn check(area, a, b, alice, bob, verify_bounds=false) any^ {
    for (i in 0 to 19) { output("old " ++ string(i),area ++ "/a/file-" ++ string(i))^ }
    local.scan(a)^; h.transfer(a,b)^; local.materialize(b)^
    output("new",area ++ "/a/file-0")^; local.scan(a,["file-0"])^
    let a_have = exchange.have_frontier(alice.space,exchange.current_token(a)^)^
    let incoming = fast.prepare(b,bob,a_have)^
    // Receiving an empty reverse delta must retain the sender's unsent atomic group.
    fast.accept(a,alice,bob.device,incoming.data)^
    let outgoing = fast.prepare(a,alice,incoming.data.have)^
    let before = local.status(b)^
    let forged_clock = [for (entry in outgoing.data.ack.clock) {*:entry,counter:entry.counter+1}]
    let forged = {*:outgoing.data,ack:{*:outgoing.data.ack,clock:forged_clock},
        summary:{*:outgoing.data.summary,clock:forged_clock},
        have:{*:outgoing.data.have,body:{*:outgoing.data.have.body,clock:forged_clock}}}
    let rejected = fast.accept(b,bob,alice.device,forged) ^ { ^ }
    let held = local.status(b)^
    let accepted = fast.accept(b,bob,alice.device,outgoing.data)^
    let duplicate = fast.accept(b,bob,alice.device,outgoing.data)^
    local.materialize(b)^
    let boundary_checks = if (verify_bounds) {
        let first = outgoing.data.bundle.objects[0]
        let bounded = local.export_bounded(a,[first.id],len(first.box)^)^
        let oversized = local.export_bounded(a,[first.id],len(first.box)^-1)^
        let invalid_limit = local.export_bounded(a,[first.id],-1) ^ { ^ }
        let invalid_count = local.export_bounded(a,[for (i in 1 to 129) first.id],1000000) ^ { ^ };
        [bounded == [first],oversized == null,invalid_limit is error,invalid_count is error]
    } else []
    let result = [outgoing.data.summary.delta == true,outgoing.data.summary.objects == 2,
        outgoing.data.summary.objects < len(local.inventory(a)^)^,rejected is error,
        held.generation == before.generation,accepted.generation == before.generation+1,
        duplicate.generation == accepted.generation,
        all([for (record in outgoing.data.bundle.objects) not contains(accepted.state.staged_objects,record.id)^]),
        input(area ++ "/b/file-0",'text')^ == "new",*boundary_checks]
    output(join([for (i in 1 to 70000) "x"],"")^,area ++ "/a/large")^; local.scan(a,["large"])^
    let declined = fast.prepare(a,alice,exchange.have_frontier(bob.space,exchange.current_token(b)^)^)^
    return all([*result,declined == null])
}
pub pn run(cached, verify_bounds=false) any^ {
    let area = "temp/sync-peer-fast-" ++ string(sys.proc.self.pid#)
    io.mkdir(area ++ "/a")^; io.mkdir(area ++ "/b")^
    let alice = identity.create("peer-fast",crypto.key()^)^
    let bob = identity.create(alice.space,alice.secret)^
    let a = local.create(area ++ "/a.sqlite",alice.secret,alice.space,area ++ "/a","a")^
    let b = local.create(area ++ "/b.sqlite",alice.secret,alice.space,area ++ "/b","b")^
    let checked = if (cached) h.owned([a,b],pn (handles)=> {
        return check(area,handles[0],handles[1],alice,bob,verify_bounds)^
    })^ else check(area,a,b,alice,bob,verify_bounds)^
    io.delete(area)^
    return checked
}
