const {test}=require('node:test');
const assert=require('node:assert/strict');
const {renderFixtures,rgb}=require('./render_fixture.cjs');
const {cases,fixtureHtml}=require('./url_live_fixtures.cjs');
const render=renderFixtures('url-live');
for(const[name,source]of cases)test('live URL: '+name,async()=>{
 const {image}=await render(name.replaceAll(' ','-'),fixtureHtml(source));
 assert.deepEqual(rgb(image,10,10),[0,255,0]);
});
