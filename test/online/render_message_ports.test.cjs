const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb, loggedResult} = require('./render_fixture.cjs');
const {messagePortCases, messagePortHtml} = require('./message_port_fixtures.cjs');
const render = renderFixtures('message-port');

for (const kind of messagePortCases) test('MessagePort payload: ' + kind, async () => {
    const {image, log} = await render(kind, messagePortHtml(kind));
    const result = loggedResult(log, 'MESSAGE_PORT_RESULT');
    assert.equal(result.okay, true, JSON.stringify(result));
    assert.deepEqual(rgb(image, 40, 40), [0, 128, 0]);
});
