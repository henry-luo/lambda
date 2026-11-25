function loadClockSource(antiFlicker=false) {
 return `<!doctype html><style>html,body{margin:0;background:white}
    ${antiFlicker?'.anti-flicker,.anti-flicker *{visibility:hidden!important;opacity:0!important}':''}
    .box{width:80px;height:80px;background:red}#fade{animation:fade 2s linear forwards}
    #paused{animation:fade 2s linear forwards;animation-play-state:paused}
    @keyframes fade{to{background-color:lime}}</style><body>
    <div class=box id=marker></div><div class=box id=fade></div><div class=box id=paused></div><script>
    const start=performance.now(),epoch=Date.now();
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
    const script=document.createElement('script');script.src='/held.js${antiFlicker?'?anti-flicker=1':''}';script.async=true;
    document.head.appendChild(script);
    </script>`;
}

function serveLoadClock(request,response) {
    const url=new URL(request.url,'http://fixture.invalid');
    const antiFlicker=url.pathname==='/anti-flicker'||url.searchParams.has('anti-flicker');
    if(url.pathname==='/held.js')setTimeout(()=>response.writeHead(200,{
        'Content-Type':'text/javascript', 'Cache-Control':'no-store',
    }).end('window.heldScriptLoaded=true;'),antiFlicker?3500:1200);
    else response.writeHead(200,{'Content-Type':'text/html','Cache-Control':'no-store'}).end(loadClockSource(antiFlicker));
}

module.exports={serveLoadClock};
