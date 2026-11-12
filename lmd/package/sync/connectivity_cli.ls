import agent: .connectivity_agent
import connectivity: .connectivity
import peer: .peer_agent
import cli: .cli
pn execute(request, key, environment) any^ {
    if (request.action == "capabilities") { return connectivity.capabilities()^ }
    if (request.action == "discover") {
        let opened = peer.open_agent(request.directory,key)^
        return connectivity.discover(opened.credentials,request.timeout_ms or 1000)^
    }
    if (request.action == "run") { return agent.run(request.directory,key,request.cloud,request.options or {})^ }
    raise error("sync connectivity: unknown action")
}
pn main() { return cli.invoke(execute,"LAMBDA_CONNECTIVITY_RESULT")^ }
