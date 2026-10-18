import wire: lambda.mcp

pn main() {
    // rejected descriptors do not acquire a socket or leak a native exchange.
    wire.request(null, "ping", {}) ^ { print("endpoint rejected\n") }
    wire.call_tool(null, "missing", {}) ^ { print("tool endpoint rejected\n") }
    wire.request({url: "file:///denied"}, "ping", {}) ^ { print("scheme rejected\n") }
    wire.request({url: "http://127.0.0.1:1/mcp", timeout: 0}, "ping", {}) ^ {
        print("timeout rejected\n")
    }
}
