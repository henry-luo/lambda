const locationNavigationCases = [
    'href', 'document-location', 'window-location', 'bare-location', 'strict-window-location',
    'window-location-hash', 'window-location-descriptor', 'inserted-script',
    'timer-script', 'fetch-script', 'relative-href', 'coercion',
    'current-url-reload', 'empty-url-reload', 'hash-only', 'history-state',
    'throwing-coercion', 'assign-method', 'replace-method',
].map(name => ({name}));

function locationNavigationHtml(name, origin) {
    const destination = JSON.stringify(origin + '/destination?case=' + name);
    const assignment = 'location.href=' + destination + ';';
    const insertion = `var script=document.createElement('script');
        script.type='text/javascript';script.text=${JSON.stringify(assignment)};
        document.body.insertBefore(script,document.body.firstChild);`;
    let action = assignment;
    if (name === 'document-location') action = 'document.location=' + destination + ';';
    else if (name === 'window-location') action = 'window.location=' + destination + ';';
    else if (name === 'bare-location') action = 'location=' + destination + ';';
    else if (name === 'strict-window-location') action = '"use strict";window.location=' + destination + ';';
    else if (name === 'window-location-hash') action = `var original=location;
        window.location='#ready';if(window.location===original&&location.hash==='#ready')
            document.body.style.background='lime';`;
    else if (name === 'window-location-descriptor') action = `var d=Object.getOwnPropertyDescriptor(window,'location'),illegal=0;
        try{d.get.call({})}catch(e){if(e instanceof TypeError)illegal++}
        try{d.set.call({},'/destination')}catch(e){if(e instanceof TypeError)illegal++}
        if(d.enumerable&&!d.configurable&&d.get.length===0&&d.set.length===1&&
            d.get.call(window)===document.location&&illegal===2)document.body.style.background='lime';`;
    else if (name === 'inserted-script') action = insertion;
    else if (name === 'timer-script') action = 'setTimeout(function(){' + insertion + '},0);';
    else if (name === 'fetch-script') action = `fetch('/ready').then(function(r){return r.text()}).then(function(){${insertion}});`;
    else if (name === 'relative-href') action = "location.href='../destination?case=relative-href';";
    else if (name === 'coercion') action = `location.href={toString:function(){
        history.replaceState(null,'','#before');if(typeof gc==='function')gc();return ${destination};}};`;
    else if (name === 'current-url-reload') action = 'location.href=location.href;';
    else if (name === 'empty-url-reload') action = "location.href='';";
    else if (name === 'hash-only') action = `var calls=0,marker={};window.keep=marker;
        addEventListener('hashchange',function(){calls++});location.href='#ready';
        var immediate=location.hash==='#ready';setTimeout(function(){
            if(immediate&&calls===1&&window.keep===marker)document.body.style.background='lime';},0);`;
    else if (name === 'history-state') action = `var marker={};window.keep=marker;
        history.pushState({value:7},'',${destination});
        if(window.keep===marker&&history.state.value===7&&location.pathname==='/destination')
            document.body.style.background='lime';`;
    else if (name === 'throwing-coercion') action = `var marker={},before=location.href,caught;
        try{location.href={toString:function(){throw marker}}}catch(error){caught=error}
        if(caught===marker&&location.href===before)document.body.style.background='lime';`;
    else if (name === 'assign-method' || name === 'replace-method')
        action = `location.${name === 'assign-method' ? 'assign' : 'replace'}(${destination});`;
    return '<!doctype html><style>body{margin:0;background:blue}</style><body>original<script>' + action + '</script></body>';
}

function locationNavigationResources(counts) {
    return (request, response) => {
        const origin = 'http://' + request.headers.host;
        const url = new URL(request.url, origin), name = url.searchParams.get('case');
        response.setHeader('Cache-Control', 'no-store');
        if (url.pathname === '/ready') { response.writeHead(200).end('ready'); return; }
        if (!name || !locationNavigationCases.some(fixture => fixture.name === name)) {
            response.writeHead(204).end(); return;
        }
        const count = counts[name] || (counts[name] = {source:0,destination:0});
        if (url.pathname === '/destination') count.destination++;
        else count.source++;
        const reloaded = name.endsWith('-reload') && count.source > 1;
        response.writeHead(200, {'Content-Type':'text/html'}).end(
            url.pathname === '/destination' || reloaded
                ? '<!doctype html><style>body{margin:0;background:lime}</style>destination loaded'
                : locationNavigationHtml(name, origin));
    };
}

function locationNavigationExpected(name) {
    if (name.endsWith('-reload')) return {source:2,destination:0};
    if (['hash-only','history-state','throwing-coercion','window-location-hash',
        'window-location-descriptor'].includes(name)) return {source:1,destination:0};
    return {source:1,destination:1};
}

function locationNavigationPath(name) {
    return (name === 'relative-href' ? '/nested/' : '/source/') + name + '?case=' + name;
}

module.exports = {locationNavigationCases, locationNavigationHtml,
    locationNavigationResources, locationNavigationExpected, locationNavigationPath};
