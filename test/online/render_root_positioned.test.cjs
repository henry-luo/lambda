const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb} = require('./render_fixture.cjs');
const {rootPositionedCases, rootPositionedHtml, rootPositionedExpected} = require('./root_positioned_fixtures.cjs');
const render = renderFixtures('root-positioned');

for (const mode of rootPositionedCases) test('root positioned reflow: ' + mode, async () => {
    const {image} = await render(mode, rootPositionedHtml(mode));
    // the footer follows its containing block after growth; viewport-based controls stay visible.
    assert.deepEqual(rgb(image, 10, 750), rootPositionedExpected(mode));
});
