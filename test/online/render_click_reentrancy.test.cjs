const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb, loggedResult} = require('./render_fixture.cjs');
const {clickReentrancyCases, clickReentrancyHtml} = require('./click_reentrancy_fixtures.cjs');
const render = renderFixtures('click-reentry');

for (const fixture of clickReentrancyCases) {
    test('click reentry: ' + fixture.name, async () => {
        const {image, log} = await render(fixture.name, clickReentrancyHtml(fixture.name));
        assert.deepEqual(loggedResult(log, 'CLICK_REENTRY_RESULT'),
            {count:fixture.count, other:fixture.other});
        assert.deepEqual(rgb(image, 40, 40), [0, 128, 0]);
    });
}
