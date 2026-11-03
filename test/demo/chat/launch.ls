// S13.3.1v2: the launcher owns the server and process tasks until all windows exit.
import chat: lambda.chat
import config: .mod_config
import launch: .mod_launch
let ARGS = sys.proc.self.argv#

pn serve(owner) {
    let result = chat.serve(0, config.root ++ "/history.json", pn (event) => { send(owner, event)^ }) ^ { ^ }
    send(owner, {type: "server_exit", result: result}) ^ { null }
    return result
}
pn client(owner, endpoint, user, peer, options) {
    let result = io.exec(ARGS[0], ["demo", "chat", *launch.client_args(options, user)],
        {timeout_ms: 0, env: {CHAT_DEMO_ENDPOINT: endpoint, CHAT_DEMO_USER: user,
            CHAT_DEMO_PEER: peer, LAMBDA_LOG_FILE: config.root ++ "/client-" ++ user ++ ".log"}}) ^ { ^ }
    send(owner, {type: "client_exit", user: user, result: result}) ^ { null }
    return result
}
pn main() {
    let ids = launch.users(ARGS[3])^
    let options = slice(ARGS, 4)
    if (len([for (argument in options where starts_with(argument, "user=")) argument]) > 0)
        raise error("Chat: specify user= only once")
    io.mkdir(config.root)^
    let owner = self()
    let server = start(serve, [owner])
    let ready = receive()^
    if (ready.type != "listening") {
        if (ready.result is error) { raise ready.result }
        raise error("Chat: server stopped before it was ready")
    }
    print("Chat server listening at " ++ ready.endpoint ++ "\n")
    print("Opening chat windows for " ++ join(ids, ", ") ++ "\n")
    // a comprehension keeps every process task in this procedure's scope.
    let clients = [for (user in ids) start(client, [owner, ready.endpoint, user, launch.peer(ids, user), options])]
    var remaining = len(clients)
    while (remaining > 0) {
        let event = receive()^
        if (event.type == "server_exit") {
            if (event.result is error) { raise event.result }
            raise error("Chat: server stopped while client windows were open")
        }
        if (event.type != "client_exit") { continue }
        let result = event.result
        if (result is error) { raise result }
        if (result.exit_code != 0 or result.timed_out or result.cancelled) {
            for (stream in [result.stdout, result.stderr] where stream != null) print(parse(stream, 'text')^)
            raise error("Chat: client '" ++ event.user ++ "' exited with code " ++ string(result.exit_code))
        }
        remaining = remaining - 1
    }
    cancel(server)
    let stopped = wait(server) ^ { ^ }
    if (stopped is error and stopped.code != 319) { raise stopped }
    return null
}
