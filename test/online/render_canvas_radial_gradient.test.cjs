const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, loggedResult} = require('./render_fixture.cjs');
const {fixtures, fixtureHtml} = require('./canvas_radial_gradient_fixtures.cjs');

const render = renderFixtures('canvas-radial-gradient');
for (const [index, fixture] of fixtures.entries()) {
    test(fixture.name, async () => {
        const {log} = await render('case-' + index, fixtureHtml(fixture));
        assert.deepEqual(loggedResult(log, 'CANVAS_RADIAL_DETAILS'), fixture.expected);
    });
}
