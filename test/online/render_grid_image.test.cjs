const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb} = require('./render_fixture.cjs');
const {fixtures, fixtureHtml} = require('./grid_image_fixtures.cjs');
const render = renderFixtures('grid-image');

for (const fixture of fixtures) {
    test('grid image paints: ' + fixture.name, async () => {
        const {image} = await render(fixture.name, fixtureHtml(fixture));
        assert.deepEqual(rgb(image, 40, 30), [0, 255, 0]);
        assert.deepEqual(rgb(image, 120, 30), [255, 0, 0]);
        assert.deepEqual(rgb(image, 120, 70), [255, 0, 255]);
    });
}
