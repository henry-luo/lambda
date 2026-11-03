// Focused WHATWG cases adapted from ref/wpt/websockets; TEST_WS_URL is injected.
function check(value, label) { if (!value) throw new Error(label); }
function throwsName(fn, name) {
    let caught = '';
    try { fn(); } catch (e) { caught = e.name; }
    check(caught === name, 'expected ' + name + ', got ' + caught);
}
throwsName(() => WebSocket(TEST_WS_URL), 'TypeError');
throwsName(() => new WebSocket(), 'TypeError');
throwsName(() => new WebSocket('ftp://example.org'), 'SyntaxError');
throwsName(() => new WebSocket(TEST_WS_URL + '#'), 'SyntaxError');
throwsName(() => new WebSocket(TEST_WS_URL, ['chat.v1', 'chat.v1']), 'SyntaxError');
throwsName(() => new WebSocket(TEST_WS_URL, ['bad protocol']), 'SyntaxError');
const protocolName = {toString() {return 'chat.v1';}};
const offers = [protocolName];
const socket = new WebSocket(TEST_WS_URL.replace('ws:', 'http:'), offers);
check(offers[0] === protocolName, 'protocol sequence leaves input array unchanged');
check(socket.url === TEST_WS_URL, 'http URL normalizes to ws');
check(socket instanceof WebSocket && socket instanceof EventTarget, 'prototype chain');
check(socket.readyState === 0 && socket.CONNECTING === 0 && WebSocket.CLOSED === 3, 'constants and connecting');
check(socket.binaryType === 'blob' && socket.protocol === '' && socket.extensions === '', 'initial attributes');
socket.binaryType = 'invalid';
check(socket.binaryType === 'blob', 'ignore invalid binaryType');
throwsName(() => socket.send('early'), 'InvalidStateError');
throwsName(() => socket.close(1001), 'InvalidAccessError');
throwsName(() => socket.close(3000, '€'.repeat(42)), 'SyntaxError');
let opened = 0, received = 0;
socket.addEventListener('open', function(e) {
    check(this === socket && e.target === socket && e.type === 'open', 'open EventTarget');
    check(socket.readyState === 1 && socket.protocol === 'chat.v1', 'open attributes');
    opened++;
    socket.send('Hello 世界');
    const view = new Uint8Array([99, 0, 255, 88]);
    socket.send(view.subarray(1, 3));
    view[1] = 42;
    socket.send(new Blob([new Uint8Array([7, 8])]));
    check(socket.bufferedAmount === 16, 'bufferedAmount includes UTF-8 and binary bytes');
});
socket.onmessage = function(e) {
    check(opened === 1 && this === socket && e instanceof MessageEvent, 'message ordering and receiver');
    check(e.origin === TEST_WS_URL.replace('/echo', ''), 'message origin uses normalized WebSocket URL');
    if (received === 0) {
        check(e.data === 'Hello 世界', 'text echo');
        socket.binaryType = 'arraybuffer';
    } else if (received === 1) {
        check(e.data instanceof ArrayBuffer, 'arraybuffer message');
        const bytes = new Uint8Array(e.data);
        check(bytes.length === 2 && bytes[0] === 0 && bytes[1] === 255, 'view snapshot and offset');
        socket.binaryType = 'blob';
    } else {
        check(e.data instanceof Blob && e.data.size === 2 && e.data.type === '', 'blob message');
        socket.send('drain-before-close');
        socket.close(3501, 'bye 世界');
        check(socket.readyState === 2, 'closing is synchronous');
    }
    received++;
};
socket.onerror = () => { throw new Error('unexpected WebSocket error'); };
socket.onclose = function(e) {
    check(socket.readyState === 3 && received === 3, 'closed after messages');
    check(e instanceof CloseEvent && e.code === 3501 && e.reason === 'bye 世界' && e.wasClean === true, 'clean closing handshake');
    check(socket.bufferedAmount === 0, 'queued sends flushed');
    socket.send('after');
    check(socket.bufferedAmount === 5, 'send after close accounts bytes');
    socket.close();
    console.log('WEBSOCKET_BROWSER_API_PASS');
};
