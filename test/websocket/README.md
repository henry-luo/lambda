# WebSocket interoperability

Run `make test-websocket`, or run both `python3 test/websocket/test_websocket.py`
and `node test/websocket/test_browser_websocket.cjs` after building.
The standard-library Python peers independently encode and decode
RFC 6455 frames. OpenSSL creates a short-lived local certificate; no public
service, extra package or vendor modification is required. All generated
material stays under `temp/websocket-tests`.

The Node gate uses the root package's `ws` development dependency and Puppeteer
headless shell to verify the WHATWG browser API and a real Chromium peer.
Prepare these with `npm install` and, if the shell is absent,
`npx puppeteer browsers install chrome-headless-shell`.
Set `CHROME_HEADLESS_SHELL` to override Puppeteer's installed shell and
`LAMBDA_WEBSOCKET_EXE` to select a captured executable. The API cases follow
`ref/wpt/websockets` constructor/state/binary/close contracts. They are a
focused compatibility gate, not the entire WPT suite. Repeat with
`LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1` to check exact ownership.

The `_*.ls` files are harness clients, not golden-discovered scripts. Golden
transport, cancellation, task identity and service tests are under
`test/lambda/conc`; the demo model golden and UI replays are in
`test/demo/chat/tests`. Both harnesses need local socket access. The test
runner never disables TLS verification.

Contracts: procedural IO **D7.2.4v4**, task suspension **S13.1.2v2**, precise
roots **D5.3.3**, and task-handle identity **S13.1.3v2**.
