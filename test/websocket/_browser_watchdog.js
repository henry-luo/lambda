const socket = new WebSocket(TEST_WS_URL);
socket.onopen = () => console.log('WEBSOCKET_WATCHDOG_OPEN');
// The CLI's finite drain must retire the document-owned native tasks before
// its fallback handle cleanup. A GUI host retains the connection instead.
