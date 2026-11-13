const {test}=require('node:test');
const assert=require('node:assert/strict');
const {renderFixtures,rgb}=require('./render_fixture.cjs');
const {fixtures,fixtureHtml}=require('./root_body_overflow_fixtures.cjs');
const render=renderFixtures('root-body-overflow');
for (const [index,fixture] of fixtures.entries()) {
    test(fixture.name,async()=>{
        const {image}=await render('root-body-overflow-'+index,fixtureHtml(fixture));
        assert.deepEqual(rgb(image,40,40),fixture.expected);
        assert.deepEqual(rgb(image,5,5),[255,255,255]);
    });
}
