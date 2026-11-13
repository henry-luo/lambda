const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb} = require('./render_fixture.cjs');
const {fixtures, fixtureHtml} = require('./compact_condition_fixtures.cjs');
const render = renderFixtures('compact-conditions');
for (const [index, fixture] of fixtures.entries()) {
    test(fixture.name, async () => {
        const {image} = await render('compact-condition-' + index, fixtureHtml(fixture));
        assert.deepEqual(rgb(image, 20, 20), fixture.matches ? [0, 255, 0] : [255, 0, 0]);
        assert.deepEqual(rgb(image, 100, 20), [255, 255, 255]);
    });
}
