const intersectionEntryCases = [false, true].map(rebound => ({
    name:rebound ? 'captured-interface-after-rebinding' : 'native-interface', rebound,
    expected:{prototype:true, descriptors:true, brand:true, readonly:true, snapshot:true,
        target:true, bounds:true, ratio:1, intersects:true, time:true, own:0, tag:true},
}));

function intersectionEntryHtml(fixture) {
    return `<!doctype html><style>body{margin:0}#result{width:80px;height:80px;background:red}
    #target{position:absolute;left:100px;top:100px;width:100px;height:100px}</style>
    <div id=result></div><div id=target></div><script>
    var result={},original=IntersectionObserverEntry,proto=original.prototype;
    var fields=['time','rootBounds','boundingClientRect','intersectionRect','isIntersecting','intersectionRatio','target'];
    var descriptors=fields.every(function(name){var d=Object.getOwnPropertyDescriptor(proto,name);
        return d&&typeof d.get==='function'&&d.set===undefined&&d.enumerable&&d.configurable});
    function rejects(action){try{action();return false}catch(e){return e instanceof TypeError}}
    var illegal=rejects(function(){new original()})&&rejects(function(){original()});
    new IntersectionObserver(function(entries,observer){
        observer.disconnect();var entry=entries[0],target=document.getElementById('target');
        var ratio=Object.getOwnPropertyDescriptor(proto,'intersectionRatio'),getter=ratio&&ratio.get;
        var brands=fields.every(function(name){var d=Object.getOwnPropertyDescriptor(proto,name);
            return d&&rejects(function(){d.get.call({})})&&rejects(function(){d.get.call(Object.create(proto))})&&
                rejects(function(){d.get.call(new Proxy(entry,{}))})});
        var unchanged=entry.intersectionRatio;
        var readonly=rejects(function(){'use strict';entry.intersectionRatio=0});
        var own=Object.keys(entry).length;
        var values={ratio:entry.intersectionRatio,intersects:entry.isIntersecting,target:entry.target===target,
            bounds:entry.boundingClientRect.width===100&&entry.rootBounds.width===1200&&entry.intersectionRect.height===100};
        Object.defineProperty(entry,'intersectionRatio',{value:99});
        result={prototype:entry instanceof original&&Object.getPrototypeOf(entry)===proto,
            descriptors:!!descriptors,brand:!!(illegal&&brands),readonly:readonly&&unchanged===1,
            snapshot:!!getter&&getter.call(entry)===1,target:values.target,bounds:values.bounds,
            ratio:values.ratio,intersects:values.intersects,time:entry.time>=0&&entry.time<=performance.now(),
            own:own,tag:Object.prototype.toString.call(entry)==='[object IntersectionObserverEntry]'};
        if(JSON.stringify(result)===${JSON.stringify(JSON.stringify(fixture.expected))})
            document.getElementById('result').style.background='rgb(0,255,0)';
        console.log('INTERSECTION_ENTRY_RESULT',JSON.stringify(result));
    }).observe(document.getElementById('target'));
    ${fixture.rebound ? 'window.IntersectionObserverEntry=function ReplacedEntry(){};' : ''}
    </script>`;
}

module.exports = {intersectionEntryCases, intersectionEntryHtml};
