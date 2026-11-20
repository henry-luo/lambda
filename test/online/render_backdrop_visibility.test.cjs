const {test}=require('node:test');
const assert=require('node:assert/strict');
const {renderFixtures,rgb}=require('./render_fixture.cjs');
const {fixtures,fixtureHtml}=require('./backdrop_visibility_fixtures.cjs');
const render=renderFixtures('backdrop-visibility');
for(const [index,f]of fixtures.entries())test(f.name,async()=>{
    const {image}=await render('backdrop-'+index,fixtureHtml(f));
    const edge=rgb(image,101,75),background=rgb(image,149,10);
    assert.deepEqual(rgb(image,10,10),[0,255,0],'underlying scene remains painted');
    if(f.hidden){
        assert.deepEqual(edge,[0,255,0]);
        assert.deepEqual(background,[0,255,0]);
    }else{
        assert.deepEqual(rgb(image,150,100),[255,0,0],'visible child remains painted');
        if(f.red)assert.deepEqual(edge,[255,0,0]);
        else assert.notDeepEqual(edge,[255,0,0],'ordinary filter still blurs the child edge');
        if(f.blur)assert.ok(background[1]>0&&background[2]>0,'visible backdrop mixes background colors');
        if(f.noBlur)assert.deepEqual(background,[0,255,0]);
    }
});
