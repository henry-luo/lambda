const {test}=require('node:test'),assert=require('node:assert/strict');
const {renderFixtures,rgb}=require('./render_fixture.cjs');
const render=renderFixtures('svg-percentage-intrinsics');
const {card,fixtures}=require('./svg_percentage_intrinsic_fixtures.cjs');
for(const [name,options,height] of fixtures)test(name,async()=>{
    const {image}=await render(name.replaceAll(' ','-'),card(options));
    assert.deepEqual(rgb(image,200,height-30),[0,0,255],'media remains inside the card');
    assert.deepEqual(rgb(image,200,height+5),[255,255,255],'intrinsic estimate must not leave excess card height');
});
