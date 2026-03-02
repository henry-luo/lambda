// test importing pub type from a module
import .mod_pub_types

// test basic variable import
x;

// test type alias import
let s: Score = 99
s;

// test object type creation from imported type
let c = {Counter value: 5}
c.value;
c.double();
c is Counter
