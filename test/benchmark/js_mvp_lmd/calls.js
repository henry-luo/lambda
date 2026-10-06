function fib(n){return n<2?n:fib(n-1)+fib(n-2)} function work(){let s=0;for(let i=0;i<100;i++)s+=fib(20);return s} work()
