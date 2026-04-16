// Edge case: empty/minimal/bizarre programs
;
;;
;;;

// Empty blocks
{}
if (true) {}
while (false) {}
for (;;) { break; }
try {} catch(e) {} finally {}
switch(0) {}

// Only comments
// comment 1
/* comment 2 */
/* multi
   line
   comment */

// Deeply nested expressions
(((((((((((1)))))))))));
[[[[[[[[[[1]]]]]]]]]];

// Chained property access on null/undefined
try { null.a.b.c.d; } catch(e) {}
try { undefined.a.b.c.d; } catch(e) {}
try { (void 0).x; } catch(e) {}

// Comma operator
(1, 2, 3, 4, 5);

// Labeled statement
loop: for (var i = 0; i < 3; i++) {
  inner: for (var j = 0; j < 3; j++) {
    if (j === 1) continue loop;
    if (i === 2) break loop;
  }
}

// typeof on undeclared
typeof undeclaredVar;
typeof undefined;
typeof null;
typeof NaN;

// void
void 0;
void "hello";
void function() {};

// delete
var obj = {a: 1, b: 2};
delete obj.a;
delete obj["b"];
delete obj.nonexistent;

// Unusual assignments
var x;
x = x = x = 1;
x += x -= x *= 2;

// Array holes
var sparse = [1, , , 4];
sparse.length;
sparse[1];

// with statement (sloppy mode)
try {
  var wObj = {a: 1, b: 2};
} catch(e) {}

// Arguments edge cases
function argTest() {
  arguments.length;
  arguments[0];
  arguments[999];
  return arguments;
}
argTest(1, 2, 3);
argTest();
