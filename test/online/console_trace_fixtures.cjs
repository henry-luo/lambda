const {scriptResultFixture}=require('./script_result_fixture.cjs');
const fixtures=[
 {name:'existing console methods remain bindable',body:`const names=['log','info','debug','warn','error','group','groupCollapsed','groupEnd','dir','table','count','countReset','time','timeEnd','timeLog'];
  return {ready:names.every(name=>typeof console[name]==='function'&&typeof console[name].bind(console)==='function')};`,expected:{ready:true}},
 {name:'console trace binds and emits its label',body:`var t;const trace=(t=console.trace).bind(console);
  return {callable:typeof trace==='function',length:console.trace.length,result:trace('trace-bind-probe')===undefined};`,
  expected:{callable:true,length:0,result:true},output:'Trace: trace-bind-probe'},
 {name:'captured console trace survives replacement and collection',body:`const original=console.trace,saved=original.bind(null);
  console.trace=function(){throw new Error('replacement must not run')};if(typeof gc==='function')gc();
  const result=saved('trace-captured-probe')===undefined;console.trace=original;
  return {result,restored:console.trace===original};`,expected:{result:true,restored:true},output:'Trace: trace-captured-probe'},
 {name:'console trace data avoids label coercion',body:`let coerced=0;
  const value={toString(){coerced++;throw new Error('trace must not convert data to a counter label')}};
  const symbol=console.trace(Symbol('trace-symbol-probe'))===undefined,object=console.trace(value)===undefined;
  return {symbol,object,coerced};`,expected:{symbol:true,object:true,coerced:0}},
];
function fixtureHtml(f){return scriptResultFixture(`const result=(function(){${f.body}})();
 console.log('CONSOLE_TRACE_DETAILS',JSON.stringify(result));check(JSON.stringify(result)===${JSON.stringify(JSON.stringify(f.expected))});`,
 'CONSOLE_TRACE_ERROR','CONSOLE_TRACE_RESULT');}
module.exports={fixtures,fixtureHtml};
