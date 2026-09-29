# 9. Reactive UI

In [Chapter 8](08_Rendering_and_Viewing.md) a script built a page once. In this chapter the page reacts: **templates** turn data into elements, **handlers** respond to clicks and keys, and after each handler Lambda re-renders only what changed. You build a counter and then a small todo list, and finish by testing the list without opening a window.

## Templates and `apply`

A template is declared with `view`, a **pattern** saying which values it handles, and a body that turns the matched value, written `~`, into an element. `apply(x)` finds the template whose pattern fits `x` best and returns what it builds. Save this as `reading.ls`:

```lambda
// reading.ls
view <book> { <li <b ~.title> " by " ~.author> }
view <paper> { <li <i ~.title> " in " ~.journal> }

let items = [
    <book title: "SICP", author: "Harold Abelson">,
    <paper title: "Go To Statement Considered Harmful", journal: "CACM">
];
<ul for (x in items) apply(x)>
```

```bash
lambda reading.ls
```

```text
<ul
  <li
    <b
      "SICP">
    " by Harold Abelson">
  <li
    <i
      "Go To Statement Considered Harmful">
    " in CACM">>
```

The list never names a template: `apply` dispatches on the tag of each item, the way XSLT's `apply-templates` does. A pattern can be an element tag, an element with pinned attribute values such as `<input type: 'checkbox'>`, or a type such as `int` or `string`; when several patterns fit, the most specific one wins. A template body is pure, like the body of an `fn`.

## A Counter: State and Handlers

A template can keep **state** for each place it is applied, and react to events with **`on` handlers** written after its body. Save this as `counter.ls`:

```lambda
// counter.ls
view <counter> state n: 0 {
    <button class: "counter", ~.label ++ ": " ++ string(n)>
}
on click(evt) {
    n = n + 1
}

<html
    <body
        apply(<counter label: "Apples">)
        apply(<counter label: "Pears">)
    >
>
```

Run as a script, it prints the page as it first appears:

```bash
lambda counter.ls
```

```text
<html
  <body
    <button class: "counter",
      "Apples: 0">
    <button class: "counter",
      "Pears: 0">>>
```

To click the buttons, open the page in the viewer:

```bash
lambda view counter.ls
```

A click on a button runs its `on click` handler, which adds one to `n`. Lambda then re-runs the body of that one template instance and patches the button in place. The other button keeps its own count: each `apply` call creates an instance with its own copy of the state.

The body and the handlers have different jobs. The body is a pure transformation from the item and its state to an element; a handler is a procedure, and handlers are the only place where state changes (S12.1.3). Assigning state in the body is an error. Save this as `impure.ls`:

```lambda error=E224
// impure.ls
view <counter> state n: 0 {
    n = n + 1;
    <button ~.label ++ ": " ++ string(n)>
}

apply(<counter label: "Apples">)
```

```bash
lambda impure.ls
```

```text partial
impure.ls:3:5: error[E224]: assignment is only allowed inside a procedure (pn)
```

## A Todo List: `edit` Templates and `emit`

A `view` template may keep state, but it cannot change the data it was given. An **`edit` template** can: its handlers assign into the model through `~`. Edit templates are chosen only when you ask for them, with `apply(x, {mode: "edit"})`. In this list, the list template owns the tasks, and each item asks the list to change them. Save this as `todo.ls`:

```lambda
// todo.ls
view <todo_item> {
    <li class: if (~.done) "item done" else "item",
        <span class: "text", ~.text>
        <span class: "remove", "×">
    >
}
on click(evt) {
    let action = if (evt.target_class == "remove") "remove_item" else "toggle_item"
    emit(action, ~)
}

edit <todo_list> {
    <ul for (i, item in ~.items) apply(<todo_item text: item.text, done: item.done, index: i>)>
}
on toggle_item(evt) {
    ~.items = [for (i, item in ~.items)
        if (i == evt.index) {text: item.text, done: not item.done} else item]
}
on remove_item(evt) {
    ~.items = [for (i, item in ~.items where i != evt.index) item]
}

let tasks = [{text: "Read chapter 9", done: true}, {text: "Write a template", done: false}]
let css = ".done .text { text-decoration: line-through; color: #999; }
    .remove { color: #c00; margin-left: 8px; }";

<html
    <head <style css>>
    <body
        <h1 "To do">
        apply(<todo_list items: tasks>, {mode: "edit"})
    >
>
```

The finished task renders with the `done` class:

```bash
lambda todo.ls
```

```text partial
      <li class: "item done",
        <span class: "text",
          "Read chapter 9">
```

Open it with `lambda view todo.ls`, then click a task to strike it through or its × to remove it. A click travels like this:

1. The item's `on click` handler reads `evt.target_class` to see which part was clicked, and calls `emit("toggle_item", ~)` or `emit("remove_item", ~)`.
2. `emit(name, payload)` raises a custom event that travels up the page to the nearest template with an `on name` handler, here the list. The payload arrives as that handler's `evt`, so `evt.index` is the `index` attribute the list gave the item.
3. The list's handler builds a new array and assigns it to `~.items`. Lambda marks the list as changed, re-runs its body, and the page updates.

`for (i, item in ~.items)` binds each item's position to `i` as well as the item to `item`. The item template keeps no state of its own, and that is deliberate: a template's state belongs to the item it was applied to, and each time the list's body runs it applies fresh `<todo_item>` elements, whose state would start over. Keep anything that must survive a re-render in the model.

## Typing Into the Page

Now add a text box: the list keeps what is typed in a `draft` state, and Enter turns it into a task. Replace `todo.ls` with this version; the `draft` state, the `<input>` and the `input` and `keydown` handlers are new:

```lambda
// todo.ls
view <todo_item> {
    <li class: if (~.done) "item done" else "item",
        <span class: "text", ~.text>
        <span class: "remove", "×">
    >
}
on click(evt) {
    let action = if (evt.target_class == "remove") "remove_item" else "toggle_item"
    emit(action, ~)
}

edit <todo_list> state draft: "" {
    <div
        <ul for (i, item in ~.items) apply(<todo_item text: item.text, done: item.done, index: i>)>
        <input type: "text", class: "new", placeholder: "Add a task", value: draft>
    >
}
on input(evt) {
    draft = evt.target.value
}
on keydown(evt) {
    if (evt.key == "Enter" and draft != "") {
        ~.items = ~.items ++ [{text: draft, done: false}]
        draft = ""
    }
}
on toggle_item(evt) {
    ~.items = [for (i, item in ~.items)
        if (i == evt.index) {text: item.text, done: not item.done} else item]
}
on remove_item(evt) {
    ~.items = [for (i, item in ~.items where i != evt.index) item]
}

let tasks = [{text: "Read chapter 9", done: true}, {text: "Write a template", done: false}]
let css = ".done .text { text-decoration: line-through; color: #999; }
    .remove { color: #c00; margin-left: 8px; }";

<html
    <head <style css>>
    <body
        <h1 "To do">
        apply(<todo_list items: tasks>, {mode: "edit"})
    >
>
```

```bash
lambda view todo.ls
```

Each keystroke in the box fires `input`, and the handler copies the box's current text, `evt.target.value`, into `draft`. In `keydown`, `evt.key` names the key: Enter appends a task to the model and clears `draft`, and since the body renders `value: draft`, the box empties too.

## Event Fields

Every handler receives its event as `evt`. The fields you will use most:

| Field | Meaning |
|---|---|
| `type` | The event name, such as `"click"` |
| `target` | The element that received the event; `evt.target.value` is the current text of an input |
| `target_tag`, `target_class` | The tag and `class` of that element |
| `target_parent_class`, `target_text` | The class of its parent, and the text that was clicked |
| `key` | The key pressed, in `keydown` |
| `shiftKey`, `ctrlKey`, `altKey`, `metaKey` | Modifier keys held during a click or a key press |
| `char`, `caret_pos` | The character typed and the caret position, in `input` |

The engine delivers `click`, `input`, `keydown`, `paste`, `cut`, `blur` and the drag-and-drop events, and any name raised with `emit`, whose `evt` is the payload. A handler may return `'prevent-default'` to stop the engine's own action, such as submitting a form or editing the text of an input.

## Testing Without a Window

`lambda view` can also run without a window and replay a script of simulated events, checking the page after each step. Save this as `todo_test.json`:

```json file=todo_test.json
{
  "name": "Todo list",
  "events": [
    {"type": "assert_count", "target": {"selector": "li"}, "count": 2},
    {"type": "click", "target": {"text": "Write a template"}},
    {"type": "assert_class", "target": {"selector": "li:nth-of-type(2)"}, "class": "done"},
    {"type": "click", "target": {"selector": ".new"}},
    {"type": "type", "target": {"selector": ".new"}, "text": "Test the page"},
    {"type": "key_press", "key": "Enter"},
    {"type": "assert_count", "target": {"selector": "li"}, "count": 3},
    {"type": "click", "target": {"selector": "li:nth-of-type(1) .remove"}},
    {"type": "assert_text", "target": {"selector": "li .text"}, "equals": "Write a template"}
  ]
}
```

```bash
lambda view todo.ls --headless --event-file todo_test.json
```

```text partial
 EVENT SIMULATION RESULTS
 Test: Todo list
========================================
 Events executed: 9
 Assertions: 4 passed, 0 failed
 Result: PASS
```

Each event has a `type` and, usually, a `target`, found by CSS selector or by its visible text. `click`, `type` and `key_press` act on the page; `assert_count`, `assert_class` and `assert_text` check it. When an assertion fails, the log shows the expected and the actual value, the result reads `FAIL`, and the command exits with status 1, so the file can serve as a test in a build.

## What You Learned

- `view <pattern> { … }` declares a template, `~` is the matched item, and `apply(x)` renders `x` with the best-matching template.
- `state` gives each template instance its own values; `on` handlers are procedures that change them, after which the body re-runs.
- The template body is pure: only handlers change anything.
- `edit` templates, applied with `{mode: "edit"}`, assign into the model through `~`.
- `emit(name, payload)` sends a custom event up to the nearest template that handles it.
- `lambda view app.ls` runs the page; `--headless --event-file` tests it without a window.

[Reactive_UI.md](../Reactive_UI.md) describes the template model in full: patterns and specificity, named templates, state, events and the reactive loop. Next, [Chapter 10](10_Packages_and_Beyond.md) tours the bundled packages, the editor and JavaScript.
