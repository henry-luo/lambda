# Lambda MCP implementation

> **Status:** first client/server slice implemented; overall plan in progress,
> 2026-10-18.
> **Design:** [Lambda_IO_MCP.md](../Lambda_IO_MCP.md).
> **Formal anchors:** **S12.1.1v3** (procedural I/O), **S12.4** (scoped
> resources), **D5.3.3** (precise ownership), **D7.1.2v2/D7.5.2** (I/O
> ownership/policy), **D7.2.4v3** (built-in namespaces).

## First delivery

Implement the shared bounded JSON-RPC engine, a stdio CLI document server,
an HTTP mount on `lambda/serve`, and a procedural generic HTTP client. Test
real process/socket transports with an independently written Python peer.
Do not advertise features before their operation and lifecycle paths work.

The initial client accepts an endpoint descriptor directly. It owns connection
acquisition and release for the duration of one procedural request. It does
not expose a persistent handle or claim implementation of general scoped
`open()` resources. This finite request API remains useful for modern MCP's
stateless HTTP model. Persistent stdio clients and the proposed `open()` API
remain dependent on the general resource implementation under **S12.4**.

The first server tools are `lambda.convert` and `lambda.capabilities`, using
captured text and the existing parse/format providers. Validation, runtime
transforms, rendering, binary/artifact transfer, application handler
registration, OAuth, client callbacks, and subscriptions are later milestones
from the design. The HTTP mount initially serves modern MCP on loopback;
stdio additionally implements the legacy initialization lifecycle.

## Shared defects found during integration

- `http_fetch` passed a `FetchResponse` to the document downloader's
  `HttpResponse` callback. The callback expects a `ByteBuilder` at its start;
  the unrelated struct layout corrupted response storage. Give the callback
  its actual capture owner and transfer bytes into the returned response.
- HTTP worker completion interpreted caller `user_data` (a connection) as a
  worker pool. Store these two owners separately.
- Worker-side convenience responses wrote directly to libuv sockets. Buffered
  dispatch now prepares a reply in the worker and submits it on the loop.
- Connections could be freed before their timer or worker callbacks completed.
  Retain through both handle closures and pending tasks; drain response writes
  before closing nonpersistent connections and drain workers before teardown.
- The accept path copied an initialized libuv socket handle. Its address is
  registered in the loop, so initialize/accept directly into the connection's
  final storage instead of relocating it.

## Verification

Run focused MCP integration and existing server tests during development.
At the delivery boundary, run `make test-lambda-baseline` for the builtin and
runtime changes, plus the relevant input baseline for shared HTTP/parser
changes. Keep generated test source/results under `./temp/`. No performance
claims are made from debug builds.

Verified on 2026-10-18:

- `make build` passes.
- `make test-lambda-baseline` passes **7,725/7,725**, including the input
  baseline and **313/313** precise-GC stress cases.
- `python3 test/mcp/test_mcp.py` passes **34/34** real-transport checks.
- The client suite passes **10/10** with `LAMBDA_EXEC_BACKEND=jit`,
  `LAMBDA_GC_FORCE_EVERY=1`, and `LAMBDA_GC_POISON_FREED=1`.
- Existing `test_serve_gtest` and `test_serve_phase2_gtest` pass **126/126**
  and **22/22**. The new procedural golden also passes on the JIT tier.

Current limits: the HTTP server is loopback-only and advertises only modern
MCP. Stdio additionally supports the legacy 2025-11-25 lifecycle. The client
captures one finite JSON/SSE exchange synchronously; persistent resources,
interactive callbacks, automatic authorization, and general tool-schema
validation remain future work. Dry-run requests are explicitly refused.
