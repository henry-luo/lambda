const {test}=require('node:test');
const assert=require('node:assert/strict');
const {renderFixtures,rgb}=require('./render_fixture.cjs');
const {fixtures,fixtureHtml}=require('./nested_positioned_fixtures.cjs');
const render=renderFixtures('nested-positioned');
for(const [index,f] of fixtures.entries()) {
    test(f.name,async()=>{
        const {image}=await render('nested-'+index,fixtureHtml(f));
        const actual=rgb(image,40,20);
        for(let channel=0;channel<3;channel++)
            assert.ok(Math.abs(actual[channel]-f.expected[channel])<=(f.alpha?1:0),actual.join(','));
        if(f.clip)assert.deepEqual(rgb(image,40,60),[255,0,0]);
    });
}
