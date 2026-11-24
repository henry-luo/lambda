const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, loggedResult, rgb} = require('./render_fixture.cjs');
const {shadowPositionedCases, shadowPositionedHtml, shadowPositionedExpected} = require('./shadow_positioned_fixtures.cjs');
const render = renderFixtures('shadow-positioned');

for (const fixture of shadowPositionedCases) test('shadow containing block: ' + fixture.name, async () => {
    const {image, log} = await render(fixture.name, shadowPositionedHtml(fixture));
    const expected = shadowPositionedExpected(fixture);
    assert.deepEqual(loggedResult(log, 'SHADOW_POSITIONED'), expected);
    assert.deepEqual(rgb(image, expected.x + 10, expected.y + 20), [0,255,0]);
});
