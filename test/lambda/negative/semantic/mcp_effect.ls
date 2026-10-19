// @expect-error: E224
// @description: S12.1.1v3 — MCP remains procedural even for a read-only method.
import mcp

fn remote_read() => mcp.request("http://127.0.0.1:1/mcp", "tools/list", {})
remote_read()
