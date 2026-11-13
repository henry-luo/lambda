const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb} = require('./render_fixture.cjs');
const {fixtures, fixtureHtml} = require('./grid_flow_variable_fixtures.cjs');
const render = renderFixtures('grid-flow-variable');
for (const [index, fixture] of fixtures.entries()) {
    test(fixture.name, async () => {
        const {image} = await render('grid-flow-variable-' + index, fixtureHtml(fixture));
        assert.deepEqual(rgb(image, 20, 20), [255, 0, 0]);
        assert.deepEqual(rgb(image, fixture.column ? 100 : 20, fixture.column ? 20 : 100), [0, 0, 255]);
        assert.deepEqual(rgb(image, fixture.column ? 20 : 100, fixture.column ? 100 : 20), [255, 255, 255]);
    });
}
