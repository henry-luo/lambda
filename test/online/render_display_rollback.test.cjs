const {test}=require('node:test'),assert=require('node:assert/strict');
const {renderFixtures,rgb,loggedResult}=require('./render_fixture.cjs');
const render=renderFixtures('display-rollback');

for(const reset of ['initial','unset','inherit'])for(const rollback of ['revert','revert-layer'])test('display rollback restores UA defaults after all:'+reset+', '+rollback,async()=>{
    const {image,log}=await render(reset+'-'+rollback,`<!doctype html><head><title>Hidden title</title>
    <style id=sheet>*{all:${reset};display:${rollback}}#target{width:100px;height:50px;background:red}</style></head>
    <body><div id=target>Content</div><table id=table><tbody><tr id=row><td id=cell>Cell</td></tr></tbody></table>
    <script id=script>console.log('DISPLAY_ROLLBACK',JSON.stringify({displays:[document.head,document.getElementById('sheet'),document.getElementById('script'),document.getElementById('target'),document.getElementById('table'),document.getElementById('row'),document.getElementById('cell')].map(e=>getComputedStyle(e).display)}));</script></body>`);
    assert.deepEqual(loggedResult(log,'DISPLAY_ROLLBACK').displays,['none','none','none','block','table','table-row','table-cell']);
    assert.deepEqual(rgb(image,90,40),[255,0,0]);
});

for(const reset of ['initial','unset'])test('all:'+reset+' still uses initial display when it wins',async()=>{
    const {log}=await render('winning-'+reset,`<!doctype html><style>.probe{all:${reset}}</style>
    <div class=probe id=target>Content</div><style class=probe id=authored>/* authored visible style */</style>
    <script>console.log('DISPLAY_INITIAL',JSON.stringify({displays:['target','authored'].map(id=>getComputedStyle(document.getElementById(id)).display)}));</script>`);
    assert.deepEqual(loggedResult(log,'DISPLAY_INITIAL').displays,['inline','inline']);
});

test('explicit display can still show a style element',async()=>{
    const {log}=await render('explicit-style',`<!doctype html><style>.visible{display:block}</style>
    <style id=authored class=visible>/* authored visible style */</style>
    <script>console.log('DISPLAY_EXPLICIT',JSON.stringify({display:getComputedStyle(document.getElementById('authored')).display}));</script>`);
    assert.equal(loggedResult(log,'DISPLAY_EXPLICIT').display,'block');
});
