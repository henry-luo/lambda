// D11.2.4: test-only host relay. All trust decisions use the production package;
// request/state files are isolated fixture data, never production grant state.
import updater: lambda.security.tuf.update
import tuf: lambda.security.tuf
import json: lambda.security.tuf.json
import meta: lambda.security.tuf.metadata

fn step(request) map^ {
    if (request.op == "begin") updater.begin_refresh([for (entry in request.cache)
        {role: entry.role, bytes: input(entry.filename, 'binary')^}], request.utc, null)^
    else {
        let saved = input("state.lmd", 'mark')^;
        if (request.op == "select") updater.select_target(saved, request.target)^
        else if (request.op == "cached_target") {
            let valid = tuf.verify_target(saved.description, input("response.bin", 'binary')^) ^ { false };
            {cache_valid: valid}
        }
        else updater.advance(saved, {status: symbol(request.status), bytes: input("response.bin", 'binary')^})^
    }
}
pn main() {
    let request = input("request.json", 'json')^
    let result = step(request) ^ { {failure: true} };
    if (result.failure == true) output({ok: false}, "reply.json", 'json')^
    else if (request.op == "cached_target") output({ok: true, valid: result.cache_valid}, "reply.json", 'json')^
    else {
        output(result, "state.lmd", 'mark')^
        let action = result.action
        let reply = {ok: true, kind: string(action.kind), status: string(action.status), path: action.path,
            limit: action.limit, artifact: result.phase == 'artifact',
            entries: if (action.kind == 'checkpoint') [for (entry in action.entries)
                {role: entry.role, hex: meta.hex(entry.bytes)}] else null};
        output(reply, "reply.json", 'json')^
    }
}
