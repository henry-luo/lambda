const {test}=require('node:test');
const assert=require('node:assert/strict');
const {renderFixtures}=require('./render_fixture.cjs');
const {fixtures,fixtureHtml}=require('./inline_button_context_fixtures.cjs');
const render=renderFixtures('inline-button-context');
for(const [index,f] of fixtures.entries()) {
    test(f.name,async()=>{
        const reference=await render('compact-'+index,fixtureHtml(f,false));
        const actual=await render('pretty-'+index,fixtureHtml(f,true));
        assert.equal(actual.image.data.equals(reference.image.data),true,'internal collapsed whitespace must not alter outer atomic layout');
    });
}
