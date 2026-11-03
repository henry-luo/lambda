import updater: lambda.security.tuf.update
let base = "test/safety/tuf/fixtures/"
fn fixture_bytes(filename) => input(base ++ filename, 'binary')^
fn role_bytes(scenario, path) {
    let routes = input(base ++ scenario ++ "/routes.json", 'json')^
    let filename = if (path == "timestamp.json" or path == "1.snapshot.json") path else routes[path];
    fixture_bytes(scenario ++ "/" ++ filename)^
}
fn drive(session, scenario, paths) map^ {
    if (len(paths) > 30) raise error("fixture refresh loop")
    else if (session.action.kind == 'done') {status: session.action.status, paths: paths}
    else {
        let action = session.action
        let response = if (action.kind == 'checkpoint') {status: 'ok'}
            else if (action.path == "2.root.json") {status: 'not_found'}
            else if (ends_with(action.path, ".pkg")) {status: 'ok', bytes: fixture_bytes("artifact.bin")}
            else {status: 'ok', bytes: role_bytes(scenario, action.path)^};
        drive(updater.advance(session, response)^, scenario, if (action.kind == 'fetch') [*paths, action.path] else paths)^
    }
}
fn scenario(scenario_name) map^ => drive(updater.begin_refresh([{role: "root", bytes: fixture_bytes("root.json")}],
    "2026-11-03T00:00:00Z", "lambda.pkg")^, scenario_name, [])^
let delegated = scenario("delegated")^
let cycle = scenario("cycle")^
let encoded = scenario("encoded")^
let hashed = scenario("hash-prefix")^;
[delegated.status, contains(delegated.paths, "1.first.json"), contains(delegated.paths, "1.second.json"),
 cycle.status, len([for (path in cycle.paths where path == "1.first.json") path]),
 hashed.status, contains(hashed.paths, "1.second.json"),
 scenario("terminating") ^ { 'denied' }, scenario("wrong-path") ^ { 'denied' },
 scenario("terminating-cycle") ^ { 'denied' }, encoded.status,
 contains(encoded.paths, "1...%2F%E5%90%8D%23%3Frole.json")]
