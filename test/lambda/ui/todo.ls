// Reactive Todo App — Lambda Script with view and edit templates
// Demonstrates Phase 8: extended interactions (delete, clear completed)
// Run: ./lambda.exe view test/lambda/ui/todo.ls

let data^err = input('./test/lambda/ui/todos.json', 'json')

// ============================================================================
// Templates — reactive components with state and event handlers
// ============================================================================

// Todo item component: clicking toggles done/not-done, delete emits to parent
view <todo_item> state toggled: false {
  let done = if (toggled) (!~.done) else ~.done
  let check_mark = if (done) "✓" else "○"
  let done_class = if (done) "todo-item done" else "todo-item"
  <li class:done_class
    <span class:"checkbox"; check_mark>
    <span class:"todo-text"; ~.text>
    <span class:"delete-btn"; "×">
  >
}
on click(evt) {
  if (evt.target_class == "delete-btn") {
    emit("delete_item", ~)
  } else {
    toggled = not toggled
  }
}

// Todo list section: edit template for model mutation (delete, clear completed, add)
edit <todo_list> state new_text: "" {
  let items = ~.items
  let item_count = len(items)
  let done_count = len(for (i in items where i.done) i)
  let count_text = string(done_count) ++ "/" ++ string(item_count)
  <div class:"todo-list"
    <div class:"list-header"
      <span class:"list-name"; ~.name>
      <span class:"list-count"; count_text>
    >
    <ul class:"items"
      for (item in items)
        apply(<todo_item text:item.text, done:item.done, id:item.id>)
    >
    <div class:"add-row"
      <input type:"text", class:"new-item-input", placeholder:"Add a task...", value:new_text>
      <button class:"add-btn"; "Add">
    >
    if (done_count > 0) {
      <div class:"list-actions"
        <button class:"clear-done-btn"; "Clear completed">
      >
    }
  >
}
on click(evt) {
  if (evt.target_class == "add-btn") {
    if (new_text != "") {
      let next_id = len(~.items) + 1
      let new_item = {id: next_id, text: new_text, done: false}
      ~.items = ~.items ++ [new_item]
      new_text = ""
    }
  }
  if (evt.target_class == "clear-done-btn") {
    ~.items = for (item in ~.items where not item.done) item
  }
}
on input(evt) {
  let pos = evt.caret_pos
  new_text = slice(new_text, 0, pos) ++ evt.char ++ slice(new_text, pos, len(new_text))
}
on keydown(evt) {
  if (evt.key == "Backspace") {
    let pos = evt.caret_pos
    if (pos > 0) {
      new_text = slice(new_text, 0, pos - 1) ++ slice(new_text, pos, len(new_text))
    }
  }
  if (evt.key == "Enter") {
    if (new_text != "") {
      let next_id = len(~.items) + 1
      let new_item = {id: next_id, text: new_text, done: false}
      ~.items = ~.items ++ [new_item]
      new_text = ""
    }
  }
}
on delete_item(evt) {
  ~.items = for (item in ~.items where item.id != evt.id) item
}

// ============================================================================
// HTML page — outer shell built directly, items rendered via apply()
// ============================================================================

<html lang:"en"
<head
  <meta charset:"UTF-8">
  <title "Lambda Todo App">
  <style
    "* { margin: 0; padding: 0; box-sizing: border-box; }
    body {
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, 'Helvetica Neue', Arial, sans-serif;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      min-height: 100vh;
      padding: 40px 20px;
    }
    .container {
      max-width: 600px;
      margin: 0 auto;
      background: white;
      border-radius: 16px;
      box-shadow: 0 20px 60px rgba(0,0,0,0.3);
      overflow: hidden;
    }
    .header {
      background: linear-gradient(135deg, #5c6bc0 0%, #7e57c2 100%);
      color: white;
      padding: 30px;
      text-align: center;
    }
    .header h1 { font-size: 1.8em; font-weight: 700; margin-bottom: 5px; }
    .header p  { font-size: 0.9em; opacity: 0.85; }
    .content { padding: 20px 30px 30px; }
    .todo-list { margin-bottom: 15px; }
    .list-header {
      display: flex;
      align-items: center;
      gap: 10px;
      padding: 12px 15px;
      background: #f1f3f5;
      border-radius: 10px;
    }
    .list-name {
      font-weight: 600;
      font-size: 1.05em;
      color: #333;
      flex: 1;
    }
    .list-count {
      font-size: 0.85em;
      color: #888;
      background: #dee2e6;
      padding: 2px 10px;
      border-radius: 12px;
    }
    .items {
      list-style: none;
      padding: 5px 0 5px 20px;
    }
    .todo-item {
      display: flex;
      align-items: center;
      gap: 10px;
      padding: 10px 12px;
      border-bottom: 1px solid #f0f0f0;
      cursor: pointer;
      user-select: none;
    }
    .todo-item:hover { background: #f8f9fa; }
    .checkbox {
      width: 24px;
      height: 24px;
      border: 2px solid #ccc;
      border-radius: 6px;
      display: flex;
      align-items: center;
      justify-content: center;
      font-size: 0.85em;
      color: white;
      flex-shrink: 0;
    }
    .todo-item.done .checkbox {
      background: #66bb6a;
      border-color: #66bb6a;
    }
    .todo-text {
      flex: 1;
      font-size: 0.95em;
      color: #333;
    }
    .todo-item.done .todo-text {
      text-decoration: line-through;
      color: #aaa;
    }
    .delete-btn {
      width: 24px;
      height: 24px;
      display: flex;
      align-items: center;
      justify-content: center;
      font-size: 1.1em;
      color: #ccc;
      cursor: pointer;
      border-radius: 4px;
      flex-shrink: 0;
    }
    .delete-btn:hover {
      color: #e53935;
      background: #ffebee;
    }
    .list-actions {
      padding: 8px 15px;
      text-align: right;
    }
    .clear-done-btn {
      font-size: 0.85em;
      color: #888;
      background: #f1f3f5;
      border: 1px solid #dee2e6;
      border-radius: 6px;
      padding: 4px 12px;
      cursor: pointer;
    }
    .clear-done-btn:hover {
      background: #e9ecef;
      color: #555;
    }
    .add-row {
      display: flex;
      gap: 8px;
      padding: 10px 15px;
      align-items: center;
    }
    .new-item-input {
      flex: 1;
      font-size: 0.9em;
      padding: 6px 10px;
      border: 1px solid #dee2e6;
      border-radius: 6px;
      color: #333;
    }
    .add-btn {
      font-size: 0.85em;
      color: white;
      background: #5c6bc0;
      border: none;
      border-radius: 6px;
      padding: 6px 14px;
      cursor: pointer;
      font-weight: 600;
    }
    .add-btn:hover {
      background: #7e57c2;
    }
    .footer {
      text-align: center;
      padding: 15px;
      color: #aaa;
      font-size: 0.8em;
      border-top: 1px solid #f0f0f0;
    }"
  >
>
<body
  <div class:"container"
    <div class:"header"
      <h1 data.title>
      <p "Reactive UI — click to toggle, × to delete">
    >
    <div class:"content"
      for (lst in data.lists)
        apply(<todo_list name:lst.name, items:lst.items>, {mode: "edit"})
    >
    <div class:"footer"
      let all_items = [for (lst in data.lists) for (item in lst.items) item]
      let total = len(all_items)
      let done = len(for (item in all_items where item.done) item)
      (string(done) ++ " of " ++ string(total) ++ " tasks completed")
    >
  >
>
>
