const {test}=require('node:test');
const assert=require('node:assert/strict');
const {renderFixtures,loggedResult,rgb,serveFixtureResources}=require('./render_fixture.cjs');
const {xhrJsonCases,serveXhrJson,xhrJsonHtml,xhrJsonExpected}=require('./xhr_json_fixtures.cjs');
const render=renderFixtures('xhr-json'),origin=serveFixtureResources(serveXhrJson);

for(const fixture of xhrJsonCases)test('XHR JSON response: '+fixture.name,async()=>{
    const {image,log}=await render(fixture.name,xhrJsonHtml(fixture,origin()));
    assert.deepEqual(loggedResult(log,'XHR_JSON_RESULT'),xhrJsonExpected(fixture));
    assert.deepEqual(rgb(image,40,40),[0,255,0]);
});
