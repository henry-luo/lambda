const {test}=require('node:test'),assert=require('node:assert/strict');
const {renderFixtures,rgb}=require('./render_fixture.cjs');
const {cases,fixtureHtml}=require('./selector_root_fixtures.cjs');
const render=renderFixtures('selector-roots');
for(const[name,source]of cases)test('selector roots: '+name,async()=>{
 const {image,log}=await render(name.replaceAll(' ','-'),fixtureHtml(source));
 assert.deepEqual(rgb(image,10,10),[0,255,0],log.slice(-2500));
});
