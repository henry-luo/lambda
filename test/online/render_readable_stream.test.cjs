const {test}=require('node:test');
const assert=require('node:assert/strict');
const {renderFixtures,rgb}=require('./render_fixture.cjs');
const {cases,fixtureHtml}=require('./readable_stream_fixtures.cjs');
const render=renderFixtures('readable-stream');
for(const[name,source]of cases)test('readable stream: '+name,async()=>{
 const {image,log}=await render(name.replaceAll(' ','-'),fixtureHtml(source));
 assert.deepEqual(rgb(image,10,10),[0,255,0],log.slice(-2000));
});
