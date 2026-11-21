const {test}=require('node:test');
const assert=require('node:assert/strict');
const {renderFixtures,loggedResult}=require('./render_fixture.cjs');
const {fixtures,fixtureHtml}=require('./namespace_lookup_fixtures.cjs');
const render=renderFixtures('namespace-lookup');
for(const [index,f]of fixtures.entries())test(f.name,async()=>{
    const {log}=await render('case-'+index,fixtureHtml(f));
    assert.deepEqual(loggedResult(log,'NAMESPACE_LOOKUP_RESULT'),f.expected);
});
