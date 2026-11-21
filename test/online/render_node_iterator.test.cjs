const {test}=require('node:test');
const assert=require('node:assert/strict');
const {renderFixtures,rgb}=require('./render_fixture.cjs');
const {cases,fixtureHtml}=require('./node_iterator_fixtures.cjs');
const render=renderFixtures('node-iterator');
for(const [name,source]of cases)test('NodeIterator: '+name,async()=>{
 const {image}=await render(name.replaceAll(' ','-'),fixtureHtml(source));
 assert.deepEqual(rgb(image,10,10),[0,255,0]);
});
