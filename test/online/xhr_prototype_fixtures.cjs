const {scriptResultFixture}=require('./script_result_fixture.cjs');
const fixtures=[
 {name:'XHR methods are shared prototype properties',body:`
    const prototype=XMLHttpRequest.prototype,first=new XMLHttpRequest(),second=new XMLHttpRequest();
    const names=['open','send','setRequestHeader','abort','getResponseHeader','getAllResponseHeaders','overrideMimeType'];
    return {published:names.every(name=>Object.hasOwn(prototype,name)&&typeof prototype[name]==='function'),
        inherited:names.every(name=>!Object.hasOwn(first,name)&&first[name]===prototype[name]),
        shared:names.every(name=>first[name]===second[name]),instance:first instanceof XMLHttpRequest};`,
    expected:{published:true,inherited:true,shared:true,instance:true}},
 {name:'XHR prototype wrappers affect existing and new instances',body:`
    const prototype=XMLHttpRequest.prototype,saved=prototype.open,first=new XMLHttpRequest();
    if(typeof saved!=='function')return {calls:-1,opened:false};
    let calls=0;
    try {
        prototype.open=new Proxy(saved,{apply(target,receiver,args){calls++;return Reflect.apply(target,receiver,args)}});
        const second=new XMLHttpRequest();
        first.open('GET','https://fixture.invalid/first',true);
        second.open('GET','https://fixture.invalid/second',true);
        prototype.setRequestHeader.call(first,'X-Test','kept');
        return {calls,opened:first.readyState===1&&second.readyState===1&&
            prototype.getAllResponseHeaders.call(first)===''&&prototype.getResponseHeader.call(second,'X-Test')===null};
    } finally {prototype.open=saved}`,expected:{calls:2,opened:true}},
];
function fixtureHtml(fixture){return scriptResultFixture(`
    const result=(function(){${fixture.body}})();window.__xhrPrototypeResult=result;
    console.log('XHR_PROTO_DETAILS',JSON.stringify(result));
    check(JSON.stringify(result)===${JSON.stringify(JSON.stringify(fixture.expected))});`,
    'XHR_PROTO_ERROR','XHR_PROTO_RESULT');}
module.exports={fixtures,fixtureHtml};
