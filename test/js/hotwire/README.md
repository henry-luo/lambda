# Hotwire compatibility fixtures

Pinned upstream packages: **Stimulus 3.2.2**, **Turbo 8.0.23**, and
**esbuild 0.28.1**. The committed bundles run without npm or public network
access. Upstream source is bundled without modifications. Turbo's npm archive
omits its license, so `TURBO_LICENSE.txt` preserves the license from the
matching upstream tag; `THIRD_PARTY_LICENSES.txt` contains all bundled notices.

| Fixture | Coverage |
|---|---|
| `../hotwire_stimulus.js` | Controller lifecycle, nested scopes, targets, typed values, action parameters, prevention, classes, mutation observation, reconnection and cleanup |
| `../hotwire_turbo_streams.js` | Append, prepend, before, after, replace, update, remove, duplicate IDs, selector targets, render hooks and cleanup |
| `http.bundle.js` | Real Lambda HTTP transport: frame loading/links, fetched and streamed Stimulus controllers, form submissions, Drive visits/history, 303 redirects, 422 validation and server request audits |

Readable sources are `stimulus.js`, `streams.js`, `http.js` and `support.js`.
The shared library builder is `../library_build.cjs`. Expected output is
committed beside each bundle. Six focused DOM regressions cover custom
element construction/private fields/reactions, repeated Range replacement,
form reflection, full-document HTML parsing, Window scrolling and rendering
after a native task.

## Run

From the repository root, build the executable and native tests using
`make build` and `make build-test`, then run:

```sh
./test/test_js_gtest.exe --full-ast --gtest_filter='*hotwire*:*dom_custom_element_lifecycle*:*dom_range_repeat_replace*:*dom_form_reflected_submission*:*dom_raf_after_task*:*dom_parser_html_document*:*dom_window_scroll*'
./test/test_js_gtest.exe --full-mir --gtest_filter='*hotwire*:*dom_custom_element_lifecycle*:*dom_range_repeat_replace*:*dom_form_reflected_submission*:*dom_raf_after_task*:*dom_parser_html_document*:*dom_window_scroll*'
./test/test_serve_gtest.exe --gtest_filter='HotwireServerTest.*:Tiers/HotwireHttpTest.*'
```

The server tests start a separate instance of Lambda's native `lambda/serve`
server, bound to `127.0.0.1` on an OS-assigned port. Each client uses the real
`lambda.exe js --document http://...` path. AST, MIR and AUTO have distinct
GTest cases. The server records methods, paths, headers and bodies; client
assertions check those records as well as DOM behavior. Readiness uses an
atomic file under `temp/hotwire/`; subprocesses have bounded startup,
execution and shutdown. These tests require loopback socket access.

The native test server's internal standalone mode is:

```sh
mkdir -p temp/hotwire
./test/test_serve_gtest.exe --hotwire-server temp/hotwire/ready
```

The readiness file contains the assigned port. Terminate the process normally
to exercise graceful shutdown. This test fixture uses the native API directly;
the general `lambda serve` CLI and Lambda `io.http` bindings are still stubs.

## Regenerate

```sh
cd test/js/hotwire
npm ci
npm run build
```

Commit regenerated bundles and notices with any source/dependency change.
The lockfile pins transitive dependencies. Tests use explicit events and
mutation conditions with failure deadlines; no fetch/library shims are used.

## Scope

These cases qualify a core Stimulus/Turbo HTTP milestone. Rails integration,
Hotwire Native, WebSocket/SSE push, morphing, full browser navigation, complete
custom-element reaction-stack semantics and all Fetch body/stream policies
remain outside this fixture's acceptance scope. Runtime changes and exact
verification evidence are recorded in
[`Lambda_Impl_Hotwire.md`](../../../vibe/impl/Lambda_Impl_Hotwire.md).
