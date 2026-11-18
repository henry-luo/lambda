import local: lambda.sync.local
import repo: lambda.sync.repository
import peer: lambda.sync.peer_agent
import crypto: lambda.io.crypto
import disk: lambda.io.disk
import sqlite: lambda.io.sqlite
pn main() {
    let area = "temp/sync-startup-cache-" ++ string(sys.proc.self.pid)
    io.mkdir(area ++ "/tree")^
    let secret = crypto.key()^
    let initial = local.create(area ++ "/repo",secret,"startup-cache",area ++ "/tree","device")^
    output("one",area ++ "/tree/note")^
    local.scan(initial)^
    let cached = local.open_repository(initial.path,secret,"startup-cache",true)^
    let first = local.status(cached)^
    output("two",area ++ "/tree/note")^
    local.scan(initial)^
    // the immutable cached head must lose its proof when another writer advances the database.
    let advanced = local.status(cached)^
    let refreshed = local.with_cache(cached,advanced)
    output("external",area ++ "/tree/note")^
    let scanned = local.scan(refreshed,null,true)^
    let latest = local.with_cache(refreshed,scanned)
    let row = repo.access(initial.path,initial.keys,pn (tx,k)=> {
        return sqlite.query(tx,"SELECT object FROM heads WHERE name = 'local'")^[0][0]
    })^
    repo.access(initial.path,initial.keys,pn (tx,k)=> {
        sqlite.execute(tx,"UPDATE heads SET object = ? WHERE name = 'local'",["invalid-head-pointer"])^
    })^
    let rejected = local.status(latest) ^ { ^ }
    repo.access(initial.path,initial.keys,pn (tx,k)=> {
        sqlite.execute(tx,"UPDATE heads SET object = ? WHERE name = 'local'",[row])^
    })^
    let agent_dir = area ++ "/agent"
    io.mkdir(agent_dir)^
    let wrapping_key = crypto.key()^
    peer.device(agent_dir,wrapping_key)^
    peer.initialize(agent_dir,wrapping_key,area ++ "/tree",uuid()^,"cached startup")^
    let resumed = peer.resume_agent(agent_dir,wrapping_key,true)^
    let checked = local.status(resumed.handle)^
    let checks = [cached.keys.cached_head.validated == true,first.id == cached.keys.cached_head.id,
        advanced.generation > first.generation,advanced.id != first.id,
        scanned.state.endpoint.replica.clock != advanced.state.endpoint.replica.clock,
        rejected is error,local.verify(latest)^.verified,
        resumed.handle.keys.cached_head.validated == true,
        checked.id == resumed.handle.keys.cached_head.id]
    io.delete(area)^
    return all(checks)
}
