# MCP transport integration

Run `make test-mcp` with permission to bind loopback sockets. The Python
standard-library peer is independent of Lambda's codec and exercises real
stdio processes and HTTP sockets. Temporary Lambda scripts and their expected
results live under `./temp/`.

The suite covers modern discovery, legacy stdio initialization, inline document
conversion, framing/escaping/size errors, notifications, concurrent HTTP calls,
disconnect cleanup, and the Lambda client's JSON/SSE reply handling, correlation,
imports, bearer credentials, and protocol/tool-error distinction.

Current entry points:

```sh
./lambda.exe mcp --transport stdio
./lambda.exe serve --mcp --host 127.0.0.1 --port 8080
```

The Lambda module's initial finite request API takes a URL string or
`{url: "http://127.0.0.1:8080/mcp", timeout: 30, token_env: "TOKEN_VARIABLE"}`
descriptor. `timeout` is seconds; `token_env` is optional. `mcp.request` returns
the protocol result payload and `mcp.call_tool` returns the full tool result.
Both are `pn` operations under **S12.1.1v3**. The implementation record lists
the remaining protocol and operation coverage.

```lambda no-run
// no-run: start the loopback MCP server shown above before running this client.
import mcp

pn main() {
    let result = mcp.call_tool("http://127.0.0.1:8080/mcp", "lambda.convert", {
        document: {kind: "text", mimeType: "application/json", text: "{\"amount\":42}"},
        to: "yaml"
    })^
    return result.structuredContent.documents[0].text
}
```

Calls currently block for one bounded exchange; JSON and finite SSE responses
are captured before returning. Interactive client callbacks, persistent
connections, legacy HTTP, automatic OAuth, and general JSON Schema validation
are not implemented. Dry-run mode rejects MCP requests before connecting.
