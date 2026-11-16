const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const {renderFixtures}=require('./render_fixture.cjs');
const {fixtures,fixtureHtml,fontFile}=require('./font_face_auto_weight_fixtures.cjs');
const render=renderFixtures('font-face-auto-weight');
for(const [index,f]of fixtures.entries()) {
    test(f.name,{skip:process.platform!=='darwin'||!fs.existsSync(fontFile)},async()=>{
        const reference=await render('explicit-'+index,fixtureHtml(f,true));
        const actual=await render('automatic-'+index,fixtureHtml(f,false));
        assert.equal(actual.image.data.equals(reference.image.data),true,'the selected descriptor must retain the correct variable weight');
    });
}
