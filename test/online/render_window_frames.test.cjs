const {test}=require('node:test'),assert=require('node:assert/strict');
const {renderFixtures,loggedResult}=require('./render_fixture.cjs');
const {fixtures,fixtureHtml}=require('./window_frame_fixtures.cjs');
const render=renderFixtures('window-frames');
for(const fixture of fixtures)test(fixture.name,async()=>{
    const {log}=await render(fixture.name,fixtureHtml(fixture));
    assert.deepEqual(loggedResult(log,'WINDOW_FRAME_DETAILS'),fixture.expected);
});
