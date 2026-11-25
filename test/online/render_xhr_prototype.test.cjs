const {test}=require('node:test'),assert=require('node:assert/strict');
const {renderFixtures,loggedResult}=require('./render_fixture.cjs');
const {fixtures,fixtureHtml}=require('./xhr_prototype_fixtures.cjs');
const render=renderFixtures('xhr-prototype');
for(const fixture of fixtures)test(fixture.name,async()=>{
    const {log}=await render(fixture.name,fixtureHtml(fixture));
    assert.deepEqual(loggedResult(log,'XHR_PROTO_DETAILS'),fixture.expected);
});
