const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb} = require('./render_fixture.cjs');
const {fixtures, fixtureHtml} = require('./flex_percentage_fixtures.cjs');
const render = renderFixtures('flex-percentage');

for (const [index, fixture] of fixtures.entries()) {
    test(fixture.name, async () => {
        const {image} = await render('flex-percentage-' + index, fixtureHtml(fixture));
        assert.deepEqual(rgb(image, 40, 40), [0, 0, 255], 'fixed icon stays visible');
        assert.deepEqual(rgb(image, 200, 79), [255, 0, 0], 'text area keeps its flex share');
        assert.deepEqual(rgb(image, 300, 79), fixture.overflow ? [255, 0, 0] : [255, 255, 255],
            'only authored nonwrapping or fixed minima overflow the outer row');
    });
}
