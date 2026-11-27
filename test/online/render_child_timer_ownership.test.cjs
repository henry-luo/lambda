const {test} = require('node:test');
const assert = require('node:assert/strict');
const {loggedResult, renderFixtures, rgb, serveFixtureResources} = require('./render_fixture.cjs');

const render = renderFixtures('child-timer-ownership');
const origin = serveFixtureResources((req, res) => {
    if (req.url === '/held.js') {
        setTimeout(() => res.writeHead(200, {'Content-Type': 'text/javascript'}).end(
            'document.body.style.background = "blue"'), 1200);
    } else if (req.url === '/frame') {
        res.writeHead(200, {'Content-Type': 'text/html'}).end(
            '<!doctype html><body style="margin:0;background:red"><script src="/held.js"></script>');
    } else {
        res.writeHead(200, {'Content-Type': 'text/html'}).end(`<!doctype html>
            <body style="margin:0"><div id="parent" style="width:40px;height:40px;background:red"></div>
            <script>
                const calls = [];
                const marker = document.getElementById('parent');
                const canceled = setTimeout(() => { marker.style.background = 'red'; }, 800);
                setTimeout(() => {
                    clearTimeout(canceled);
                    calls.push('clear');
                }, 150);
                setTimeout(() => {
                    marker.style.background = 'lime';
                    calls.push('timeout');
                }, 250);
                const interval = setInterval(() => {
                    clearInterval(interval);
                    if (calls.includes('interval')) marker.style.background = 'red';
                    calls.push('interval');
                    console.log('frame timer ownership ' + JSON.stringify({calls}));
                }, 350);
            </script>
            <iframe style="display:block;border:0" src="/frame"></iframe>`);
    }
});

test('ready parent timers survive a blocked child document and remain cancellable', async () => {
    const {image, log} = await render('child-load', {url: origin() + '/'}, {...process.env,
        LAMBDA_POST_LOAD_SETTLE_MS: '1000', LAMBDA_CAPTURE_LOAD_CLOCK: 'elapsed'});
    assert.deepEqual(rgb(image, 20, 20), [0, 255, 0], 'parent timeout paint');
    assert.deepEqual(rgb(image, 20, 60), [0, 0, 255], 'child loaded and painted');
    assert.deepEqual(loggedResult(log, 'frame timer ownership'), {calls:['clear', 'timeout', 'interval']});
    assert.doesNotMatch(log, /timer fired without captured JS runtime/);
});
