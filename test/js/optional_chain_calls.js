var optionalChainChecks = [];
var descriptor = Object.getOwnPropertyDescriptor({window:1}, 'window');
optionalChainChecks.push(descriptor?.get?.toString().includes('native') === undefined);
var absent = null;
optionalChainChecks.push(absent?.method().next().value === undefined);
var skipped = 0;
optionalChainChecks.push(absent?.method()[skipped++].next(skipped++) === undefined && skipped === 0);
var receiver = {value:7, method:function(){return {read:function(){return this.value;},value:this.value};}};
optionalChainChecks.push(receiver?.method().read() === 7);
var calls = 0;
var callable = {make:function(){calls++;return {read:function(){return 9;}};}};
optionalChainChecks.push(callable?.make().read() === 9 && calls === 1);
var groupedThrows = false;
try { (absent?.method()).next(); } catch (error) { groupedThrows = error instanceof TypeError; }
optionalChainChecks.push(groupedThrows);
optionalChainChecks.push(absent?.method()() === undefined);
var groupedCallThrows = false;
try { (absent?.method)(); } catch (error) { groupedCallThrows = error instanceof TypeError; }
optionalChainChecks.push(groupedCallThrows);
for (var check of optionalChainChecks) console.log(check);
optionalChainChecks.every(function(value){return value;});
