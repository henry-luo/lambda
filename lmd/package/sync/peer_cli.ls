// invoke with a public JSON request path and a separate random wrapping key in the environment.
import agent: .peer_agent
import identity: .peer_identity
import codec: .codec
import cli: .cli

pn execute(request, key, environment) any^ {
    let directory = request.directory
    let action = request.action
    if (action == "device") { return agent.device(directory,key)^ }
    if (action == "init") { return agent.initialize(directory,key,request.root,request.space,request.label)^ }
    if (action == "identity") { return identity.load(directory,key)^.device }
    if (action == "invite") {
        let target = codec.decode(input(request.recipient,'text')^,16384)^
        let invitation = agent.invite(directory,key,target)^
        output(codec.encode(invitation)^,request.output,{format:'text',atomic:true})^
        return {recipient:target.id}
    }
    if (action == "enroll") {
        let offer = codec.decode(input(request.invitation,'text')^,65536)^
        return agent.enroll(directory,key,request.root,request.label,offer,request.expected)^
    }
    if (action == "export_identity") {
        let device = identity.load(directory,key)^.device
        output(codec.encode(device)^,request.output,{format:'text',atomic:true})^
        return {id:device.id}
    }
    if (action == "revoke") { return agent.revoke(directory,key,request.peer)^ }
    if (action == "status") { return agent.status(directory,key)^ }
    if (action == "serve") {
        return agent.serve(directory,key,request.port,request.bind,request.max_sessions or 0,request.ready)^
    }
    if (action == "sync") { return agent.sync(directory,key,request.url,request.peer)^ }
    if (action == "watch") {
        return agent.watch(directory,key,request.url,request.peer,request.interval_ms or 5000,request.rounds or 0)^
    }
    raise error("sync agent: unknown action")
}
pn main() { return cli.invoke(execute)^ }
