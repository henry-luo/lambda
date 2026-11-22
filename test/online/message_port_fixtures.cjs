const messagePortCases = ['property', 'listener', 'reentrant-gc', 'window'];

function exerciseMessagePort(kind) {
    const isWindow = kind === 'window', channel = isWindow ? null : new MessageChannel(), received = [];
    let payloadsIntact = new MessageEvent('probe').data === null &&
        new MessageEvent('probe', {data:undefined}).data === null;
    const expected = kind === 'reentrant-gc'
        ? ['start', 'tail', 'undefined', 'kept', 'end']
        : ['undefined', 'null', '0', 'false', '', 'end'];
    function send(value) {
        if (isWindow) window.postMessage(value, '*');
        else channel.port1.postMessage(value);
    }
    function receive(event) {
        const value = event.data;
        if (typeof gc === 'function') gc();
        if (isWindow) payloadsIntact = payloadsIntact && event.source === window;
        if (value && typeof value === 'object')
            payloadsIntact = payloadsIntact && value.values.length === 64 && value.values[63] === 'payload';
        received.push(value && typeof value === 'object' ? value.name : String(value));
        if (kind === 'reentrant-gc' && value === 'start') {
            send(undefined);
            send({name:'kept', values:Array(64).fill('payload')});
            send('end');
        }
        if (value === 'end') {
            const okay = payloadsIntact && JSON.stringify(received) === JSON.stringify(expected);
            console.log('MESSAGE_PORT_RESULT', JSON.stringify({received, okay}));
            document.getElementById('probe').style.background = okay ? 'green' : 'red';
            if (isWindow) removeEventListener('message', receive);
            else { channel.port1.close(); channel.port2.close(); }
        }
    }
    if (isWindow) addEventListener('message', receive);
    else if (kind === 'listener') {
        channel.port2.addEventListener('message', receive);
        channel.port2.start();
    } else channel.port2.onmessage = receive;
    const values = kind === 'reentrant-gc' ? ['start', 'tail'] : [undefined, null, 0, false, '', 'end'];
    values.forEach(send);
    if (typeof gc === 'function') gc();
}

function messagePortHtml(kind) {
    return '<!doctype html><style>body{margin:0}#probe{width:80px;height:80px;background:red}</style>' +
        '<div id="probe"></div><script>(' + exerciseMessagePort.toString() + ')(' +
        JSON.stringify(kind) + ');</script>';
}

module.exports = {messagePortCases, messagePortHtml};
