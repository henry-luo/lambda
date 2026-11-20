const cases=[
 ['parameter mutations update their owner',`
 const u=new URL('https://example.test/path?b=2&a=x%20y#frag'),p=u.searchParams;
 check(p===u.searchParams&&p.get('a')==='x y');p.set('config',encodeURIComponent('{"x":"a+b c"}'));
 check(u.search.includes('config=')&&new URL(u.href).searchParams.get('config')===encodeURIComponent('{"x":"a+b c"}'));
 p.append('a','z');check(u.href.includes('a=z'));p.delete('b');check(!u.search.includes('b='));
 p.sort();check(u.search.startsWith('?a=x+y&a=z&config=')&&u.hash==='#frag');
 u.search='?x=a#b&space=c d';check(p.get('x')==='a#b'&&p.get('space')==='c d'&&u.hash==='#frag'&&u.search.includes('a%23b'));`],
 ['URL query replacement retains the parameter object',`
 const u=new URL('https://example.test/?old=1'),p=u.searchParams;
 u.search='?new=x%20y&new=z';check(p===u.searchParams&&p.getAll('new').join('|')==='x y|z'&&!p.has('old'));
 u.href='https://other.test:443/new?one=1#part';check(p===u.searchParams&&p.get('one')==='1'&&!p.has('new'));
 check(u.hostname==='other.test'&&u.port===''&&u.pathname==='/new'&&u.hash==='#part');
 p.set('one','two');check(u.toString()==='https://other.test/new?one=two#part');
 const before=u.href;let invalid=false;try{u.href='http://['}catch(error){invalid=error instanceof TypeError}check(invalid&&u.href===before&&p.get('one')==='two');`],
 ['no-op mutations still apply form serialization',`
 const u=new URL('https://example.test/?x=a%20b~');u.searchParams.sort();check(u.href==='https://example.test/?x=a+b%7E');
 const empty=new URL('https://example.test/?#frag');empty.searchParams.delete('absent');check(empty.href==='https://example.test/#frag');
 u.search='';check(u.searchParams.size===0&&u.href==='https://example.test/');`],
 ['public shadows and freezing do not replace URL state',`
 const u=new URL('https://example.test/?x=1');Object.defineProperty(u,'href',{value:'shadow'});
 u.searchParams.set('x','two');check(u.href==='shadow'&&URL.prototype.toString.call(u)==='https://example.test/?x=two');
 const v=new URL('https://example.test/path'),p=v.searchParams;Object.freeze(v);p.append('x','a b');
 check(JSON.stringify(p)==='{}');
 check(v.href==='https://example.test/path?x=a+b'&&v.searchParams===p&&Reflect.ownKeys(p).every(k=>typeof k!=='string'||!k.includes('URL record')));`],
 ['callback collection retains both URL and parameters',`
 const u=new URL('https://example.test/?a=1'),p=u.searchParams;
 p.set({toString(){if(typeof gc==='function')gc();return 'a'}},{toString(){if(typeof gc==='function')gc();return 'b c'}});
 check(u.search==='?a=b+c'&&p.get('a')==='b c');
 u.search={toString(){if(typeof gc==='function')gc();return '?next=2'}};
 check(p.get('next')==='2'&&u.href==='https://example.test/?next=2');`],
];
function fixtureHtml(source){return `<!doctype html><style>html,body{margin:0}#result{width:30px;height:30px;background:red}</style><div id=result></div><script>
let okay=true;function check(value){okay=okay&&!!value}try{${source}}catch(error){okay=false;console.log('URL_FIXTURE_ERROR',error.name,error.message)}
if(okay)document.getElementById('result').style.background='lime';</script>`}
module.exports={cases,fixtureHtml};
