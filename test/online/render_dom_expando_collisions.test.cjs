const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb, loggedResult} = require('./render_fixture.cjs');
const {domExpandoCollisionCases, domExpandoCollisionHtml} = require('./dom_expando_collisions_fixtures.cjs');
const render = renderFixtures('dom-expando-collisions');

for (const mode of domExpandoCollisionCases) {
    test('DOM element fields: ' + mode, async () => {
        const {image, log} = await render(mode, domExpandoCollisionHtml(mode));
        assert.deepEqual(loggedResult(log, 'DOM_EXPANDO_COLLISION_RESULT'), {failures:[], clicks:1});
        assert.deepEqual(rgb(image, 40, 40), [0, 128, 0]);
    });
}
