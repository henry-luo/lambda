const {test}=require('node:test');
const assert=require('node:assert/strict');
const {renderFixtures}=require('./render_fixture.cjs');
const {fixtures,fixtureHtml,differingPixels}=require('./flex_contents_position_fixtures.cjs');
const render=renderFixtures('flex-contents-position');
for(const [index,fixture]of fixtures.entries()){
    test('boxless flex item positioning: '+fixture.name,async()=>{
        const {image}=await render('contents-'+index,fixtureHtml(fixture));
        assert.equal(differingPixels(image),0,'flattening a boxless wrapper preserves the flex item paint coordinates');
    });
}
