const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb} = require('./render_fixture.cjs');
const {fixtures, fixtureHtml} = require('./intrinsic_width_value_fixtures.cjs');
const render = renderFixtures('intrinsic-width-values');

for (const [index, fixture] of fixtures.entries()) {
    test(fixture.name, async () => {
        const {image} = await render('intrinsic-width-value-' + index, fixtureHtml(fixture));
        const height = fixture.cardHeight || 240;
        assert.deepEqual(rgb(image, 300, 50), [255, 0, 0], 'the image retains its selected box');
        assert.deepEqual(rgb(image, 10, height + 4), [0, 255, 0], 'enclosing height uses the resolved child width');
        assert.deepEqual(rgb(image, 10, height + 15), [255, 255, 255], 'no preferred-width overestimate remains');
    });
}
