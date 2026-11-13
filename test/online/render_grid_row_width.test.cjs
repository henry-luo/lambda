const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb} = require('./render_fixture.cjs');
const {fixtures, fixtureHtml} = require('./grid_row_width_fixtures.cjs');
const render = renderFixtures('grid-row-width');

for (const [index, fixture] of fixtures.entries()) {
    test(fixture.name, async () => {
        const {image} = await render('grid-row-width-' + index, fixtureHtml(fixture));
        const row = fixture.rowHeight || 200;
        const section = fixture.flex ? 400 : row;
        const offset = fixture.flex ? (400 - row) / 2 : 0;
        assert.deepEqual(rgb(image, 170, offset + 5), [0, 0, 255], 'content begins at the centered row');
        assert.deepEqual(rgb(image, 10, section + 4), [0, 255, 0], 'following band uses the measured grid row');
        assert.deepEqual(rgb(image, 10, section + 15), [255, 255, 255], 'no provisional whole-grid height remains');
    });
}
