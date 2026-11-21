const {test}=require('node:test');
const assert=require('node:assert/strict');
const {renderFixtures,rgb}=require('./render_fixture.cjs');
const {cases,fixtureHtml}=require('./dialog_display_fixtures.cjs');
const render=renderFixtures('dialog-display');
for(const f of cases)test(f.name,async()=>{
 const {image}=await render(f.name.replaceAll(' ','-'),fixtureHtml(f));
 assert.deepEqual(rgb(image,10,10),f.visible?[255,0,0]:[0,255,0]);
});
