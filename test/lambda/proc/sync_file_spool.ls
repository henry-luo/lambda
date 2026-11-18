import disk: lambda.io.disk
pn blocked_spool(area, parent) any^ {
    return disk.spool(area,1024,pn (spool)=> {
        disk.spool_write(spool,binary("pending")^)^
        send(parent,'staged')^
        sleep(30000)^
        return disk.spool_publish(spool,".pack")^
    })^
}
pn main() {
    let area = "temp/sync-spool-" ++ string(sys.proc.self.pid)
    io.mkdir(area)^
    let payload = binary("one-two-three")^
    let run = disk.spool(area,1024,pn (spool)=> {
        let first = disk.spool_write(spool,slice(payload,0,4)^)^
        let second = disk.spool_write(spool,slice(payload,4)^)^
        let before = disk.spool_read(spool,0,len(payload)^)^
        let id = disk.spool_publish(spool,".pack")^
        let saved = disk.capture(area,id ++ ".pack",1024)^.bytes
        let again = disk.spool_write(spool,binary("reset")^)^
        let foreign = start(pn (value)=> { return disk.spool_read(value,0,1)^ },[spool])
        let denied: any^ = wait(foreign)
        return {checks:[first == 0,second == 4,before == payload,saved == payload,again == 0,denied is error],escaped:spool}
    })^
    let escaped = disk.spool_read(run.escaped,0,1) ^ { ^ }
    let oversize = disk.spool(area,2,pn (spool)=> { return disk.spool_write(spool,payload)^ }) ^ { ^ }
    let aborted = disk.spool(area,1024,pn (spool)=> {
        disk.spool_write(spool,payload)^
        raise error("abort before publication")
    }) ^ { ^ }
    let writer = start(blocked_spool,[area,self()])
    let signal = receive()^
    cancel(writer)
    let cancelled: any^ = wait(writer)
    let files = disk.entries(area,"",100)^
    let checks = [*run.checks,escaped is error,oversize is error,aborted is error,
        signal == 'staged',cancelled is error and cancelled.code == 319,
        len(files)^ == 1,ends_with(files[0].name,".pack")^]
    io.delete(area)^
    return all(checks)
}
