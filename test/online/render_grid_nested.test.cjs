const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb} = require('./render_fixture.cjs');
const {fixtures, fixtureHtml} = require('./grid_nested_fixtures.cjs');

const render = renderFixtures('grid-nested');
for (const [index, fixture] of fixtures.entries()) {
    test(fixture.name, async () => {
        const {image, log} = await render('grid-nested-' + index, fixtureHtml(fixture));
        assert(log.includes('NESTED_GRID_GEOMETRY ' + JSON.stringify([
            fixture.height, fixture.parentHeight, fixture.y])), log.slice(-2500));
        assert.deepEqual(rgb(image, 10, fixture.y + fixture.height - 4), [255, 255, 0]);
        assert.deepEqual(rgb(image, 30, fixture.y + 20), [255, 0, 0]);
        assert.deepEqual(rgb(image, 30, fixture.y + 70), [0, 0, 255]);
    });
}
