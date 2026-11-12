import rtc: lambda.io.rtc
pn blocked(parent) any^ {
    let connection = rtc.create({})^
    send(parent,'waiting')^
    return rtc.receive(connection,60000)^
}
pn main() {
    let capabilities = rtc.capabilities()^
    let a = rtc.create({})^
    let b = rtc.create({})^
    let before = rtc.status(a)^
    let offer = rtc.description(a,"offer",10000)^
    rtc.remote(b,offer,"offer")^
    let answer = rtc.description(b,"answer",10000)^
    rtc.remote(a,answer,"answer")^
    let bytes = binary([for (i in 0 to 65535) i%256])^
    rtc.send(a,bytes)^
    let received = rtc.receive(b,10000)^
    rtc.send(b,received.data)^
    let echoed = rtc.receive(a,10000)^
    let connected = rtc.status(a)^
    let timed = rtc.receive(a,10) ^ { ^ }
    let bad = rtc.create({stun:["turn:example.invalid"]}) ^ { ^ }
    let repeated = rtc.description(a,"offer",10) ^ { ^ }
    let invalid = rtc.send(a,"text") ^ { ^ }
    rtc.close(a)^
    rtc.close(a)^
    rtc.close(b)^
    let closed = rtc.status(a) ^ { ^ }
    let task = start(blocked,[self()])
    let waiting = receive()^
    cancel(task)
    let cancelled: any^ = wait(task)
    return [capabilities.rtc,not before.open,contains(offer,"a=fingerprint:")^,
        received.type == "binary",received.data == bytes,echoed.data == bytes,connected.open,
        timed is error,bad is error,repeated is error,invalid is error,closed is error,
        waiting == 'waiting',cancelled is error and cancelled.code == 319]
}
