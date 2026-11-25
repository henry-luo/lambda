const {test}=require('node:test'),assert=require('node:assert/strict');
const {renderFixtures,rgb}=require('./render_fixture.cjs');
const {cases,fixtureHtml}=require('./service_worker_events_fixtures.cjs');
const render=renderFixtures('service-worker-events');
for(const[name,source]of cases)test(name,async()=>{
 const {image,log}=await render(name.replaceAll(' ','-'),fixtureHtml(source));
 assert.deepEqual(rgb(image,10,10),[0,255,0],log.slice(-2200));
});
