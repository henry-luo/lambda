const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb} = require('./render_fixture.cjs');
const {fixtures, fixtureHtml} = require('./nested_pseudo_fixtures.cjs');
const render = renderFixtures('nested-pseudo');
for (const [index, fixture] of fixtures.entries()) {
    test(fixture.name, async () => {
        const {image} = await render('nested-pseudo-' + index, fixtureHtml(fixture));
        assert.deepEqual(rgb(image, 10, 10), [255, 0, 0], 'following media rule still applies');
        assert.deepEqual(rgb(image, 160, 10), [255, 255, 255], 'following media width still applies');
    });
}
