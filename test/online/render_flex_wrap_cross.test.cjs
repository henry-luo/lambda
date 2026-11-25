const {test}=require('node:test'),assert=require('node:assert/strict');
const {renderFixtures,loggedResult}=require('./render_fixture.cjs');
const {fixtures,fixtureHtml}=require('./flex_wrap_cross_fixtures.cjs');
const render=renderFixtures('flex-wrap-cross');
for(const fixture of fixtures)test(fixture.name,async()=>{
 const {log}=await render(fixture.name,fixtureHtml(fixture));
 assert.deepEqual(loggedResult(log,'FLEX_WRAP_CROSS_RESULT'),fixture.expected);
});
