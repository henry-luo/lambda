const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb} = require('./render_fixture.cjs');
const {fixtures, fixtureHtml, witnesses} = require('./video_percentage_fixtures.cjs');
const render = renderFixtures('video-percentage');

for (const [index, fixture] of fixtures.entries()) {
    test(fixture.name, async () => {
        const {image} = await render('video-percentage-' + index, fixtureHtml(fixture));
        for (const [x, y, expected] of witnesses(fixture)) {
            assert.deepEqual(rgb(image, x, y), expected, JSON.stringify({x, y, expected}));
        }
    });
}
