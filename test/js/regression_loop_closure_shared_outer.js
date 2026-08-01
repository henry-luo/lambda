// A per-iteration loop binding must stay private while sibling closures share
// the outer mutable binding established by the enclosing function.
function factory() {
  let current = null;
  const listeners = [];

  function set_current(value) {
    current = value;
  }

  for (const label of ["first", "second"]) {
    listeners.push(() => current ? label + ":" + current.name : label + ":missing");
  }

  set_current({ name: "ready" });
  console.log(listeners[0]());
  console.log(listeners[1]());

  const suffix = "!";
  const actions = [];
  for (const button of ["skip", "one", "two"]) {
    const action = button;
    if (action === "skip") continue;
    actions.push(() => action + suffix);
  }
  console.log(actions[0]());
  console.log(actions[1]());

  let deferred = null;
  const deferred_actions = [];
  function set_deferred(value) {
    deferred = value;
  }
  for (const button of ["skip", "one", "two"]) {
    const action = button;
    if (action === "skip") continue;
    deferred_actions.push(() => action + ":" + deferred);
  }
  set_deferred("ready");
  console.log(deferred_actions[0]());
  console.log(deferred_actions[1]());

  let drawing = null;
  const state = {};
  function publish() {
    state.published = drawing.name;
  }
  function report_failure() {
    drawing = null;
  }
  const handlers = [];
  for (const button of ["skip", "one", "two"]) {
    const action = button;
    if (action === "skip") continue;
    handlers.push(() => {
      try {
        state.action = action;
        if (!drawing) throw new Error("drawing unavailable");
        publish();
        return action + ":" + drawing.name;
      } catch (error) {
        report_failure(error);
        return state.action + ":failed";
      }
    });
  }
  drawing = { name: "ready" };
  console.log(handlers[0]());
  console.log(handlers[1]());
}

factory();
