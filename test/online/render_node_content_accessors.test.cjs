const {test}=require('node:test');
const assert=require('node:assert/strict');
const {renderFixtures,rgb}=require('./render_fixture.cjs');
const {source,detachedSource}=require('./node_content_accessor_fixture.cjs');
const render=renderFixtures('node-content-accessors');
for(const backend of ['ast','mir'])test('captured Node content accessors: '+backend,async()=>{
    const {image,log}=await render(backend,source(),{...process.env,JS_EXEC_BACKEND:backend});
    assert.match(log,/NODE_CONTENT_ACCESSORS 38\b/);
    assert.deepEqual(rgb(image,50,50),[0,255,0]);
});
for(const backend of ['ast','mir'])test('detached CharacterData retains its owner: '+backend,async()=>{
    const {image,log}=await render('detached-'+backend,detachedSource(),{...process.env,JS_EXEC_BACKEND:backend});
    assert.match(log,/DETACHED_TEXT_DONE/);
    assert.equal((log.match(/DETACHED_TEXT aBCDr \["before","after"\] true/g)||[]).length,2);
    assert.deepEqual(rgb(image,50,50),[0,255,0]);
});
