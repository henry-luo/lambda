const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb, loggedResult} = require('./render_fixture.cjs');
const {rangeFragmentCases, rangeFragmentHtml} = require('./range_fragment_fixtures.cjs');
const render = renderFixtures('range-fragment');

for (const kind of rangeFragmentCases) test('Range contextual fragment: ' + kind, async () => {
    const {image, log} = await render(kind, rangeFragmentHtml(kind));
    assert.deepEqual(loggedResult(log, 'RANGE_FRAGMENT_RESULT'), {failures:[]});
    assert.deepEqual(rgb(image, 40, 40), [0, 128, 0]);
    if (kind === 'scripting') assert.deepEqual(rgb(image, 120, 40), [0, 0, 255]);
});
