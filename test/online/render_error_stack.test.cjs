const {test}=require('node:test');
const assert=require('node:assert/strict');
const {renderFixtures,rgb}=require('./render_fixture.cjs');
const {cases,fixtureHtml}=require('./error_stack_fixtures.cjs');
const render=renderFixtures('error-stack');
for(const[name,source]of cases)test('error stack: '+name,async()=>{
 const {image,log}=await render(name.replaceAll(' ','-'),fixtureHtml(source));
 assert.deepEqual(rgb(image,10,10),[0,255,0],log.slice(-1800));
 if(name==='lazy subclass names and collection')assert.match(log,/ERROR_STACK_COLLECTIONS 1/);
});
