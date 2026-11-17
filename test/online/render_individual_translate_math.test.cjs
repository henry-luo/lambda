const {test}=require('node:test');
const assert=require('node:assert/strict');
const {renderFixtures,rgb}=require('./render_fixture.cjs');
const {fixtures,fixtureHtml}=require('./individual_translate_math_fixtures.cjs');
const render=renderFixtures('individual-translate-math');
for (const [index,fixture] of fixtures.entries()) {
    test(fixture.name,async()=>{
        const {image}=await render('translate-'+index,fixtureHtml(fixture));
        const width=fixture.width||120;
        for (const [x,y] of [[fixture.x+2,fixture.y+2],[fixture.x+width-2,fixture.y+78]])
            assert.deepEqual(rgb(image,x,y),[0,128,0]);
        for (const [x,y] of [[fixture.x-2,fixture.y+40],[fixture.x+width+2,fixture.y+40],[fixture.x+40,fixture.y-2]])
            assert.deepEqual(rgb(image,x,y),[255,255,255]);
    });
}
