const {test}=require('node:test');
const assert=require('node:assert/strict');
const {renderFixtures,rgb}=require('./render_fixture.cjs');
const {fixtures,fixtureHtml}=require('./flex_ratio_constraint_fixtures.cjs');
const render=renderFixtures('flex-ratio-constraints');
for(const [index,fixture]of fixtures.entries())test('flex ratio constraints: '+fixture.name,async()=>{
    const {image,log}=await render('ratio-'+index,fixtureHtml(fixture));
    const match=/FLEX_RATIO_MEASURE (\[[^\n]+\])/.exec(log);
    assert.ok(match,log.slice(-2000));
    const actual=JSON.parse(match[1]);
    assert.ok(actual.every((value,i)=>Math.abs(value-fixture.expected[i])<=1),
        'got '+actual+'; expected '+fixture.expected);
    assert.deepEqual(rgb(image,fixture.expected[0]+Math.min(50,fixture.expected[2]/2),
        fixture.expected[1]+Math.min(50,fixture.expected[3]/2)),[0,255,0]);
});
