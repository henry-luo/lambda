const {test}=require('node:test');
const assert=require('node:assert/strict');
const {renderFixtures}=require('./render_fixture.cjs');
const render=renderFixtures('rounded-clip-text');
const {fixtures,fixtureHtml}=require('./rounded_clip_text_fixtures.cjs');
for(const [index,f] of fixtures.entries()) {
    test(f.name,async()=>{
        const reference=await render('reference-'+index,fixtureHtml(20,f.extra));
        const actual=await render('large-'+index,fixtureHtml(500,f.extra));
        assert.equal(actual.image.data.equals(reference.image.data),true,'oversized radius must retain identical text coverage');
    });
}
