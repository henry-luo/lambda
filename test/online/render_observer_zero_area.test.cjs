const {test}=require('node:test'),assert=require('node:assert/strict');
const {renderFixtures,rgb}=require('./render_fixture.cjs');
const {cases,source}=require('./observer_zero_area_fixtures.cjs');
const render=renderFixtures('observer-zero-area');
for(const [index,fixture]of cases.entries()) {
    test('intersection distinguishes zero area from absence: '+fixture.name,async()=>{
        const {image,log}=await render('zero-'+index,source(fixture));
        assert(log.includes('ZERO_AREA_SAMPLE'),log.slice(-1800));
        assert.deepEqual(rgb(image,60,40),[0,255,0],log.slice(-1800));
        if(fixture.populate)assert.deepEqual(rgb(image,200,250),[0,255,0]);
    });
}
