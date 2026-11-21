const resourceTimingCases = [
    {name:'fetch-fade', delay:80, settle:1000},
    {name:'xhr-fade', delay:80, xhr:true, settle:1000},
    {name:'timer-fetch-fade', delay:80, start:150, settle:1000},
    {name:'chained-fetch-fade', delay:70, chain:true, settle:1000},
    {name:'fetch-timer-fade', delay:80, timer:100, settle:1000},
    {name:'after-window', delay:350, settle:100},
    {name:'zero-window', delay:80, settle:0},
];

function resourceTimingHtml(fixture, origin) {
    const url = JSON.stringify(origin + '/data?delay=' + fixture.delay);
    return `<!doctype html><style>html,body{margin:0;background:white}
        .fade{width:80px;height:80px;background:lime;animation:appear .3s linear}
        @keyframes appear{from{opacity:0}to{opacity:1}}</style><body><script>
        window.addEventListener('load',function(){
            const start=performance.now();
            setTimeout(function(){document.body.style.background='red'},3000);
            function appear(){
                const box=document.createElement('div');box.className='fade';document.body.appendChild(box);
                console.log('RESOURCE_TIMING_RESULT',JSON.stringify({birth:performance.now()-start}));
            }
            function read(){return ${fixture.xhr ? `new Promise(function(resolve,reject){
                const xhr=new XMLHttpRequest();xhr.open('GET',${url});
                xhr.onload=function(){resolve(JSON.parse(xhr.responseText))};xhr.onerror=reject;xhr.send();
            })` : `fetch(${url}).then(function(response){return response.json()})`}}
            function begin(){read()
                ${fixture.chain ? '.then(read)' : ''}
                .then(function(){${fixture.timer ? 'setTimeout(appear,' + fixture.timer + ')' : 'appear()'}})}
            ${fixture.start ? 'setTimeout(begin,' + fixture.start + ')' : 'begin()'}
        });</script>`;
}

function serveResourceTiming(request, response) {
    const delay = Number(new URL(request.url, 'http://fixture.invalid').searchParams.get('delay'));
    setTimeout(() => response.writeHead(200, {
        'Content-Type':'application/json', 'Access-Control-Allow-Origin':'*', 'Cache-Control':'no-store',
    }).end('{}'), delay);
}

module.exports = {resourceTimingCases, resourceTimingHtml, serveResourceTiming};
