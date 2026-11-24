const navigationTimingNames = [
    'navigationStart', 'unloadEventStart', 'unloadEventEnd', 'redirectStart', 'redirectEnd',
    'fetchStart', 'domainLookupStart', 'domainLookupEnd', 'connectStart', 'connectEnd',
    'secureConnectionStart', 'requestStart', 'responseStart', 'responseEnd', 'domLoading',
    'domInteractive', 'domContentLoadedEventStart', 'domContentLoadedEventEnd', 'domComplete',
    'loadEventStart', 'loadEventEnd',
];

const userTimingCases = [
    {name:'legacy-navigation-origin', expected:{start:0, duration:12}, source:`
        performance.mark('beforeRender', {startTime:12});
        var entry=performance.measure('before-hydration', 'navigationStart', 'beforeRender');
        result={start:entry.startTime, duration:entry.duration};`},
    {name:'options-navigation-origin', expected:{start:0, duration:12}, source:`
        var entry=performance.measure('options', {start:'navigationStart', duration:12});
        result={start:entry.startTime, duration:entry.duration};`},
    {name:'navigation-origin-as-end', expected:{start:12, duration:-12}, source:`
        performance.mark('beforeRender', {startTime:12});
        var entry=performance.measure('reverse', 'beforeRender', 'navigationStart');
        result={start:entry.startTime, duration:entry.duration};`},
    {name:'reserved-window-mark-names', expected:{reserved:42, entries:0}, source:`
        var names=${JSON.stringify(navigationTimingNames)}, reserved=0;
        for(var name of names) {
            try { performance.mark(name); } catch(e) { if(e.name==='SyntaxError') reserved++; }
            try { new PerformanceMark(name); } catch(e) { if(e.name==='SyntaxError') reserved++; }
        }
        result={reserved:reserved, entries:performance.getEntriesByType('mark').length};`},
    {name:'unrecorded-navigation-timestamp', expected:{error:'InvalidAccessError'}, source:`
        try { performance.measure('redirect', 'redirectStart'); }
        catch(e) { result={error:e.name}; }`},
    {name:'ordinary-marks-and-numeric-options', expected:{start:12, duration:8, numeric:8}, source:`
        performance.mark('begin', {startTime:12}); performance.mark('end', {startTime:20});
        var entry=performance.measure('normal', 'begin', 'end');
        var numeric=performance.measure('numeric', {start:12, end:20});
        result={start:entry.startTime, duration:entry.duration, numeric:numeric.duration};`},
    {name:'missing-user-mark', expected:{error:'SyntaxError'}, source:`
        try { performance.measure('missing', 'no-such-user-mark'); }
        catch(e) { result={error:e.name}; }`},
];

function userTimingHtml(fixture) {
    return `<!doctype html><style>body{margin:0}#result{width:80px;height:80px;background:red}</style>
    <div id=result></div><script>
    var result={};
    try { ${fixture.source} } catch(e) { result={unexpected:e.name, message:e.message}; }
    if(JSON.stringify(result)===${JSON.stringify(JSON.stringify(fixture.expected))})
        document.getElementById('result').style.background='rgb(0,255,0)';
    console.log('USER_TIMING_RESULT',JSON.stringify(result));
    </script>`;
}

module.exports = {userTimingCases, userTimingHtml};
