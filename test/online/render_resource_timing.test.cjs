const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb, loggedResult, serveFixtureResources} = require('./render_fixture.cjs');
const {resourceTimingCases, resourceTimingHtml, serveResourceTiming} = require('./resource_timing_fixtures.cjs');
const render = renderFixtures('resource-timing');
const origin = serveFixtureResources(serveResourceTiming);

for (const fixture of resourceTimingCases) {
    test('resource delivery shares the capture window: ' + fixture.name, async () => {
        const {image, log} = await render(fixture.name, resourceTimingHtml(fixture, origin()), {
            ...process.env, LAMBDA_POST_LOAD_SETTLE_MS:String(fixture.settle),
        });
        const birth = loggedResult(log, 'RESOURCE_TIMING_RESULT').birth;
        if (fixture.settle === 1000) {
            assert.ok(birth >= (fixture.start || 0) + (fixture.timer || 0), 'causal timers keep their deadlines');
            assert.ok(birth < 700, 'native completion arrives before selection of the final frame');
            assert.deepEqual(rgb(image, 40, 40), [0, 255, 0], 'the resource-created fade has time to finish');
        } else {
            assert.ok(Math.abs(birth - (fixture.settle ? fixture.settle + 1000 / 60 : 0)) < 0.01,
                'completion after the window keeps the selected frame frozen');
            assert.deepEqual(rgb(image, 40, 40), [255, 255, 255]);
        }
        assert.deepEqual(rgb(image, 140, 40), [255, 255, 255], 'later timers remain outside the window');
    });
}
