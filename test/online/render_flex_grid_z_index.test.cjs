const {test}=require('node:test');
const assert=require('node:assert/strict');
const {renderFixtures,rgb}=require('./render_fixture.cjs');
const {fixtures,fixtureHtml}=require('./flex_grid_z_index_fixtures.cjs');
const render=renderFixtures('flex-grid-z-index');
for(const [index,fixture] of fixtures.entries()) {
    test(fixture.name,async()=>{
        const {image}=await render('stack-'+index,fixtureHtml(fixture));
        assert.deepEqual(rgb(image,40,40),fixture.expected);
    });
}
