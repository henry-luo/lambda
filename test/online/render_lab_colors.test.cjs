const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb} = require('./render_fixture.cjs');
const {fixtures, fixtureHtml} = require('./lab_color_fixtures.cjs');
const render = renderFixtures('lab-colors');

for (const fixture of fixtures) {
    test('shared Lab color computation: ' + fixture.name, async () => {
        const {image} = await render(fixture.name, fixtureHtml(fixture));
        const pixel = rgb(image, 50, 50);
        assert.ok(pixel.every((value, i) => Math.abs(value - fixture.expected[i]) <= 1),
            `${fixture.name}: got ${pixel}, expected ${fixture.expected}`);
    });
}
