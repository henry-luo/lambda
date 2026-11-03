function check(value, label) { if (!value) throw new Error(label); }
let completed = 0;
function done() { if (++completed === 4) console.log('WEBSOCKET_BROWSER_FAILURES_PASS'); }
function failure(socket, label, opens = false) {
    const events = [];
    socket.onopen = () => {check(opens, label + ' unexpectedly opened'); events.push('open');};
    socket.onerror = e => {check(e.type === 'error' && e.target === socket && socket.readyState === 3, label + ' error'); events.push('error');};
    socket.onclose = e => {
        check(events.join(',') === (opens ? 'open,error' : 'error'), label + ' error before close');
        check(socket.readyState === 3 && e instanceof CloseEvent, label + ' CloseEvent');
        check(e.code === 1006 && e.reason === '' && !e.wasClean, label + ' abnormal close');
        done();
    };
}
const early = new WebSocket(TEST_WS_URL);
failure(early, 'close while connecting');
early.close();
check(early.readyState === 2, 'early close enters CLOSING');
failure(new WebSocket(TEST_WS_URL + '/missing-protocol', ['missing']), 'missing protocol');
failure(new WebSocket(TEST_WS_URL + '/abort'), 'abrupt disconnect', true);
const socket = new WebSocket(TEST_WS_URL, ['chat.v1']);
socket.onopen = () => { socket.close(); };
socket.onclose = e => {
    check(e instanceof CloseEvent && e instanceof Event, 'CloseEvent inheritance');
    check(e.code === 1005 && e.reason === '' && e.wasClean, 'default close has empty body');
    check(socket.readyState === 3, 'default close complete');
    done();
};
const event = new CloseEvent('close', {code:3500, reason:'sample', wasClean:true});
check(event.code === 3500 && event.reason === 'sample' && event.wasClean, 'CloseEvent constructor');
check(!event.bubbles && !event.cancelable && !event.isTrusted, 'CloseEvent defaults');
check(new CloseEvent('close', {reason:'\ud800'}).reason === '\ufffd', 'USVString reason replaces unpaired surrogate');
