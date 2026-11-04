// a standalone owner keeps the listener and peer tasks alive (D9.3.4).
import chat: lambda.chat
import config: .mod_config

pn listening(event) {
    output(event.endpoint, config.root ++ "/endpoint.txt", {format: 'text', atomic: true})^
    print("Chat server listening at " ++ event.endpoint ++ "\n")
}
pn main() {
    io.mkdir(config.root)^
    let port = int(config.setting("CHAT_DEMO_PORT", "0"))
    chat.serve(port, config.root ++ "/history.json", listening)^
}
