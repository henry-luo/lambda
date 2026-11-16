const {test}=require('node:test');
const assert=require('node:assert/strict');
const {renderFixtures,rgb}=require('./render_fixture.cjs');
const {fixtures,fixtureHtml}=require('./inline_rounded_background_fixtures.cjs');
const render=renderFixtures('inline-rounded-background');
for(const [index,f] of fixtures.entries()) {
    test(f.name,async()=>{
        const reference=await render('reference-'+index,fixtureHtml(f,1000));
        const actual=await render('large-'+index,fixtureHtml(f,9999));
        assert.equal(actual.image.data.equals(reference.image.data),true,'both oversized radii must use the same fragment constraint');
        assert.deepEqual(rgb(actual.image,10,10),[255,255,255]);
        let green=0;
        for(let offset=0;offset<actual.image.data.length;offset+=4) {
            const p=actual.image.data;
            if(p[offset]===0 && p[offset+1]===255 && p[offset+2]===0)green++;
        }
        assert.equal(f.offscreen?green===0:green>1000 && green<15000,true,'background must paint only the inline fragment');
    });
}
