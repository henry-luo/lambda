// RegExp compiler front-end validation and named-backref lowering.

function compile_status(pattern, flags) {
  try {
    new RegExp(pattern, flags);
    return "ok";
  } catch (e) {
    return e.name || "throw";
  }
}

console.log("t1:" +
  compile_status("a", "gg") + "," +
  compile_status("a", "uv") + "," +
  compile_status("a", "z"));

var flags = new RegExp("a", "mig").flags;
console.log("t2:" + flags);

console.log("t3:" +
  compile_status("(?<x>a)(?<x>b)", "u") + "," +
  compile_status("\\k<missing>", "u") + "," +
  compile_status("\\k<missing>", ""));

var named = new RegExp("(?<word>a)\\k<word>", "u");
console.log("t4:" + named.test("aa") + "," + named.test("ab"));

// Keep the compact character-class matcher on the observable RegExp paths:
// global/sticky lastIndex, source anchors, repetition, and Unicode whitespace.
var global_word = /\w/g;
global_word.lastIndex = 1;
console.log("t5:" + global_word.test("!a") + "," + global_word.lastIndex);

var sticky_word = /\w/y;
sticky_word.lastIndex = 1;
console.log("t6:" + sticky_word.test("!a") + "," + sticky_word.lastIndex);

var anchored_sticky = /^\w/y;
anchored_sticky.lastIndex = 1;
console.log("t7:" + anchored_sticky.test("!a") + "," + anchored_sticky.lastIndex);

console.log("t8:" + /^\W+$/.test("!@") + "," + /^\W+$/.test("a!"));
console.log("t9:" + /\s/.test(String.fromCodePoint(0x3000)));
