const {test}=require('node:test'),assert=require('node:assert/strict');
const {renderFixtures,loggedResult}=require('./render_fixture.cjs');
const {fixtures,fixtureHtml}=require('./element_name_fixtures.cjs');
const render=renderFixtures('element-name');
for(const [index,f]of fixtures.entries())test(f.name,async()=>{
 const {log}=await render('case-'+index,fixtureHtml(f));
 assert.deepEqual(loggedResult(log,'ELEMENT_NAME_DETAILS'),f.expected);
});
