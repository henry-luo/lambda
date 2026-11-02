# Chat

Run `./lambda.exe demo chat` from the checkout root (`lambda demo chat` in a
release). The native Radiant window is a WhatsApp-inspired one-to-one text
messenger, written in Lambda.

## Two windows talking to each other

The quickest way starts a shared server and opens both windows:

```sh
./lambda.exe demo chat user=maya,alex
```

List any of `alex`, `maya`, `noah` and `priya`, once each, separated by commas:

```sh
./lambda.exe demo chat user=maya,alex,noah,priya
```

Each window is signed in as its selected profile. The launcher picks a free
loopback port and configures the clients automatically. Closing one window
leaves the others connected; closing the last window stops the server. Ctrl+C
in the launching terminal closes the session too. Empty, duplicate and unknown
profile names are rejected before starting a server.

Viewer options can follow the user list. `{user}` expands to the window's
profile ID in option values, allowing separate automation files/results:

```sh
./lambda.exe demo chat user=maya,alex --headless \
  --event-file 'temp/replay/{user}.json' --event-result 'temp/replay/{user}-result.json'
```

The launcher uses `CHAT_DEMO_ROOT` (default `temp/chat`) for shared history.
Choose different roots for concurrently running server sessions.

### Separate terminals

Run these from the checkout root in three terminals. Keep the first terminal
running; the other two commands each open a client window.

Terminal 1 — shared server:

```sh
CHAT_DEMO_ROOT=temp/chat-shared CHAT_DEMO_PORT=9090 ./lambda.exe run test/demo/chat/server.ls
```

Terminal 2 — Alex:

```sh
CHAT_DEMO_ENDPOINT=ws://127.0.0.1:9090/chat CHAT_DEMO_USER=alex ./lambda.exe demo chat
```

Terminal 3 — Maya:

```sh
CHAT_DEMO_ENDPOINT=ws://127.0.0.1:9090/chat CHAT_DEMO_USER=maya ./lambda.exe demo chat
```

Write in Alex's window and reply in Maya's window. Their conversations are
selected automatically. In this mode each window connects only its own
profile, and the profile selector is disabled. Use different profiles in
different windows; connecting the same profile again replaces its old session.
Available IDs are `alex`, `maya`, `noah` and `priya`.
`CHAT_DEMO_PEER` optionally chooses a window's initial conversation.

The server listens on loopback, so these commands are for two windows on the
same computer. Set a different `CHAT_DEMO_PORT` and matching endpoint if the
chosen port is occupied. Without a port setting the server chooses a free
port, prints its endpoint, and writes it to `CHAT_DEMO_ROOT/endpoint.txt`
(`temp/chat/endpoint.txt` with the default root).
`CHAT_DEMO_ROOT` changes the server's history/discovery directory. Clients
in shared-server mode leave persistence to the server. Stop it with Ctrl+C.

## Single-window mode

Running `./lambda.exe demo chat` without `CHAT_DEMO_ENDPOINT` starts an
embedded server and connects all four profiles for switching within one window.

1. Write a message to Maya and press **Enter** or the send button. **Shift +
   Enter** inserts a newline. One tick means the service saved it; two mean
   the recipient received it; blue ticks mean it was read.
2. Change **Your profile** to Maya to read the message and reply as her.
   Each of the four demonstration profiles has its own connection and inbox.
3. Click **Go offline**, switch to another profile, and send a message to the
   disconnected participant. Reconnect that participant to replay pending
   messages and update receipts.
4. Close and reopen the app. Accepted messages and receipts survive.
   Search filters contacts by name and their latest message.

There are no scripted replies. The local service, independent client tasks,
WebSocket connections, JSON protocol and persisted history are all real.
The server binds only to `127.0.0.1`. Embedded mode selects a free port;
the standalone server also accepts `CHAT_DEMO_PORT`.
Demo profiles are not authenticated, and messages are not end-to-end
encrypted. The UI says so. This is a local demonstration, not an account on
WhatsApp or a production messaging service.

History is `temp/chat/history.json`; set `CHAT_DEMO_ROOT` to choose another
profile directory. Use a separate directory for each concurrently running
server instance. The service atomically replaces the snapshot before acknowledging
messages or receipt changes. It keeps the full demonstration history in
memory; database storage, paging, quotas and a durable client outbox remain
future milestones. An offline sender keeps the draft and reconnects before
sending; an offline recipient's messages wait at the service. Drafts and
unaccepted sends are not durable across app termination.

## Code and checks

- `chat.ls`: document-owned tasks, participant state and event handlers
  (**S13.3.1v2**, **S12.1.3v2**).
- `server.ls`, `mod_config.ls`: standalone server and environment configuration.
- `mod_ui.ls`, `chat.css`: native conversation UI and responsive layout.
- `lambda.chat`: reusable local service/client and pure message transitions.
- `lambda.io.websocket`: native procedural transport (**D7.2.4v4**), including
  verified `wss://` client connections. The demo uses plain loopback WS;
  production TLS termination and remote authentication are future work.
- [Implementation plan](../../../vibe/impl/Lambda_Impl_Chat.md).

```sh
./lambda.exe test/demo/chat/tests/chat_model.ls
./lambda.exe run test/lambda/conc/chat_service.ls
python3 test/websocket/test_websocket.py
python3 test/demo/chat/test_shared_clients.py
python3 test/demo/chat/test_launch.py
CHAT_DEMO_ROOT=temp/chat-ui-fresh ./lambda.exe demo chat --headless \
  --event-file test/demo/chat/tests/chat.json --event-result temp/chat-ui.json
CHAT_DEMO_ROOT=temp/chat-mobile-fresh ./lambda.exe demo chat --headless \
  --event-file test/demo/chat/tests/chat_mobile.json --event-result temp/chat-mobile.json
```

Use fresh profile directories for replays that assert exact message counts.
Captures go under `temp/chat/shots/`. The model golden is discovered by the
Lambda baseline, alongside procedural transport and service regressions in
`test/lambda/conc`. Independent Python peers test interoperability, malformed
frames, fragmentation, ping/pong, timeouts and verified WSS. Network checks
need loopback networking; native captures also need graphics access.
`make test-chat-clients` runs the shared-server replay: it launches a server
on a free port and two separate client processes, checks messages and read
receipts in both windows, and verifies the persisted server history. Artifacts
stay under `temp/chat-clients/`.
The CLI launcher tests also cover all four profile windows, invalid user lists,
server/client startup failures and interruption. Their artifacts stay under
`temp/chat-launch/`.
