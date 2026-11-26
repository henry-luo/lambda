const {test} = require('node:test');
const assert = require('node:assert/strict');
const {loggedResult, renderFixtures, rgb} = require('./render_fixture.cjs');
const {hostCascade, expectedHostCascade} = require('./shadow_host_fixtures.cjs');

const render = renderFixtures('shadow-host');

test('shadow host styles participate in the cascade and track live scope changes', async () => {
    const {log, image} = await render('host-cascade', hostCascade);
    assert.deepEqual(loggedResult(log, 'shadow host result'), expectedHostCascade);
    assert.deepEqual(rgb(image, 25, 45), [255, 165, 0]);
    assert.deepEqual(rgb(image, 60, 45), [0, 0, 255]);
});
