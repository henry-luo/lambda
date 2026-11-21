const {test}=require('node:test'),assert=require('node:assert/strict');
const {renderFixtures,rgb,loggedResult}=require('./render_fixture.cjs');
const render=renderFixtures('relative-color');

for(const mode of ['custom-property','current-color','updated-origin'])test('relative Oklch paints through '+mode,async()=>{
    const expression=mode==='current-color'?'oklch(from currentColor calc(l + .02) c h)':'oklch(from var(--base) calc(l + .02) c h)';
    const {image,log}=await render(mode,`<!doctype html><style>body{margin:0;background:white}
    #box{--base:${mode==='updated-origin'?'red':'#002fa6'};color:#002fa6;width:200px;height:100px;background:${expression}}
    </style><div id=box></div><script>
    const box=document.getElementById('box');box.getBoundingClientRect();
    ${mode==='updated-origin'?"box.style.setProperty('--base','#002fa6');":''}
    console.log('RELATIVE_COLOR',JSON.stringify({supported:CSS.supports('color','oklch(from red l c h)'),color:getComputedStyle(box).backgroundColor}));
    </script>`);
    const result=loggedResult(log,'RELATIVE_COLOR');
    assert.equal(result.supported,true);
    assert.match(result.color,/^oklch\(/);
    const actual=rgb(image,30,30),expected=[4,54,173];
    for(let i=0;i<3;i++)assert.ok(Math.abs(actual[i]-expected[i])<=1,JSON.stringify({actual,expected,result}));
});
