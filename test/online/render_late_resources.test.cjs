const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb, loggedResult, serveFixtureResources} = require('./render_fixture.cjs');
const {lateResourceCases, lateResourceHtml, serveLateResource} = require('./late_resource_fixtures.cjs');
const render = renderFixtures('late-resources');
const origin = serveFixtureResources(serveLateResource);

for (const mode of lateResourceCases) {
    test('snapshot resource completion: ' + mode, async () => {
        const settle = mode === 'zero-window' ? '0' : '1000';
        const {image, log} = await render(mode, lateResourceHtml(mode, origin()), {
            ...process.env, LAMBDA_POST_LOAD_SETTLE_MS:settle,
        });
        assert.deepEqual(rgb(image, 40, 40), [0, 128, 0], 'completion must reach the DOM');
        assert.deepEqual(rgb(image, 140, 40), mode === 'failed' || mode === 'detached'
            ? [255, 255, 255] : [0, 0, 255]);
        if (mode !== 'plain') assert.deepEqual(loggedResult(log, 'LATE_RESOURCE_RESULT'), {
            loads:mode === 'failed' ? 0 : mode === 'chain' ? 2 : 1,
            errors:mode === 'failed' ? 1 : 0,
            decoded:mode === 'blocked-sibling' ? 0 : 1,
            complete:true, width:mode === 'failed' ? 0 : 80, frameHeld:true,
        });
    });
}
