const {test}=require('node:test');
const assert=require('node:assert/strict');
const {renderFixtures,serveFixtureResources,loggedResult}=require('./render_fixture.cjs');
const fixtures=require('./load_boundary_fixtures.cjs');
const render=renderFixtures('load-boundary'),records=new Map();
const origin=serveFixtureResources(fixtures.serverHandler(records,()=>origin()));
for(const fixture of fixtures.cases)test('window.load boundary: '+fixture.name,async()=>{
    const id=fixture.name, row={mode:id,events:[],release:[]};records.set(id,row);
    const {log}=await render(id,{url:origin()+'/page?id='+id},
        {...process.env,LAMBDA_POST_LOAD_SETTLE_MS:'1000'});
    fixtures.check(assert,fixture,row,loggedResult(log,'LOAD_BOUNDARY_RESULT'));
});
