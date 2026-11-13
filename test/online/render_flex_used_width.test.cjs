const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb} = require('./render_fixture.cjs');
const {fixtures, fixtureHtml} = require('./flex_used_width_fixtures.cjs');
const render = renderFixtures('flex-used-width');

for (const [index, fixture] of fixtures.entries()) {
    test(fixture.name, async () => {
        const {image} = await render('flex-used-width-' + index, fixtureHtml(fixture));
        const height = fixture.rowHeight || 52;
        assert.deepEqual(rgb(image, 10, height + 4), [0, 255, 0], 'following band starts at the content-based row bottom');
        assert.deepEqual(rgb(image, (fixture.rowWidth || 600) - 100, height - 5), [0, 0, 255],
            'fixed neighbor aligns to the measured row bottom');
        assert.deepEqual(rgb(image, 10, height + 15), [255, 255, 255], 'row does not retain a stale preferred-width height');
    });
}
