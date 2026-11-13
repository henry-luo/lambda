const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb} = require('./render_fixture.cjs');
const {fixtures, fixtureHtml, witnesses} = require('./positioned_order_fixtures.cjs');
const render = renderFixtures('positioned-order');

for (const [index, fixture] of fixtures.entries()) {
    test(fixture.name, async () => {
        const {image} = await render('positioned-order-' + index, fixtureHtml(fixture));
        for (const [x, y, expected] of witnesses(fixture)) {
            const actual = rgb(image, x, y);
            assert.ok(actual.every((value, channel) => Math.abs(value - expected[channel]) <= 1),
                JSON.stringify({x, y, actual, expected}));
        }
    });
}
