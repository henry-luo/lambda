const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb} = require('./render_fixture.cjs');
const render = renderFixtures('svg-image-motion');
const {cases, fixtureHtml} = require('./svg_image_motion_fixtures.cjs');
for (const fixture of cases) {
    test('SVG image motion: ' + fixture.name, async () => {
        const {image} = await render(fixture.name.replaceAll(' ', '-'), fixtureHtml(fixture),
            {...process.env, LAMBDA_POST_LOAD_SETTLE_MS: '1000'});
        const actual = rgb(image, 50, 50);
        assert.ok(actual.every((value, i) => Math.abs(value - fixture.expected[i]) <= 1),
            `selected image frame: ${actual}, expected ${fixture.expected}`);
    });
}
