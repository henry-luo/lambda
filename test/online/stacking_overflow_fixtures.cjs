const stackingOverflowCases = [
    {name:'automatic-z-control', z:'auto'},
    {name:'zero-height-context', z:'5'},
    {name:'large-z-context', z:'2147483638'},
    {name:'positive-height-control', z:'5', height:1},
    {name:'zero-width-context', z:'5', width:0, height:100},
    {name:'offscreen-context', z:'5', height:100, top:-200},
    {name:'nested-context', z:'5', nested:true},
    {name:'overflow-hidden-escape', z:'5', extra:'overflow:hidden'},
    {name:'negative-layer-escape', z:'5', extra:'overflow:hidden', childZ:-1},
    {name:'positive-layer-escape', z:'5', extra:'overflow:hidden', childZ:10},
    {name:'scrolled-owner-escape', z:'5', height:100, extra:'overflow:auto', scroll:true},
    {name:'containing-owner-still-clips', z:'5', top:680, height:40, extra:'overflow:hidden',
        childPosition:'absolute', childBottom:-100, clipped:true, point:[40,760]},
    {name:'paint-containment-clips', z:'5', extra:'contain:paint', clipped:true},
    {name:'clip-path-clips', z:'5', extra:'clip-path:inset(0)', clipped:true},
    {name:'dynamic-zero-height', z:'5', height:1, dynamic:true},
    {name:'absolute-child-outside-containing-block', z:'auto', position:'static', extra:'isolation:isolate', childPosition:'absolute'},
];

function stackingOverflowHtml(fixture) {
    const child=(fixture.scroll?'<div style="height:1000px"></div>':'')+'<div class="child"></div>';
    return `<!doctype html><style>body{margin:0}.stack{position:${fixture.position||'absolute'};
    left:0;top:${fixture.top||0}px;width:${fixture.width===undefined?'100%':fixture.width+'px'};
    height:${fixture.height||0}px;z-index:${fixture.z};${fixture.extra||''}}
    .child{position:${fixture.childPosition||'fixed'};left:24px;bottom:${fixture.childBottom===undefined?24:fixture.childBottom}px;width:200px;height:80px;background:lime;z-index:${fixture.childZ===undefined?'auto':fixture.childZ}}
    </style><div class="stack">${fixture.nested?'<div style="isolation:isolate">'+child+'</div>':child}</div>
    <script>${fixture.dynamic?"document.querySelector('.child').getBoundingClientRect();document.querySelector('.stack').style.height='0px';":''}
    ${fixture.scroll?"document.querySelector('.stack').scrollTop=200;":''}
    var box=document.querySelector('.child').getBoundingClientRect();
    console.log('STACKING_OVERFLOW_RESULT',JSON.stringify({x:box.x,y:box.y,width:box.width,height:box.height}));</script>`;
}

module.exports = {stackingOverflowCases, stackingOverflowHtml};
