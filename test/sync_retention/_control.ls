// test-only authenticated calls expose storage observations without adding production fault switches.
import cli: lambda.sync.cli
import agent: lambda.sync.peer_agent
import transport: lambda.sync.cloud_transport

pn execute(request, key, environment) any^ {
    let owner = agent.open_agent(request.directory,key)^
    if (request.action == "read") { return transport.read(request.cloud,owner.credentials,request.key)^ }
    return transport.call(request.cloud,owner.credentials,request.action,request.body or {})^
}
pn main() { return cli.invoke(execute,"LAMBDA_CLOUD_RESULT")^ }
