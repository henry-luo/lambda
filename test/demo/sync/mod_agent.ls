// Enrollment owns the profile until complete; ordinary replication starts afterward.
import pairing: lambda.sync.pairing
import profile: .mod_profile
import identity: lambda.sync.peer_identity
import peer: lambda.sync.peer_agent
import connectivity: lambda.sync.connectivity_agent
import ws: lambda.io.websocket
import discovery: lambda.io.discovery

pn host_proof(socket, ticket, credentials) any^ {
    let request = pairing.receive(socket,ticket.deadline)^
    let challenge = pairing.challenge(ticket,credentials.device,request,now().unix)^
    pairing.send(socket,challenge)^
    let response = pairing.receive(socket,ticket.deadline)^
    return pairing.admit(ticket,request,challenge,response,now().unix)^
}
pn accept_pair(listener, ticket, paths, value, key, notify) any^ {
    var attempts = 0
    while (now().unix < ticket.deadline and attempts < 8) {
        let socket = ws.accept(listener,min(1000,max(1,ticket.deadline-now().unix))) ^ { ^ }
        if (socket is error) { continue }
        attempts = attempts+1
        let credentials = identity.load(profile.directory(paths,value)^,key)^
        let accepted = host_proof(socket,ticket,credentials) ^ { ^ }
        if (accepted is error) {
            ws.close(socket) ^ { null }
            notify({type:"pairing_attempt",attempts:attempts,message:"A device could not prove the pairing code. Waiting for your device."})^
            continue
        }
        // the code is spent at the first valid proof; later failures cannot admit another device.
        let invitation = peer.invite(profile.directory(paths,value)^,key,accepted^)^
        pairing.send(socket,invitation)^
        let receipt = pairing.receive(socket,ticket.deadline)^
        ws.close(socket) ^ { null }
        if (receipt != {ok:true}) raise error("The other device did not confirm enrollment. Create a new pairing code.")
        notify({type:"paired",peer:accepted.id})^
        return true
    }
    raise error("Pairing ended. Create a new code and try linking the other device again.")
}
pub pn host(paths, value, key, notify, lifetime_ms=120000) any^ {
    let saved = profile.create(paths,value,key)^
    let credentials = identity.load(profile.directory(paths,value)^,key)^
    let ticket = pairing.create(value,now().unix,lifetime_ms)^
    let listener = pairing.listen()^
    let advertisement = discovery.advertise(pairing.name(value,credentials.device)^,listener.port)^
    notify({type:"pairing",user:saved.user,code:pairing.display(ticket.code)^,deadline:ticket.deadline})^
    let result = accept_pair(listener,ticket,paths,value,key,notify) ^ { ^ }
    discovery.close(advertisement) ^ { null }
    ws.close(listener) ^ { null }
    if (result is error) raise result
    return saved
}
pn join_candidate(paths, value, code, key, credentials, url, deadline) any^ {
    let socket = pairing.connect(url,min(5000,max(1,deadline-now().unix)))^
    let request = pairing.hello(value,credentials.device)^
    pairing.send(socket,request)^
    let response = pairing.receive(socket,deadline)^
    let confirmed = pairing.confirm(value,code,request,response)^
    pairing.send(socket,{proof:confirmed.proof})^
    let invitation = pairing.receive(socket,deadline)^
    let saved = profile.enroll(paths,value,key,invitation,confirmed.expected)^
    pairing.send(socket,{ok:true})^
    ws.close(socket)^
    return saved
}
pn attempt(paths, value, code, key, credentials, url, deadline) any^ {
    // a failed candidate's native resources end with this finite child task.
    return join_candidate(paths,value,code,key,credentials,url,deadline)^
}
pub pn join(paths, value, supplied_code, key, notify, lifetime_ms=120000) any^ {
    if (lifetime_ms < 1 or lifetime_ms > 120000) raise error("sync demo: pairing deadline bounds")
    let code = pairing.code(supplied_code)^
    let credentials = profile.prepare(paths,value,key)^
    let recovered = profile.recover(paths,value,key)^
    if (recovered != null) { notify({type:"paired"})^; return recovered }
    let deadline = now().unix+lifetime_ms
    var attempts = 0
    notify({type:"joining",user:pairing.user(value)^})^
    while (now().unix < deadline and attempts < 8) {
        let hints = pairing.discover(value,min(1000,max(1,deadline-now().unix)))^
        if (len(hints)^ > 0) {
            // rotate candidates, retrying a reachable endpoint after a transient failure.
            let hint = hints[attempts%len(hints)^]
            attempts = attempts+1
            let worker = start(attempt,[paths,value,code,key,credentials,hint.url,deadline])
            let result = wait(worker) ^ { ^ }
            if (not (result is error)) {
                notify({type:"paired"})^
                return result
            }
            // a committed enrollment survives a lost final receipt; do not retry it as a new device.
            let saved = profile.load(paths)^
            if (saved != null) { notify({type:"paired"})^; return saved }
        }
        if (now().unix < deadline) sleep(min(500,deadline-now().unix))^
    }
    raise error("Could not link. Check the user ID and code, and keep the other device's pairing screen open.")
}
pub pn run(paths, key, notify, options={}) any^ {
    let saved = profile.load(paths)^
    if (saved == null) raise error("Create a sync folder or link this device first.")
    notify({type:"running",user:saved.user,folder:saved.folder})^
    return connectivity.run(profile.directory(paths,saved.user)^,key,null,
        {*:{mdns:true,rtc:false,interval_ms:1000},*:options},
        pn (status)=> { notify({type:"status",status:status})^ })^
}
