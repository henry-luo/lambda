const {test}=require('node:test');
const assert=require('node:assert/strict');
const {renderFixtures,rgb}=require('./render_fixture.cjs');
const {fixtures,fixtureHtml}=require('./flex_omitted_basis_fixtures.cjs');
const render=renderFixtures('flex-omitted-basis');
for (const [index,fixture] of fixtures.entries()) {
    test(fixture.name,async()=>{
        const {image}=await render('basis-'+index,fixtureHtml(fixture));
        assert.deepEqual(rgb(image,30,fixture.height+10),[255,255,0]);
        assert.deepEqual(rgb(image,30,30),fixture.height>40?[0,0,255]:[0,128,0]);
    });
}
