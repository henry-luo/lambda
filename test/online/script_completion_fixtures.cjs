function scriptCompletionSource(run) {
    if (run === 'empty') return '';
    return `globalThis.scriptSteps.push('execute');globalThis.scriptReady=true;
        const sheet=document.createElement('style');
        sheet.textContent='#probe{width:160px}';document.head.appendChild(sheet);
        globalThis.scriptScope=document.currentScript ? document.currentScript.id : null;
        const owner=document.currentScript || document.getElementById('resource');
        owner.addEventListener('load',()=>{globalThis.scriptListeners++;globalThis.scriptSteps.push('listener')});
        queueMicrotask(()=>globalThis.scriptSteps.push('execute-microtask'));
        ${run === 'removed' ? 'owner.remove();document.body.getBoundingClientRect();' : ''}
        ${run === 'throw' ? 'throw new Error("expected script completion failure");' : ''}`;
}

function scriptCompletionHtml(run) {
    const inline = run === 'inline' || run === 'inline-module';
    const module = run === 'module' || run === 'inline-module';
    const resource = `<script id="resource" ${module ? 'type="module"' : ''}
        ${run === 'defer' ? 'defer' : run === 'async' ? 'async' : ''}
        ${inline ? '' : `src="/script-completion.js?run=${run}"`}
        onload="scriptEvent.call(this,event)" onerror="scriptEvent.call(this,event)">
        ${inline ? scriptCompletionSource(run) : ''}</script>`;
    return `<!doctype html><head><style>body{margin:0}#probe{width:80px;height:80px;background:red}</style>
        <script>globalThis.scriptSteps=[];globalThis.scriptLoads=0;globalThis.scriptErrors=0;
        const bootstrapStyle=document.createElement('style');
        bootstrapStyle.textContent='#bootstrap{width:80px;height:80px;background:blue}';
        document.head.appendChild(bootstrapStyle);
        globalThis.scriptListeners=0;globalThis.scriptShape=true;globalThis.scriptScope=null;
        globalThis.scriptReady=false;globalThis.scriptHandlerMicrotask=false;globalThis.scriptHeadTail=null;
        function scriptEvent(event){
            scriptSteps.push(event.type);
            scriptShape=scriptShape && event.target===this && event.currentTarget===this &&
                !event.bubbles && !event.cancelable && document.currentScript===null;
            scriptHeadTail=!!document.getElementById('future-head');
            if(event.type==='load'){scriptLoads++;queueMicrotask(()=>{
                scriptHandlerMicrotask=true;scriptSteps.push('load-microtask')});}
            else scriptErrors++;
        }
        window.addEventListener('load',()=>{
            scriptSteps.push('window');
            console.log('SCRIPT_COMPLETION_RESULT',JSON.stringify({loads:scriptLoads,errors:scriptErrors,
                listeners:scriptListeners,shape:scriptShape,scope:scriptScope,ready:scriptReady,
                handlerMicrotask:scriptHandlerMicrotask,headTail:scriptHeadTail,steps:scriptSteps}));
            document.getElementById('probe').style.background='green';
        });</script>${run === 'removed' ? '' : resource}<meta id="future-head"></head>
        <body><div id="probe"></div><div id="bootstrap"></div>${run === 'removed' ? resource : ''}</body>`;
}

module.exports = {scriptCompletionSource, scriptCompletionHtml};
