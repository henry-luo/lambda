const {test}=require('node:test');
const assert=require('node:assert/strict');
const {renderFixtures}=require('./render_fixture.cjs');
const {fixtures,fixtureHtml,shadowInk}=require('./text_shadow_inheritance_fixtures.cjs');
const render=renderFixtures('text-shadow-inheritance');
for(const [index,fixture] of fixtures.entries()){
    test('text-shadow inheritance: '+fixture.name,async()=>{
        const {image}=await render('shadow-'+index,fixtureHtml(fixture));
        const ink=shadowInk(image);
        if(fixture.absent)assert.equal(ink.count,0,'a local reset or parent removal suppresses the inherited shadow');
        else assert.ok(ink.count>100,`inherited shadow must paint: ${ink.count} red pixels`);
        if(fixture.minX)assert.ok(ink.minX>=fixture.minX[0]&&ink.minX<=fixture.minX[1],
            `offsets use the parent computed font, found x=${ink.minX}`);
    });
}
