const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb} = require('./render_fixture.cjs');
const {fixtures, fixtureHtml} = require('./nested_ratio_height_fixtures.cjs');
const render = renderFixtures('nested-ratio-height');
for (const [index, fixture] of fixtures.entries()) {
    test(fixture.name, async () => {
        const {image} = await render('nested-ratio-height-' + index, fixtureHtml(fixture));
        assert.deepEqual(rgb(image, 260, fixture.height - 2), [255, 0, 0], 'button keeps its content height');
        assert.deepEqual(rgb(image, 20, fixture.height + 4), [0, 255, 255], 'next row follows the measured flex line');
        assert.deepEqual(rgb(image, 20, fixture.height + 14), [255, 255, 255]);
    });
}
