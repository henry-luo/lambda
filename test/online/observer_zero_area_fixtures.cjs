const cases = [
    {name:'deferred panel',style:'width:400px',ratio:1,intersects:true,populate:true},
    {name:'zero height',style:'width:100px;height:0',ratio:1,intersects:true},
    {name:'zero width',style:'width:0;height:100px',ratio:1,intersects:true},
    {name:'point',style:'width:0;height:0',ratio:1,intersects:true},
    {name:'bottom edge',style:'top:800px;width:100px;height:20px',ratio:0,intersects:true},
    {name:'right edge',style:'left:1200px;width:20px;height:100px',ratio:0,intersects:true},
    {name:'outside',style:'top:801px;width:100px;height:20px',ratio:0,intersects:false},
    {name:'display none',style:'display:none;width:100px;height:20px',ratio:0,intersects:false},
    {name:'hidden ancestor',style:'width:100px;height:20px',parent:'display:none',ratio:0,intersects:false},
    {name:'disconnected',style:'width:100px;height:20px',disconnected:true,ratio:0,intersects:false},
    {name:'collapsed transform',style:'width:100px;height:100px;transform-origin:0 0;transform:scale(0)',ratio:1,intersects:true},
    {name:'transparent box',style:'width:100px;height:100px;opacity:0',ratio:1,intersects:true}
];
function source(fixture) {
    return `<!doctype html><style>body{margin:0}#state{width:120px;height:80px;background:red}
        #target{position:absolute;left:100px;top:100px;${fixture.style}}</style>
        <div id="state"></div><div style="${fixture.parent||''}"><div id="target"></div></div>
        <script>var target=document.getElementById('target'),state=document.getElementById('state');
        ${fixture.disconnected ? 'target.remove();' : ''}
        new IntersectionObserver(function(entries,observer){
            var entry=entries[0];
            console.log('ZERO_AREA_SAMPLE',entry.isIntersecting,entry.intersectionRatio);
            if(entry.isIntersecting===${fixture.intersects}&&entry.intersectionRatio===${fixture.ratio})
                {state.style.background='lime';
                 ${fixture.populate ? `target.innerHTML='<div style="height:300px;background:lime;color:black">Deferred content loaded</div>';` : ''}}
            observer.disconnect();
        }).observe(target);</script>`;
}
module.exports={cases,source};
