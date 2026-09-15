// A `return` inside `for await` evaluates its value, then awaits the
// iterator's `return()` (AsyncIteratorClose) before completing. The await
// suspends the async state machine, so the evaluated value must survive the
// resume; it used to come back as whatever the register last held.

function closable(mark) {
  let sent = false;
  const it = {
    next() {
      if (sent) return Promise.resolve({ done: true });
      sent = true;
      return Promise.resolve({ value: 1, done: false });
    },
    return() {
      return Promise.resolve().then(function () { globalThis[mark] = true; return { closed: mark }; });
    },
  };
  it[Symbol.asyncIterator] = function () { return it; };
  return it;
}

// 1. Single loop: the returned value, not the close result, completes the call.
async function single() {
  for await (const v of closable("singleClosed")) { return "single:" + v; }
}

// 2. Nested loops: both closes await in turn before the value completes.
async function nested() {
  for await (const a of closable("outerClosed")) {
    for await (const b of closable("innerClosed")) { return "nested:" + (a + b); }
  }
}

// 3. An object value is carried by identity across the closes.
const marker = { tag: "marker" };
async function identity() {
  for await (const v of closable("identityClosed")) { return marker; }
}

(async function () {
  console.log(await single());
  console.log(globalThis.singleClosed === true);
  console.log(await nested());
  console.log(globalThis.outerClosed === true && globalThis.innerClosed === true);
  console.log((await identity()) === marker);
})();
