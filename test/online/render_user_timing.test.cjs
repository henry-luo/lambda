const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, loggedResult, rgb} = require('./render_fixture.cjs');
const {userTimingCases, userTimingHtml} = require('./user_timing_fixtures.cjs');
const render = renderFixtures('user-timing');

for (const fixture of userTimingCases) {
    test('user timing: ' + fixture.name, async () => {
        const {image, log} = await render(fixture.name, userTimingHtml(fixture));
        assert.deepEqual(loggedResult(log, 'USER_TIMING_RESULT'), fixture.expected);
        assert.deepEqual(rgb(image, 40, 40), [0, 255, 0]);
    });
}
