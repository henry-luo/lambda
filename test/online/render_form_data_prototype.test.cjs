const {test}=require('node:test'),assert=require('node:assert/strict');
const {renderFixtures,loggedResult}=require('./render_fixture.cjs');
const {fixtures,fixtureHtml}=require('./form_data_prototype_fixtures.cjs');
const render=renderFixtures('form-data-prototype');
for(const fixture of fixtures)test(fixture.name,async()=>{
    const {log}=await render(fixture.name,fixtureHtml(fixture));
    assert.deepEqual(loggedResult(log,'FORM_DATA_PROTO_DETAILS'),fixture.expected);
});
