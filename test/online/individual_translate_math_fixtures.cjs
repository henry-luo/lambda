const fixtures = [
    {name:'nested percentage arithmetic', declaration:'translate:calc(calc(1 / 2 * 100%) * -1) 0', x:180, y:100},
    {name:'mixed font lengths and reference-box percentages', declaration:'translate:calc(-50% + 1em) calc(25% - 5px)', x:200, y:115},
    {name:'substituted calculation after a size edit', declaration:'--shift:calc(-50% + 10px);translate:var(--shift) 0', script:"box.style.width='160px'", width:160, x:170, y:100},
    {name:'independent translation composes before the transform list', declaration:'translate:calc(-50% + 10px) 0;transform:translateX(20px)', x:210, y:100},
    {name:'equivalent transform-list calculation is applied once', declaration:'transform:translate(calc(-50% + 10px),0)', x:190, y:100},
    {name:'reference-dependent transform-list minimum retains its expression', declaration:'transform:translate(min(10px,50%),0)', x:250, y:100},
    {name:'invalid percentage on the third axis retains the preceding declaration', declaration:'translate:-50% 0;translate:calc(-50%) 0 calc(10%)', x:180, y:100},
];
function fixtureHtml(fixture) {
    return '<!doctype html><style>html,body{margin:0;background:white}' +
        '#box{position:absolute;left:240px;top:100px;width:120px;height:80px;' +
        'font-size:20px;background:#008000;' + fixture.declaration + '}</style>' +
        '<div id="box"></div>' + (fixture.script ? '<script>const box=document.getElementById("box");'+fixture.script+'</script>' : '');
}
module.exports={fixtures,fixtureHtml};
