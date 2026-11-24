const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, serveFixtureResources, rgb} = require('./render_fixture.cjs');
const {locationNavigationCases, locationNavigationResources,
    locationNavigationExpected, locationNavigationPath} = require('./location_navigation_fixtures.cjs');
const render = renderFixtures('location-navigation'), counts = {};
const origin = serveFixtureResources(locationNavigationResources(counts));

for (const {name} of locationNavigationCases) test('Location navigation: ' + name, async () => {
    const {image} = await render(name, {url:origin() + locationNavigationPath(name)});
    assert.deepEqual(rgb(image,600,400), [0,255,0]);
    // request counts distinguish real loads from URL-only state changes and accidental reloads.
    assert.deepEqual(counts[name], locationNavigationExpected(name));
});
