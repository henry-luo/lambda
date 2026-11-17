// Only this task executes repository scopes; network waits stay in its callers.
pub pn run(initial=null, observe=null, initial_cache=null) any^ {
    var token = initial
    var cache = initial_cache
    var watchers = []
    while (true) {
        let message = receive()^
        if (message.stop == true) { return true }
        if (message.kind == 'forget') {
            watchers = [for (item in watchers where item.reply != message.reply) item]
            continue
        }
        if (message.kind == 'changed') {
            if (message.token != token) { send(message.reply,{value:token}) ^ { null }; continue }
            watchers = [*watchers,message]
            continue
        }
        if (message.kind == 'token') {
            send(message.reply,{value:token}) ^ { null }
            continue
        }
        let result = if (message.cached == true) message.work(cache) ^ { ^ } else message.work() ^ { ^ }
        // The work has left its SQLite scope before its committed frontier becomes visible.
        if (observe != null and result is map and (result^).state is map and (result^).state.endpoint != null) {
            let projected = observe((result^).state) ^ { ^ }
            if (projected is error) { send(message.reply,{value:projected}) ^ { null }; continue }
            token = projected^
        }
        if (observe != null and result is map and (result^).token is map) { token = (result^).token }
        // A failed observer must not publish a head cache ahead of its token.
        if (result is map and (result^).repository_cache != null) { cache = (result^).repository_cache }
        for (item in watchers where item.token != token) { send(item.reply,{value:token}) ^ { null } }
        watchers = [for (item in watchers where item.token == token) item]
        // A cancelled requester can disappear while an atomic repository operation finishes.
        send(message.reply,{value:result}) ^ { null }
    }
}
pn request(owner, message) any^ {
    send(owner,{*:message,reply:self()})^
    let received = receive() ^ {
        // Remove cancelled subscriptions even when the next repository change is far away.
        send(owner,{kind:'forget',reply:self()}) ^ { null }
        raise ^
    }
    let result = received.value
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
pub pn changed(owner, token) any^ { return request(owner,{kind:'changed',token:token})^ }
