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
