// S13.3.1 and D11.1.2: parked callback cancellation must join before native handles/stages escape.
import disk: lambda.io.disk
import crypto: lambda.io.crypto
pn blocked_write(area, parent) any^ {
    return disk.write_stream(area,"cancelled",{size:1,hash:hash("x",'sha256')^},pn (acc)=> {
        send(parent,'staged')^
        sleep(30000)^
        return {bytes:binary("x")^,state:null,done:false}
    })^
}
pn blocked_read(area, parent) any^ {
    return disk.read_stream(area,"source",{},pn (acc,bytes)=> {
        send(parent,'reading')^
        sleep(30000)^
        return bytes
    })^
}
pn main() {
    let area = "temp/sync-stream-cancel-" ++ string(sys.proc.self.pid#)
    io.mkdir(area)^
    disk.install(area,"source",binary("x")^,null)^
    let writer = start(blocked_write,[area,self()])
    let first = receive()^
    cancel(writer)
    let written: any^ = wait(writer)
    let reader = start(blocked_read,[area,self()])
    let second = receive()^
    cancel(reader)
    let read: any^ = wait(reader)
    let entries = disk.entries(area,"",8)^
    let source = disk.capture(area,"source",8)^
    io.delete(area)^
    return [first == 'staged',written is error and written.code == 319,
        second == 'reading',read is error and read.code == 319,
        len(entries)^ == 1 and entries[0].name == "source",source.bytes == binary("x")^]
}
