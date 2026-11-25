const {test}=require('node:test'),assert=require('node:assert/strict');
const {renderFixtures,loggedResult}=require('./render_fixture.cjs');
const {fixtures,fixtureHtml}=require('./number_compact_fixtures.cjs');
const render=renderFixtures('number-compact');
for(const fixture of fixtures)test(fixture.name,async()=>{
    const {log}=await render(fixture.name,fixtureHtml(fixture));
    assert.deepEqual(loggedResult(log,'NUMBER_COMPACT_DETAILS'),fixture.expected);
});
