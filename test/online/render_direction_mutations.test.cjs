const {test}=require('node:test'),assert=require('node:assert/strict');
const {renderFixtures,rgb,loggedResult}=require('./render_fixture.cjs');
const {cases,fixtureHtml}=require('./direction_mutation_fixtures.cjs');
const render=renderFixtures('direction-mutations');
for(const fixture of cases)test('direction mutations: '+fixture.name,async()=>{
 const {image,log}=await render(fixture.name.replaceAll(' ','-'),fixtureHtml(fixture),
  {...process.env,LAYOUT_DEBUG:'cache'});
 assert.deepEqual(loggedResult(log,'DIRECTION_MUTATION').widths,
  fixture.broad?[200,100,200]:[100,100,100]);
 assert.deepEqual(rgb(image,10,10),[0,255,0],log.slice(-1800));
 if(!fixture.broad)assert.doesNotMatch(log,/mutation-fallback: reason=direction-sensitive-selector/,
  'text with fixed direction must not force a direction-dependent document cascade');
});
