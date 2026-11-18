const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures} = require('./render_fixture.cjs');
const {fixtures, fixtureHtml} = require('./intrinsic_image_basis_fixtures.cjs');
const render = renderFixtures('intrinsic-image-basis');

for (const fixture of fixtures) {
    test('intrinsic inline flow retains its own sizing basis: ' + fixture.name, async () => {
        const {log} = await render(fixture.name, fixtureHtml(fixture));
        const match = /INTRINSIC_IMAGE_HEIGHT\s+([\d.]+)/.exec(log);
        assert.ok(match, log);
        assert.ok(Math.abs(Number(match[1]) - fixture.expected) < 0.1,
            `${fixture.name}: got ${match[1]}, expected ${fixture.expected}`);
    });
}
