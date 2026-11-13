const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb} = require('./render_fixture.cjs');
const {fixtures, fixtureHtml} = require('./flex_inline_break_fixtures.cjs');
const render = renderFixtures('flex-inline-break');

for (const [index, fixture] of fixtures.entries()) {
    test(fixture.name, async () => {
        const {image} = await render('flex-inline-break-' + index, fixtureHtml(fixture));
        if (fixture.centered) {
            assert.deepEqual(rgb(image, 10, 310), [255, 0, 0], 'headline keeps its centered row');
            assert.deepEqual(rgb(image, 560, 470), [0, 255, 0], 'action follows the full heading');
            return;
        }
        assert.deepEqual(rgb(image, 30, 300), [0, 255, 0], 'action remains inside the hero');
        assert.deepEqual(rgb(image, 30, 370), [255, 255, 255], 'action does not overflow below the hero');
        assert.deepEqual(rgb(image, 500, 300), [0, 0, 255], 'adjacent card shares the bottom alignment');
        assert.deepEqual(rgb(image, 500, 350), [238, 238, 238], 'card does not overflow its row');
    });
}
