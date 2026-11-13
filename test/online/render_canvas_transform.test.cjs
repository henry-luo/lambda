const {test}=require('node:test'),assert=require('node:assert/strict');
const {renderFixtures,rgb}=require('./render_fixture.cjs');
const {fixtures,fixtureHtml}=require('./canvas_transform_fixtures.cjs');
const render=renderFixtures('canvas-transform');
for(const [index,f]of fixtures.entries())test(f.name,async()=>{const {image,log}=await render('matrix-'+index,fixtureHtml(f));if(f.log)assert(log.includes(f.log),log.slice(-1600));for(const [x,y,color]of f.pixels)assert.deepEqual(rgb(image,x,y),color,`${f.name}: ${x},${y}`);});
