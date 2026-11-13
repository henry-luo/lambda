import disk: lambda.io.disk
pn collect(watch, path) any^ {
    let deadline = now().unix+5000
    while (now().unix < deadline) {
        let changes = disk.changes(watch,500)^
        if (changes.rescan or contains(changes.paths,path)^) { return true }
    }
    return false
}
pn blocked(watch, parent) any^ {
    send(parent,true)^
    return disk.changes(watch,60000)^
}
pn owning(root, parent) any^ {
    let watch = disk.watch(root)^
    send(parent,watch)^
    while (true) { disk.changes(watch,60000)^ }
}
pn main() {
    let area = "temp/sync-disk-watch-" ++ string(sys.proc.self.pid#)
    io.mkdir(area ++ "/nested")^
    let watch = disk.watch(area)^
    output("one",area ++ "/nested/file.txt")^
    let created = collect(watch,"nested/file.txt")^
    output("two",area ++ "/nested/file.txt")^
    let edited = collect(watch,"nested/file.txt")^
    io.move(area ++ "/nested/file.txt",area ++ "/nested/moved.txt")^
    let moved = collect(watch,"nested/moved.txt")^
    io.mkdir(area ++ "/new/deep")^
    output("new",area ++ "/new/deep/file.txt")^
    let nested = collect(watch,"new/deep/file.txt")^
    io.delete(area ++ "/new/deep/file.txt")^
    let deleted = collect(watch,"new/deep/file.txt")^
    // Drain coalesced native hints before exercising cancellation of a parked read.
    sleep(100)^
    disk.changes(watch,0)^
    let child = start(blocked,[watch,self()])
    receive()^
    cancel(child)
    let cancelled = wait(child) ^ { ^ }
    disk.unwatch(watch)^
    disk.unwatch(watch)^
    let closed = disk.changes(watch,0) ^ { ^ }
    let bad = disk.watch(area ++ "/missing") ^ { ^ }
    let task = start(owning,[area,self()])
    let owned = receive()^
    cancel(task)
    let ended = wait(task) ^ { ^ }
    let released = disk.changes(owned,0) ^ { ^ }
    io.delete(area)^
    return [created,edited,moved,nested,deleted,cancelled is error and cancelled.code == 319,
        closed is error,bad is error,ended is error and ended.code == 319,released is error]
}
