const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb} = require('./render_fixture.cjs');
const {fixtures, fixtureHtml, checkEnvironmentValue} = require('./environment_value_fixtures.cjs');
const render = renderFixtures('environment-value');
for (const [index, fixture] of fixtures.entries()) {
    test(fixture.name, async () => {
        const {image} = await render('environment-value-' + index, fixtureHtml(fixture));
        checkEnvironmentValue(assert, fixture, image, rgb);
    });
}
