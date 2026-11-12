const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb} = require('./render_fixture.cjs');
const {fixtures, fixtureHtml} = require('./grid_fixtures.cjs');

const render = renderFixtures('grid');
for (const [index, fixture] of fixtures.entries()) {
    test(fixture.name, async () => {
        const {image, log} = await render('grid-' + index, fixtureHtml(fixture));
        assert(log.includes('GRID_GEOMETRY ' + fixture.width + ' ' + fixture.peerX), log.slice(-2000));
        assert.deepEqual(rgb(image, 100, 40), [255, 0, 0], 'first item missing');
        assert.deepEqual(rgb(image, fixture.peerX + 10, 40), [0, 0, 255], 'second item pushed out of its area');
        assert.deepEqual(rgb(image, fixture.peerX - 10, 40), [255, 255, 255], 'item painted across the grid gap');
    });
}
