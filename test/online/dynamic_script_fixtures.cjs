const dynamicScriptCases = ['basic', 'ordered', 'async', 'removed', 'src-change', 'connected-src',
    'connected-attribute', 'failed', 'throw', 'empty-src', 'chain', 'late', 'inert', 'unsupported',
    'adopted', 'async-idl', 'zero-window', 'src-observer', 'stylesheet', 'empty-body',
    'late-fetch', 'late-fetch-chain', 'late-fetch-immediate', 'large-admitted'];
const dynamicScriptBudgetCases = ['large-async', 'large-ordered'];
const dynamicModuleCases = ['basic', 'ordered', 'async', 'removed', 'connected-src',
    'failed', 'throw', 'import', 'cached', 'large-async', 'late-timer', 'chain', 'late-chain', 'large-admitted'];

function dynamicScriptExpected(mode, module = false) {
    const absent = ['failed', 'empty-src', 'empty-body', 'inert', 'unsupported', 'adopted', 'async-idl', ...dynamicScriptBudgetCases].includes(mode);
    const runs = absent ? [] : mode === 'async' ? ['B', 'A']
        : ['ordered', 'chain', 'late-chain'].includes(mode) ? ['A', 'B'] : ['A'];
    const result = {runs, scopes:runs.map(id => module ? null : id === 'A' ? 'first' : 'second'),
        loads:mode === 'cached' ? 2 : mode === 'empty-body' ? 1 : runs.length, errors:mode === 'failed' || mode === 'empty-src' ? 1 : 0,
        asynchronous:true, shape:true, beforeWindow:!mode.startsWith('late') && mode !== 'empty-src', asyncIdl:true};
    if (mode === 'src-observer') result.attributes = true;
    if (mode.startsWith('late-fetch')) result.data = true;
    return result;
}

function dynamicScriptHtml(mode, origin, module = false) {
    const expected = dynamicScriptExpected(mode, module);
    return `<!doctype html><head><style>body{margin:0;background:white}
        #probe,#styleprobe{width:80px;height:80px;position:absolute;top:0}
        #probe{background:red;left:0}#styleprobe{left:100px}</style></head>
        <body><div id="probe"></div><div id="styleprobe"></div><script>
        window.sourceRuns=[];window.sourceScopes=[];window.sourceLoads=0;window.sourceErrors=0;
        window.inAppend=false;window.sourceAsynchronous=true;window.sourceShape=true;
        window.sourceBeforeWindow=true;window.sourceAsyncIdl=true;window.windowLoaded=false;window.reported=false;
        window.sourceAttributes=true;
        window.sourceDataReady=${!mode.startsWith('late-fetch') && mode !== 'import' && mode !== 'late-timer'};
        const mode=${JSON.stringify(mode)},origin=${JSON.stringify(origin)},expected=${JSON.stringify(expected)};
        function report(){
            if(reported || !sourceDataReady || !windowLoaded || sourceLoads!==expected.loads || sourceErrors!==expected.errors)return;
            reported=true;
            const result={runs:sourceRuns,scopes:sourceScopes,loads:sourceLoads,errors:sourceErrors,
                asynchronous:sourceAsynchronous,shape:sourceShape,beforeWindow:sourceBeforeWindow,asyncIdl:sourceAsyncIdl};
            if(mode==='src-observer')result.attributes=sourceAttributes;
            if(mode.startsWith('late-fetch'))result.data=sourceDataReady;
            console.log('DYNAMIC_SCRIPT_RESULT',JSON.stringify(result));
            document.getElementById('probe').style.background=JSON.stringify(result)===JSON.stringify(expected)?'green':'red';
        }
        function sourceUrl(id,delay){return origin+'/script.js?case='+mode+'&id='+id+'&delay='+delay+${JSON.stringify(module ? '&module=1' : '')}}
        window.appendSource=function(id,delay){
            const script=document.createElement('script');script.id=id==='A'?'first':'second';
            ${module ? "script.type='module';" : ''}
            if(mode==='ordered'||mode==='large-ordered')script.async=false;
            if(mode==='unsupported')script.type='text/plain';
            script.onload=script.onerror=function(event){
                sourceShape=sourceShape && event.target===script && event.currentTarget===script &&
                    !event.bubbles && !event.cancelable && document.currentScript===null;
                ${module ? "sourceShape=sourceShape && typeof window.moduleLocalMarker==='undefined';" : ''}
                sourceBeforeWindow=sourceBeforeWindow && !windowLoaded;
                if(event.type==='load')sourceLoads++;else sourceErrors++;
                report();
            };
            const connected=mode==='connected-src'||mode==='connected-attribute'||mode==='src-observer';
            if(mode==='src-observer'){
                sourceAttributes=false;
                new MutationObserver(function(records){sourceAttributes=records.length===2 &&
                    records[0].attributeName==='src' && records[0].oldValue===null && records[0].target===script &&
                    records[1].attributeName==='src' && records[1].oldValue===sourceUrl('A',40);
                }).observe(script,{attributes:true,attributeFilter:['src'],attributeOldValue:true});
            }
            if(!connected)script.src=mode==='empty-src'?'':sourceUrl(id,delay);
            if(mode==='empty-src')script.textContent="sourceRuns.push('unsafe-inline')";
            inAppend=true;document.head.appendChild(script);inAppend=false;
            if(connected){if(mode==='connected-src'||mode==='src-observer')script.src=sourceUrl(id,delay);
                else script.setAttribute('src',sourceUrl(id,delay))}
            if(mode==='src-change'||mode==='src-observer')script.src=sourceUrl('B',0);
            if(mode==='removed'){script.remove();document.documentElement.getBoundingClientRect();
                document.body.appendChild(script.cloneNode(true))}
            if(mode==='adopted'){window.otherDocument=document.implementation.createHTMLDocument('other');
                otherDocument.head.appendChild(otherDocument.adoptNode(script))}
            return script;
        };
        window.addEventListener('load',function(){windowLoaded=true;
            if(mode.startsWith('late'))appendSource('A',40);report()});
        if(mode==='async-idl'){
            const script=document.createElement('script');
            sourceAsyncIdl=script.async===true && !script.hasAttribute('async') && document.currentScript.async===false;
            script.async=false;sourceAsyncIdl=sourceAsyncIdl && script.async===false && !script.hasAttribute('async');
            script.async=true;sourceAsyncIdl=sourceAsyncIdl && script.async===true && script.hasAttribute('async');
            script.removeAttribute('async');sourceAsyncIdl=sourceAsyncIdl && script.async===false;
            sourceAsyncIdl=sourceAsyncIdl && document.createElement('script').cloneNode(true).async===true;
        }else if(mode==='inert'){
            const box=document.createElement('div');box.innerHTML='<script src="'+sourceUrl('A',0)+'"><'+'/script>';
            document.body.appendChild(box);
        }else if(!mode.startsWith('late')){
            appendSource('A',mode==='ordered'||mode==='async'?120:40);
            if(mode==='ordered'||mode==='async')appendSource('B',10);
            if(mode==='cached')appendSource('A',40);
        }
        </script></body>`;
}

function serveDynamicScript(request, response) {
    const url = new URL(request.url, 'http://fixture.invalid');
    const mode = url.searchParams.get('case'), id = url.searchParams.get('id');
    response.setHeader('Cache-Control', 'no-store');
    if (url.pathname === '/dependency.js') {
        response.writeHead(200, {'Content-Type':'application/javascript', 'Access-Control-Allow-Origin':'*'})
            .end("export const value='dependency';");return;
    }
    setTimeout(() => {
        if (mode === 'failed') { response.writeHead(404).end(); return; }
        if (mode === 'empty-body') { response.writeHead(200, {'Content-Type':'application/javascript'}).end(); return; }
        if (url.pathname === '/sheet.css') {
            response.writeHead(200, {'Content-Type':'text/css'}).end('#styleprobe{background:blue}'); return;
        }
        if (url.pathname === '/data.json') {
            response.writeHead(200, {'Content-Type':'application/json', 'Access-Control-Allow-Origin':'*'})
                .end('{"okay":true}'); return;
        }
        let source = `sourceRuns.push(${JSON.stringify(id)});sourceScopes.push(document.currentScript?document.currentScript.id:null);
            sourceAsynchronous=sourceAsynchronous && !inAppend;`;
        if (url.searchParams.has('module')) source +=
            'var moduleLocalMarker=1;sourceShape=sourceShape && this===undefined && document.currentScript===null;';
        if (mode === 'import') source = "import {value} from './dependency.js';" + source +
            `sourceDataReady=value==='dependency' && new URL('./dependency.js',import.meta.url).href===${JSON.stringify('http://' + request.headers.host + '/dependency.js')};`;
        source += mode === 'stylesheet' ? `var sheet=document.createElement('link');sheet.rel='stylesheet';
            sheet.href=${JSON.stringify('http://' + request.headers.host + '/sheet.css?case=stylesheet')};`
            : "var sheet=document.createElement('style');sheet.textContent='#styleprobe{background:blue}';";
        source += 'document.head.appendChild(sheet);';
        if ((mode === 'chain' || mode === 'late-chain') && id === 'A') source += "appendSource('B',20);";
        if (mode === 'late-timer') source += `Promise.resolve().then(function(){
            setTimeout(function(){sourceShape=sourceShape && sourceLoads===1;
                sourceDataReady=true;report()},100);
        });`;
        if (mode.startsWith('late-fetch')) {
            const dataUrl = JSON.stringify('http://' + request.headers.host + '/data.json?case=' + mode + '&delay=80');
            source += `fetch(${dataUrl}).then(function(response){return response.json()})`;
            if (mode === 'late-fetch-chain') source += `.then(function(data){return data.okay?fetch(${dataUrl}):Promise.reject('invalid data')})
                .then(function(response){return response.json()})`;
            source += '.then(function(data){var complete=function(){sourceDataReady=data.okay;report()};';
            source += mode === 'late-fetch-immediate'
                ? `if(typeof setImmediate==='function')setImmediate(complete);
                    else {var channel=new MessageChannel();channel.port1.onmessage=function(){
                        channel.port1.close();channel.port2.close();complete()};channel.port2.postMessage(null)}});`
                : 'complete()});';
        }
        if (mode === 'throw') source += "throw new Error('expected dynamic source throw');";
        if (mode.startsWith('large-')) source = '/*' + 'x'.repeat(131072) + '*/' + source;
        response.writeHead(200, {'Content-Type':'application/javascript', 'Access-Control-Allow-Origin':'*'}).end(source);
    }, Number(url.searchParams.get('delay')) || 0);
}

module.exports = {dynamicScriptCases, dynamicScriptBudgetCases, dynamicModuleCases, dynamicScriptExpected,
    dynamicScriptHtml, serveDynamicScript};
