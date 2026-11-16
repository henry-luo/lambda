const {test}=require('node:test');
const assert=require('node:assert/strict');
const {renderFixtures}=require('./render_fixture.cjs');
const {fixtures,fixtureHtml}=require('./generated_style_measurement_fixtures.cjs');
const render=renderFixtures('generated-style-measurement');
for(const[index,f]of fixtures.entries()){
    test(f.name,async()=>{
        const reference=await render('normal-'+index,fixtureHtml(f,false));
        const actual=await render('measured-'+index,fixtureHtml(f,true));
        assert.equal(actual.log.includes('inline height: selected flow restoration failed'),false,'a derived style refresh must preserve the selected flow dependency');
        assert.equal(actual.image.data.equals(reference.image.data),true,'an empty custom-layout sibling must not alter navigation pixels');
    });
}
