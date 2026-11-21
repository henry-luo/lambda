const {test}=require('node:test'),assert=require('node:assert/strict');
const {renderFixtures,rgb,loggedResult}=require('./render_fixture.cjs');
const render=renderFixtures('all-reset-unknown');

for(const value of ['none','auto','100%'])test('all reset keeps authored sizing beside text-size-adjust: '+value,async()=>{
    const {image,log}=await render(value.replace('%','percent'),`<!doctype html><style>
    body{margin:0;background:white}#root{all:initial;position:relative;display:block;width:400px;height:200px}
    #root *{all:revert}#root .box{width:200px;height:100px;background:red;text-size-adjust:${value};position:absolute;display:grid}
    </style><div id=root><div class=box>Content</div></div><script>
    const e=document.querySelector('.box'),r=e.getBoundingClientRect();
    console.log('ALL_RESET_SIZE',JSON.stringify({width:r.width,height:r.height}));
    </script>`);
    assert.deepEqual(loggedResult(log,'ALL_RESET_SIZE'),{width:200,height:100});
    assert.deepEqual(rgb(image,150,50),[255,0,0]);
    assert.deepEqual(rgb(image,250,50),[255,255,255]);
});
