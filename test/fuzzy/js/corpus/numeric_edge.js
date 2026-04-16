// Edge case: numeric coercions and boundaries
var a = -0;
var b = +0;
a === b;
Object.is(a, b);
1/a; 1/b;

NaN === NaN;
NaN !== NaN;
isNaN(NaN);
Number.isNaN(NaN);

Infinity + 1;
-Infinity - 1;
Infinity - Infinity;
Infinity * 0;
0 / 0;

Number.MAX_SAFE_INTEGER;
Number.MAX_SAFE_INTEGER + 1;
Number.MAX_SAFE_INTEGER + 2;

parseInt("", 10);
parseInt("0x");
parseInt("  123  ");
parseFloat(".");
parseFloat("1e");

~~NaN;
~~Infinity;
~~(-Infinity);
~~3.9;
~~(-3.9);

"5" - 3;
"5" + 3;
"5" * "2";
true + true;
false + null;
undefined + 1;
null + undefined;
[] + [];
[] + {};
{} + [];
