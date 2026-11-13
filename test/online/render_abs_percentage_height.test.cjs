const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb} = require('./render_fixture.cjs');
const {fixtures, fixtureHtml} = require('./abs_percentage_height_fixtures.cjs');
const render = renderFixtures('abs-percentage-height');
for (const [index, fixture] of fixtures.entries()) {
    test(fixture.name, async () => {
        const {image} = await render('abs-percentage-height-' + index, fixtureHtml(fixture));
        assert.deepEqual(rgb(image, 20, fixture.height - 2), [255, 0, 0]);
        assert.deepEqual(rgb(image, 20, fixture.height + 2), [255, 255, 255]);
    });
}
