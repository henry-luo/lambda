(123, "hello" + " world", string(123), "a" + "b" is string, string(40.5) is string)
[123, "hello" + " world", string(123), "a" + "b" is string]
{a:123, b:"hello" + " world", c:string(123), d:"a" + "b" is string}
<elmt a:123, b:"hello" + " world", c:string(123), d:"a" + "b" is string>

let ls = (1, 2.5, true, "hi");
ls[0]; ls[1]; (ls[3], ls[2]);

let arr = [21, 22, 23];
arr[0]; arr[1]; [21,22] is array;

let m = {a:10, b:12.5, c:false, d:"hii"};
m.a; m.b; (m.c, m.d); {a:123} is map;