const cases = [
    {name:'eager', blocks:true}, {name:'css', blocks:true},
    {name:'css-var', blocks:true}, {name:'detached', blocks:true},
    {name:'mixed', blocks:true}, {name:'failed', blocks:true},
    {name:'chain', blocks:true}, {name:'lazy-sibling', blocks:true},
    {name:'lazy', blocks:false}, {name:'preload', blocks:false},
    {name:'hidden', blocks:false}, {name:'postload', blocks:false},
    {name:'pseudo', blocks:true}, {name:'unused-pseudo', blocks:false},
    {name:'shorthand', blocks:true}, {name:'overridden', blocks:false},
    {name:'source-replaced', blocks:false},
];

function html(mode, origin, id) {
    const image = origin + '/image.svg?id=' + id;
    const isCss = ['css','css-var','hidden','pseudo','unused-pseudo','shorthand','overridden'].includes(mode);
    let markup = '';
    if (isCss) {
        const property = (mode === 'shorthand' ? 'background:' : 'background-image:') +
            (mode === 'css-var' ? 'var(--photo)' : 'url(' + image + ')') +
            (mode === 'overridden' ? ';background:none' : '');
        markup = '<style>.box{width:80px;height:80px;--photo:url(' + image + ');' +
            (mode.includes('pseudo') ? '' : property) + '}' +
            (mode.includes('pseudo') ? '.box::before{display:block;width:80px;height:80px;' + property + ';' +
                (mode === 'pseudo' ? 'content:""' : '') + '}' : '') + '</style>' +
            '<div ' + (mode === 'hidden' ? 'style="display:none"' : '') + '><div class="box"></div></div>';
    } else if (mode === 'preload') markup = '<link rel="preload" as="image" href="' + image + '">';
    else if (!['detached','postload'].includes(mode)) {
        const tag = lazy => '<img ' + (lazy ? 'loading="lazy" ' : 'id="photo" ') + 'src="' + image + '">';
        markup = (mode === 'mixed' ? tag(true) : '') + (mode === 'lazy' ? tag(true) : tag(false));
        if (mode === 'lazy-sibling') markup += '<img loading="lazy" src="' + image + '&role=optional">';
    }
    return '<!doctype html><style>body{margin:0}img{width:80px;height:80px}</style><body>' + markup + `<script>
        const mode=${JSON.stringify(mode)},url=${JSON.stringify(image)};
        let events=[];const photo=mode==='detached'?new Image():document.getElementById('photo');
        if(photo){photo.onload=function(){events.push('image');
            if(mode==='chain'&&events.length===1)photo.src=url+'&second=1';
        };photo.onerror=function(){events.push('error')};}
        if(mode==='detached')photo.src=url;
        if(mode==='source-replaced')photo.src='data:image/svg+xml,'+encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" width="1" height="1"/>');
        window.addEventListener('load',function(){
            events.push('window');
            console.log('LOAD_BOUNDARY_RESULT',JSON.stringify({events:events,complete:photo?photo.complete:null}));
            fetch(${JSON.stringify(origin + '/beacon?id=' + id)});
            if(mode==='postload')fetch(url);
        });</script>`;
}

function serverHandler(records, origin) {
    return (request,response) => {
        const url = new URL(request.url,'http://fixture.invalid');
        const id = url.searchParams.get('id'), row = records.get(id);
        response.setHeader('Cache-Control','no-store');
        if (!row) {response.writeHead(404).end();return;}
        if (url.pathname === '/page') {
            response.writeHead(200,{'Content-Type':'text/html'}).end(html(row.mode,origin(),id));
        } else if (url.pathname === '/image.svg') {
            const key = url.searchParams.has('second') ? 'second' : url.searchParams.get('role') || 'main';
            row.events.push(key + '-start');let timer,finished=false;
            const release = () => {
                if(finished)return;finished=true;clearTimeout(timer);
                row.events.push(key + '-end');
                if(row.mode==='failed')response.writeHead(404).end();
                else response.writeHead(200,{'Content-Type':'image/svg+xml'}).end(
                    '<svg xmlns="http://www.w3.org/2000/svg" width="80" height="80"><rect width="80" height="80" fill="blue"/></svg>');
            };
            row.release.push(release);
            // the beacon releases early loads; the fallback lets conforming load blockers finish.
            timer=setTimeout(release,key==='optional'?1600:650);
        } else if (url.pathname === '/beacon') {
            row.events.push('window-load');response.end('{}');
            for(const release of row.release)release();
        } else response.writeHead(404).end();
    };
}

function check(assert, fixture, row, payload) {
    const load = row.events.indexOf('window-load'), end = row.events.indexOf('main-end');
    assert.ok(load >= 0, JSON.stringify(row.events));
    assert.equal(end >= 0 && end < load, fixture.blocks, JSON.stringify(row.events));
    if (['eager','detached','mixed','chain','lazy-sibling','failed'].includes(fixture.name)) {
        assert.equal(payload.complete,true);
        assert.deepEqual(payload.events,fixture.name==='failed'?['error','window']:
            fixture.name==='chain'?['image','image','window']:['image','window']);
    }
    if(fixture.name==='lazy-sibling')assert.ok(row.events.indexOf('optional-end')>load);
}

module.exports={cases,html,serverHandler,check};
