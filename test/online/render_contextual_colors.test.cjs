const {test}=require('node:test');
const assert=require('node:assert/strict');
const {renderFixtures,rgb}=require('./render_fixture.cjs');
const {fixtures,fixtureHtml}=require('./contextual_color_fixtures.cjs');
const render=renderFixtures('contextual-colors');
for(const [index,fixture]of fixtures.entries())test('contextual colors: '+fixture.name,async()=>{
    const {image}=await render('color-'+index,fixtureHtml(fixture));
    const actual=rgb(image,50,50);
    assert.ok(actual.every((channel,i)=>Math.abs(channel-fixture.expected[i])<=1),
        'got '+actual+'; expected '+fixture.expected);
    if(!fixture.paint||!fixture.paint.includes('gradient(')){
        const reflected=rgb(image,200,50);
        assert.ok(reflected.every((channel,i)=>Math.abs(channel-fixture.expected[i])<=1),
            'computed color paints '+reflected+'; expected '+fixture.expected);
    }
});
