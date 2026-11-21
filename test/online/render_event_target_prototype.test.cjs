const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb, loggedResult} = require('./render_fixture.cjs');
const {eventTargetPrototypeCases, eventTargetPrototypeHtml} = require('./event_target_prototype_fixtures.cjs');
const render = renderFixtures('event-target-prototype');

for (const kind of eventTargetPrototypeCases) {
    test('EventTarget prototype: ' + kind, async () => {
        const {image, log} = await render(kind, eventTargetPrototypeHtml(kind));
        assert.deepEqual(loggedResult(log, 'EVENT_TARGET_PROTOTYPE_RESULT'), {failures:[]});
        assert.deepEqual(rgb(image, 40, 40), [0, 128, 0]);
    });
}
