const {test}=require('node:test'),assert=require('node:assert/strict');
const {renderFixtures,rgb}=require('./render_fixture.cjs');
const render=renderFixtures('console-groups');
test('console groups do not abort document initialization',async()=>{
    const {image,log}=await render('groups','<!doctype html><style>body{margin:0}#target{width:100px;height:80px;background:red;opacity:0}</style><div id="target"></div><script>document.addEventListener("DOMContentLoaded",function(){console.group("startup",7);console.groupCollapsed("details");console.groupEnd();console.groupEnd();document.getElementById("target").style.opacity="1";console.log("CONSOLE_GROUPS_READY");});</script>');
    assert(log.includes('CONSOLE_GROUPS_READY'),log.slice(-1600));
    assert.deepEqual(rgb(image,50,40),[255,0,0]);
});
