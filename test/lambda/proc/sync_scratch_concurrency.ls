import local: lambda.sync.local
import repo: lambda.sync.repository
import sqlite: lambda.io.sqlite
import disk: lambda.io.disk
import crypto: lambda.io.crypto

pn hold_writer(handle, parent, name) any^ {
    return repo.access(handle.path,handle.keys,pn (tx,k)=> {
        // Real repository spool creation happens only after its write transaction starts.
        sqlite.execute(tx,"UPDATE objects SET kind = kind WHERE id IS NULL")^
        output("",k.objects ++ "/" ++ name)^
        send(parent,'held')^
        receive()^
        return true
    })^
}
pn recover(handle, parent) any^ {
    send(parent,'started')^
    return repo.access(handle.path,handle.keys,pn (tx,k)=> { return repo.recover(tx,k)^ })^
}
pn main() {
    let area = "temp/sync-scratch-race-" ++ string(sys.proc.self.pid#)
    io.mkdir(area ++ "/tree")^
    let handle = local.create(area ++ "/repo",crypto.key()^,"scratch-race",area ++ "/tree","device")^
    let name = ".lambda-spool-0123456789abcdef0123456789abcdef"
    let writer = start(hold_writer,[handle,self(),name])
    let held = receive()^
    let reader = start(recover,[handle,self()])
    let started = receive()^
    sleep(100)^
    let preserved = disk.inspect(handle.path ++ ".objects",name)^ != null
    send(writer,'release')^
    let finished = wait(writer)^
    let first: any^ = wait(reader)
    // An immediate SQLite upgrade conflict is retryable after the writer exits.
    let retried = repo.access(handle.path,handle.keys,pn (tx,k)=> { return repo.recover(tx,k)^ })^
    let cleaned = disk.inspect(handle.path ++ ".objects",name)^ == null
    io.delete(area)^
    return [held == 'held',started == 'started',preserved,finished,first is error or first is array,
        retried is array,cleaned]
}
