const {test}=require('node:test'),assert=require('node:assert/strict');
const {renderFixtures,rgb}=require('./render_fixture.cjs');
const {cases,fixtureHtml}=require('./readable_tee_fixtures.cjs');
const render=renderFixtures('readable-tee');
for(const[name,source]of cases)test('readable tee: '+name,async()=>{
 const {image,log}=await render(name.replaceAll(' ','-'),fixtureHtml(source));
 assert.deepEqual(rgb(image,10,10),[0,255,0],log.slice(-2200));
});
