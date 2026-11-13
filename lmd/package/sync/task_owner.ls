// Only this task executes repository scopes; network waits stay in its callers.
pub pn run(initial=null, observe=null, initial_cache=null) any^ {
    var token = initial
    var cache = initial_cache
    while (true) {
        let message = receive()^
        if (message.stop == true) { return true }
        if (message.kind == 'token') {
            send(message.reply,{value:token}) ^ { null }
            continue
        }
        let result = if (message.cached == true) message.work(cache) ^ { ^ } else message.work() ^ { ^ }
        if (result is map and (result^).repository_cache != null) { cache = (result^).repository_cache }
        // The work has left its SQLite scope before its committed frontier becomes visible.
        if (observe != null and result is map and (result^).state is map and (result^).state.endpoint != null) {
            let projected = observe((result^).state) ^ { ^ }
            if (projected is error) { send(message.reply,{value:projected}) ^ { null }; continue }
            token = projected^
        }
        if (observe != null and result is map and (result^).token is map) { token = (result^).token }
        // A cancelled requester can disappear while an atomic repository operation finishes.
        send(message.reply,{value:result}) ^ { null }
    }
}
pn request(owner, message) any^ {
    send(owner,{*:message,reply:self()})^
    let result = receive()^.value
    if (result is error) raise result
    return result
}
pub pn call(owner, work) any^ {
    if (owner == null or owner == self()) { return work()^ }
    // An isolated reply mailbox cannot consume the agent's filesystem/timer events.
    let reply = start(request,[owner,{work:work}])
    return wait(reply)^
}
pub pn current(owner) any^ {
    let reply = start(request,[owner,{kind:'token'}])
    return wait(reply)^
}
pub pn cached(owner, work) any^ {
    let reply = start(request,[owner,{work:work,cached:true}])
    return wait(reply)^
}
