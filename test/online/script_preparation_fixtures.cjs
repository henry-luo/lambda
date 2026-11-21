const scriptPreparationCases = ['disabled', 'disabled-defer', 'redirected', 'inline-rewritten',
    'activated', 'removed-before', 'scheduled-defer', 'prepared', 'prepared-redirected', 'prepared-removed',
    'prepared-adopted', 'data-adopted', 'head-segments'];

function scriptPreparationExpected(mode) {
    const deferred = ['scheduled-defer', 'prepared', 'prepared-redirected', 'prepared-removed'].includes(mode);
    const absent = ['disabled', 'disabled-defer', 'removed-before', 'prepared-adopted', 'data-adopted'].includes(mode);
    const name = mode === 'redirected' || mode === 'inline-rewritten' ? 'replacement' : 'original';
    return {runs: mode === 'head-segments'
        ? ['before-meta', 'candidate', 'original', 'after-meta', 'final-script', 'blocking']
        : absent ? ['blocking'] : deferred ? ['blocking', name] : [name, 'blocking'],
        loads: absent || mode === 'inline-rewritten' ? 0 : 1, errors: 0,
        current: absent ? null : 'candidate'};
}

function scriptPreparationHtml(mode, origin) {
    const prepared = mode.startsWith('prepared');
    const disabled = mode === 'disabled' || mode === 'disabled-defer';
    const change = disabled ? "node.type='text/plain';node.removeAttribute('src');"
        : mode === 'redirected' ? "node.src='" + origin + "/replacement.js';"
        : mode === 'inline-rewritten' ? "node.textContent=\"runs.push('replacement');current=document.currentScript.id;\";"
        : mode === 'activated' ? "node.type='text/javascript';"
        : mode === 'removed-before' ? 'node.remove();'
        : mode === 'scheduled-defer' ? 'node.defer=true;' : '';
    const observer = prepared || mode === 'data-adopted' ? '' : mode === 'head-segments' ? `new MutationObserver(function(records){
        for(const record of records)for(const node of record.addedNodes)if(node.nodeType===1 && node.id){
            runs.push(node.id);
            if(node.id==='candidate' && document.getElementById('after-meta'))runs.push('future-visible');
        }
    }).observe(document.head,{childList:true});` : `new MutationObserver(function(records){
        for(const record of records)for(const node of record.addedNodes){
            if(node.nodeType===1 && node.id==='candidate' && !changed){changed=true;${change}}
        }
    }).observe(document.head,{childList:true});`;
    const later = mode === 'prepared' ? "candidate.type='text/plain';candidate.removeAttribute('src');"
        : mode === 'prepared-redirected' ? "candidate.src='" + origin + "/replacement.js';"
        : mode === 'prepared-removed' ? 'candidate.remove();document.documentElement.getBoundingClientRect();' : '';
    const adoption = mode === 'prepared-adopted' || mode === 'data-adopted' ? `const other=document.implementation.createHTMLDocument('other');
        other.head.appendChild(other.adoptNode(document.getElementById('candidate')));` : '';
    return `<!doctype html><html><head><style>body{margin:0;background:red}</style><script>
        window.runs=[];window.loads=0;window.errors=0;window.current=null;window.changed=false;
        window.addEventListener('load',function(){
            const result={runs,loads,errors,current};
            console.log('SCRIPT_PREPARATION_RESULT',JSON.stringify(result));
            document.body.style.background=JSON.stringify(result)===${JSON.stringify(JSON.stringify(scriptPreparationExpected(mode)))}?'green':'red';
        });${observer}</script>
        ${mode === 'head-segments' ? '<meta id="before-meta">' : ''}
        <script id="candidate" ${prepared || mode === 'disabled-defer' ? 'defer' : ''}
            ${mode === 'activated' || mode === 'data-adopted' ? 'type="text/plain"' : ''}
            ${mode === 'inline-rewritten' ? '' : 'src="' + origin + '/original.js"'}
            onload="loads++" onerror="errors++">${mode === 'inline-rewritten' ? "runs.push('original');current=document.currentScript.id;" : ''}</script>
        ${mode === 'head-segments' ? '<meta id="after-meta">' : ''}
        <script ${mode === 'head-segments' ? 'id="final-script"' : ''}>${adoption}${later ? "const candidate=document.getElementById('candidate');" + later : ''}runs.push('blocking');</script>
        </head><body></body></html>`;
}

function servePreparationScript(request, response) {
    response.setHeader('Cache-Control', 'no-store');
    response.writeHead(200, {'Content-Type':'application/javascript'}).end(
        'runs.push(' + JSON.stringify(request.url.includes('replacement') ? 'replacement' : 'original') +
        ');current=document.currentScript.id;');
}

module.exports = {scriptPreparationCases, scriptPreparationExpected, scriptPreparationHtml, servePreparationScript};
