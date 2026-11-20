const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb} = require('./render_fixture.cjs');
const render = renderFixtures('browser-globals');
const {cases, fixtureHtml} = require('./browser_global_fixtures.cjs');
for (const [name, setup, condition] of cases) {
    test('browser globals: ' + name, async () => {
        const {image} = await render(name.replaceAll(' ', '-'), fixtureHtml(setup, condition));
        assert.deepEqual(rgb(image, 10, 10), [0, 255, 0]);
    });
}
