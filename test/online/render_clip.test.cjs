const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb} = require('./render_fixture.cjs');
const {fixtures, fixtureHtml} = require('./clip_fixtures.cjs');

const render = renderFixtures('clip');

for (const [index, fixture] of fixtures.entries()) {
    test(fixture.name, async () => {
        const label = 'clip-' + index;
        const {image} = await render(label, fixtureHtml(fixture));
        for (const [x, y, color] of fixture.points) {
            assert.deepEqual(rgb(image, x, y), color === 'red' ? [255, 0, 0] : [0, 0, 255],
                fixture.clip + ' at ' + x + ',' + y);
        }
    });
}
