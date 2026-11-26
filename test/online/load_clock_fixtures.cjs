const assert=require('node:assert/strict');
const {rgb}=require('./render_fixture.cjs');

function checkCssClockFrame(image) {
    const fade=rgb(image,40,120);
    assert.ok(Math.abs(fade[0]-128)<20&&Math.abs(fade[1]-128)<20&&fade[2]===0,
        'CSS animation age includes the one-second settlement, excluding loading: '+fade);
    assert.deepEqual(rgb(image,40,200),[255,0,0],'authored CSS pause stays paused');
}

function loadClockSource(antiFlicker=false,frames='') {
 return `<!doctype html><style>html,body{margin:0;background:white}
    ${antiFlicker?'.anti-flicker,.anti-flicker *{visibility:hidden!important;opacity:0!important}':''}
    .box{width:80px;height:80px;background:red}#fade{animation:fade 2s linear forwards}
    #paused{animation:fade 2s linear forwards;animation-play-state:paused}
    @keyframes fade{to{background-color:lime}}</style><body>
    <div class=box id=marker></div><div class=box id=fade></div><div class=box id=paused></div><script>
    const start=performance.now(),epoch=Date.now();
    ${frames?`let loaded=false,scriptDone=false,microtaskDone=false,before=0,first=-1,last=-1;
    let reentrant=false,clockError=0,nestedBeforeLoad=false,canceled=false;
    Promise.resolve().then(()=>{microtaskDone=true});
    requestAnimationFrame(function tick(timestamp){
        const now=performance.now()-start;
        reentrant=reentrant||!scriptDone||!microtaskDone;
        clockError=Math.max(clockError,Math.abs(timestamp-performance.now()));
        if(!loaded){
            before++;last=now;
            if(first<0){first=now;requestAnimationFrame(()=>{nestedBeforeLoad=!loaded})}
            requestAnimationFrame(tick);
        }
    });
    const canceledId=requestAnimationFrame(()=>{canceled=true});cancelAnimationFrame(canceledId);
    addEventListener('load',()=>{
        loaded=true;console.log('LOAD_FRAMES_RESULT',JSON.stringify({
            before,first,last,load:performance.now()-start,reentrant,clockError,nestedBeforeLoad,canceled
        }));
    });`:''}
    ${antiFlicker?"document.documentElement.classList.add('anti-flicker');":''}
    setTimeout(()=>{document.getElementById('marker').style.background='lime';
        document.documentElement.classList.remove('anti-flicker');},${antiFlicker?4000:1800});
    // force CSS timeline creation before the delayed load blocker completes.
    document.getElementById('fade').getBoundingClientRect();
    addEventListener('load',()=>{
        const loaded=performance.now(),dateLoaded=Date.now();
        setTimeout(()=>console.log('LOAD_CLOCK_RESULT',JSON.stringify({
            load:loaded-start,dateLoad:dateLoaded-epoch,
            settle:performance.now()-loaded,dateSettle:Date.now()-dateLoaded,
            originError:Math.abs(performance.timeOrigin+performance.now()-Date.now())
        })),500);
    });
    const blocker=document.createElement('${frames==='image'?'img':'script'}');
    blocker.src='/held.${frames==='image'?'svg':'js'}${antiFlicker?'?anti-flicker=1':''}';
    ${frames?"blocker.src+='?fixture='+encodeURIComponent(location.search);":''}
    ${frames==='image'?"document.body.appendChild(blocker);":"blocker.async=true;document.head.appendChild(blocker);"}
    ${frames?'scriptDone=true;':''}
    </script>`;
}

function serveLoadClock(request,response) {
    const url=new URL(request.url,'http://fixture.invalid');
    const antiFlicker=url.pathname==='/anti-flicker'||url.searchParams.has('anti-flicker');
    if(url.pathname==='/held.js')setTimeout(()=>response.writeHead(200,{
        'Content-Type':'text/javascript', 'Cache-Control':'no-store',
    }).end('window.heldScriptLoaded=true;'),antiFlicker?3500:1200);
    else if(url.pathname==='/held.svg')setTimeout(()=>response.writeHead(200,{
        'Content-Type':'image/svg+xml','Cache-Control':'no-store',
    }).end('<svg xmlns="http://www.w3.org/2000/svg" width="1" height="1"><path d="M0 0h1v1H0z" fill="white"/></svg>'),1200);
    else response.writeHead(200,{'Content-Type':'text/html','Cache-Control':'no-store'})
        .end(loadClockSource(antiFlicker,url.searchParams.get('frames')||''));
}

module.exports={serveLoadClock,checkCssClockFrame};
