const {test}=require('node:test'),assert=require('node:assert/strict');
const {renderFixtures,loggedResult}=require('./render_fixture.cjs');
const {fixtures,fixtureHtml}=require('./canvas_readback_fixtures.cjs');
const render=renderFixtures('canvas-readback');
for(const fixture of fixtures)test(fixture.name,async()=>{
    const {log}=await render(fixture.name,fixtureHtml(fixture));
    assert.deepEqual(loggedResult(log,'CANVAS_READBACK_DETAILS'),fixture.expected);
});
