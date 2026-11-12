// Test-only file signaling isolates the real DataChannel repository transport gate.
import agent: lambda.sync.peer_agent
import peer: lambda.sync.peer
import wire: lambda.sync.peer_transport
import rtc: lambda.io.rtc
import disk: lambda.io.disk
import local: lambda.sync.local
import cli: lambda.sync.cli
pn read_signal(directory, name, deadline) any^ {
    while (disk.inspect(directory,name)^ == null) {
        if (now().unix >= deadline) raise error("test channel: signaling deadline")
        sleep(20)^
    }
    return input(directory ++ "/" ++ name,'text')^
}
pn execute(request, key, environment) any^ {
    let opened = agent.open_agent(request.directory,key)^
    local.scan(opened.handle)^
    if (request.action == "capture") { return true }
    if (request.action == "capture_loop") {
        output("ready",request.signals ++ "/capture-ready",{format:'text',atomic:true})^
        var counter = 1
        while (counter <= 100) {
            let suffix = string(counter)
            while (disk.inspect(request.signals,"capture-request-" ++ suffix)^ == null) { sleep(10)^ }
            local.scan(opened.handle)^
            output("done",request.signals ++ "/capture-done-" ++ suffix,{format:'text',atomic:true})^
            counter = counter+1
        }
        return true
    }
    let channel = rtc.create({})^
    let deadline = now().unix+15000
    if (request.role == "client") {
        output(rtc.description(channel,"offer",10000)^,request.signals ++ "/offer",{format:'text',atomic:true})^
        rtc.remote(channel,read_signal(request.signals,"answer",deadline)^,"answer")^
    } else {
        rtc.remote(channel,read_signal(request.signals,"offer",deadline)^,"offer")^
        output(rtc.description(channel,"answer",10000)^,request.signals ++ "/answer",{format:'text',atomic:true})^
    }
    let socket = wire.channel(channel)
    let authorize = pn (id)=> { return agent.current_member(request.directory,key,opened.credentials,id)^ }
    let observe = pn (progress)=> {
        if (request.marker != null) {
            output("committed object",request.marker,{format:'text',atomic:true})^
            sleep(30000)^
        }
    }
    let result = if (request.role == "client")
        peer.exchange_connection(socket,opened.handle,opened.credentials,request.peer,authorize,observe)^
        else peer.serve_connection(socket,opened.handle,opened.credentials,authorize)^
    let counters = rtc.status(channel)^
    rtc.close(channel)^
    agent.materialize(opened.handle)^
    return {*:result,channel_bytes:counters}
}
pn main() { return cli.invoke(execute,"LAMBDA_CONNECTIVITY_RESULT")^ }
