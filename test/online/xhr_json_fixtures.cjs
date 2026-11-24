const xhrJsonCases = [
    {name:'object', body:'{"nested":{"value":42}}', value:{nested:{value:42}}},
    {name:'array', body:'[1,{"value":2}]', value:[1,{value:2}]},
    {name:'string', body:'"λ😀"', value:'λ😀'},
    {name:'number', body:'0', value:0},
    {name:'boolean', body:'false', value:false},
    {name:'null', body:'null', value:null},
    {name:'malformed', body:'{"broken":', value:null},
    {name:'empty', body:'', status:204, value:null},
    {name:'non-json-mime', body:'{"value":3}', mime:'text/plain', value:{value:3}},
    {name:'bom', body:'\uFEFF{"value":4}', value:{value:4}},
    {name:'malformed-utf8', body:Buffer.from([34,97,128,98,34]), value:'a\uFFFDb'},
    {name:'explicit-text-control', body:'{"value":5}', type:'text', value:'{"value":5}'},
    {name:'default-text-control', body:'{"value":6}', type:'', value:'{"value":6}'},
    {name:'http-error-json', body:'{"value":7}', status:404, value:{value:7}},
    {name:'reopened-request', body:'{"first":1}', reuse:true, value:{second:2}},
    {name:'aborted-request', body:'{"first":1}', abort:true, value:{second:2}},
];

function serveXhrJson(request,response) {
    const name=new URL(request.url,'http://fixture.test').pathname.slice(1);
    const fixture=xhrJsonCases.find(f=>f.name===name);
    if(name==='second')return response.writeHead(200,{'Content-Type':'application/json'}).end('{"second":2}');
    if(!fixture)return response.writeHead(404).end();
    response.writeHead(fixture.status||200,{'Content-Type':fixture.mime||'application/json'});
    response.end(fixture.body);
}

function xhrJsonHtml(fixture,origin) {
    const type=fixture.type===undefined?'json':fixture.type;
    return `<!doctype html><style>body{margin:0}#state{width:80px;height:80px;background:red}</style><div id=state></div><script>
    var xhr=new XMLHttpRequest(),phase=0,first=null,readyValue=null,beforeDone=true,reopened=true,aborted=false;
    function read(){return this.response}
    xhr.responseType=${JSON.stringify(type)};
    xhr.onreadystatechange=function(){if(xhr.readyState===4)readyValue=read.call(xhr);
        else if(${type==='json'})beforeDone=beforeDone&&read.call(xhr)===null};
    xhr.onabort=function(){aborted=read.call(xhr)===null};
    xhr.onload=function(){try{
        var value=read.call(xhr),same=value===readyValue&&read.call(xhr)===value;
        if(${!!fixture.reuse}&&phase===0){first=value;phase++;xhr.open('GET',${JSON.stringify(origin+'/second')});reopened=read.call(xhr)===null;xhr.send();return}
        if(value&&typeof value==='object'){value.__cacheTest=17;if(typeof gc==='function')gc();same=same&&read.call(xhr).__cacheTest===17;delete value.__cacheTest}
        var textGuard=false;try{xhr.responseText;textGuard=${type!=='json'}}catch(e){textGuard=${type==='json'}&&e.name==='InvalidStateError'}
        window.probeResult={value:value,cached:same,beforeDone:beforeDone,textGuard:textGuard,
            reopened:reopened,prior:${!!fixture.reuse}?!!first&&first.first===1&&first!==value:true,
            aborted:${!!fixture.abort}?aborted:true,status:xhr.status};
        document.getElementById('state').style.background='lime';console.log('XHR_JSON_RESULT',JSON.stringify(window.probeResult));
    }catch(e){console.log('XHR_JSON_RESULT',JSON.stringify({error:String(e)}))}};
    xhr.open('GET',${JSON.stringify(origin+'/'+fixture.name)});xhr.send();
    ${fixture.abort?`xhr.abort();xhr.open('GET',${JSON.stringify(origin+'/second')});reopened=read.call(xhr)===null;xhr.send();`:''}
    </script>`;
}

function xhrJsonExpected(fixture) {
    return {value:fixture.value,cached:true,beforeDone:true,textGuard:true,reopened:true,
        prior:true,aborted:true,status:fixture.reuse||fixture.abort?200:fixture.status||200};
}

module.exports={xhrJsonCases,serveXhrJson,xhrJsonHtml,xhrJsonExpected};
