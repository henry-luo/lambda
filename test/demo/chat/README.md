# Chat

Run `./lambda.exe demo chat` from the checkout root (`lambda demo chat` in a
release). The native Radiant window is a WhatsApp-inspired one-to-one text
messenger, written in Lambda.

## Try it

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
The listener binds only to `127.0.0.1` on an automatically selected port.
Demo profiles are not authenticated, and messages are not end-to-end
encrypted. The UI says so. This is a local demonstration, not an account on
WhatsApp or a production messaging service.

History is `temp/chat/history.json`; set `CHAT_DEMO_ROOT` to choose another
profile directory. Use a separate directory for each concurrently running
instance. The service atomically replaces the snapshot before acknowledging
messages or receipt changes. It keeps the full demonstration history in
memory; database storage, paging, quotas and a durable client outbox remain
future milestones. An offline sender keeps the draft and reconnects before
sending; an offline recipient's messages wait at the service. Drafts and
unaccepted sends are not durable across app termination.

## Code and checks

- `chat.ls`: document-owned tasks, participant state and event handlers
  (**S13.3.1v2**, **S12.1.3v2**).
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
