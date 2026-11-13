const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb} = require('./render_fixture.cjs');
const {fixtures, fixtureHtml} = require('./grid_absolute_fixtures.cjs');

const render = renderFixtures('grid-absolute');
for (const [index, fixture] of fixtures.entries()) {
    test(fixture.name, async () => {
        const {image, log} = await render('grid-absolute-' + index, fixtureHtml(fixture));
        const [x, y, width, height] = fixture.box;
        const d = fixture.decoration || 0;
        const expected = [x, y, width, height, x + d, y + d, width - d * 2, height - d * 2];
        assert(log.includes('GRID_ABSOLUTE_GEOMETRY ' + JSON.stringify(expected)), log.slice(-2500));
        assert.deepEqual(rgb(image, x + d + 2, y + d + 2), [0, 255, 0]);
        assert.deepEqual(rgb(image, x + width - d - 2, y + height - d - 2), [0, 255, 0]);
        if (x + width + 2 < image.width) {
            const outside = rgb(image, x + width + 2, y + height / 2);
            assert.notDeepEqual(outside, [0, 255, 0]);
        }
    });
}
