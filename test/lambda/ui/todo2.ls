// Reactive Todo App v2 — Two-column layout with file management
// Demonstrates Phase 5: multi-panel layout, file ops, inline editing
// Run: ./lambda.exe view test/lambda/ui/todo2.ls

let data_dir = './data/todo/'

// ============================================================================
// Templates — reactive components with state and event handlers
// ============================================================================

// Todo item component: toggle, delete, inline edit, notes textarea
view <todo_item> state toggled: false, editing: false, edit_text: "", edit_notes: "" {
  let done = if (toggled) (!~.done) else ~.done
  let check_mark = if (done) "✓" else "○"
  let done_class = if (done) "todo-item done" else "todo-item"
  let notes = if (~.notes != null) ~.notes else ""
  <li class:done_class
    <span class:"checkbox"; check_mark>
    <div class:"item-content"
      if (editing) {
        <input type:"text", class:"inline-edit", value:edit_text, autofocus:"true">
        <textarea class:"notes-edit", rows:"3", value:edit_notes, placeholder:"Add notes...">
      } else {
        <span class:"todo-text"; ~.text>
        if (notes != "") {
          <span class:"todo-notes"; notes>
        }
      }
    >
    <span class:"delete-btn"; "×">
  >
}
on click(evt) {
  if (evt.target_class == "delete-btn") {
    emit("delete_item", ~)
  } else if (evt.target_class == "todo-text" or evt.target_class == "todo-notes") {
    editing = true
    edit_text = ~.text
    edit_notes = if (~.notes != null) ~.notes else ""
  } else if (evt.target_class == "checkbox") {
    toggled = not toggled
  }
}
on input(evt) {
  if (editing) {
    if (evt.target_class == "notes-edit") {
      if (evt.selection_start != null) {
        let s = evt.selection_start
        let e = evt.selection_end
        edit_notes = slice(edit_notes, 0, s) ++ evt.char ++ slice(edit_notes, e, len(edit_notes))
      } else {
        let pos = evt.caret_pos
        edit_notes = slice(edit_notes, 0, pos) ++ evt.char ++ slice(edit_notes, pos, len(edit_notes))
      }
    } else {
      let pos = evt.caret_pos
      edit_text = slice(edit_text, 0, pos) ++ evt.char ++ slice(edit_text, pos, len(edit_text))
    }
  }
}
on keydown(evt) {
  if (editing) {
    if (evt.target_class == "notes-edit") {
      if (evt.key == "Backspace") {
        if (evt.selection_start != null) {
          let s = evt.selection_start
          let e = evt.selection_end
          edit_notes = slice(edit_notes, 0, s) ++ slice(edit_notes, e, len(edit_notes))
        } else {
          let pos = evt.caret_pos
          if (pos > 0) {
            edit_notes = slice(edit_notes, 0, pos - 1) ++ slice(edit_notes, pos, len(edit_notes))
          }
        }
      }
      if (evt.key == "Enter") {
        if (evt.selection_start != null) {
          let s = evt.selection_start
          let e = evt.selection_end
          edit_notes = slice(edit_notes, 0, s) ++ "\n" ++ slice(edit_notes, e, len(edit_notes))
        } else {
          let pos = evt.caret_pos
          edit_notes = slice(edit_notes, 0, pos) ++ "\n" ++ slice(edit_notes, pos, len(edit_notes))
        }
      }
      if (evt.key == "Escape") {
        emit("update_item", {id: ~.id, text: edit_text, notes: edit_notes})
        editing = false
      }
    } else {
      if (evt.key == "Backspace") {
        let pos = evt.caret_pos
        if (pos > 0) {
          edit_text = slice(edit_text, 0, pos - 1) ++ slice(edit_text, pos, len(edit_text))
        }
      }
      if (evt.key == "Enter") {
        emit("update_item", {id: ~.id, text: edit_text, notes: edit_notes})
        editing = false
      }
      if (evt.key == "Escape") {
        editing = false
      }
    }
  }
}
on paste(evt) {
  if (editing) {
    if (evt.target_class == "notes-edit") {
      if (evt.selection_start != null) {
        let s = evt.selection_start
        let e = evt.selection_end
        edit_notes = slice(edit_notes, 0, s) ++ evt.text ++ slice(edit_notes, e, len(edit_notes))
      } else {
        let pos = evt.caret_pos
        edit_notes = slice(edit_notes, 0, pos) ++ evt.text ++ slice(edit_notes, pos, len(edit_notes))
      }
    } else {
      let pos = evt.caret_pos
      edit_text = slice(edit_text, 0, pos) ++ evt.text ++ slice(edit_text, pos, len(edit_text))
    }
  }
}
on cut(evt) {
  if (editing) {
    if (evt.selection_start != null) {
      let s = evt.selection_start
      let e = evt.selection_end
      if (evt.target_class == "notes-edit") {
        edit_notes = slice(edit_notes, 0, s) ++ slice(edit_notes, e, len(edit_notes))
      } else {
        edit_text = slice(edit_text, 0, s) ++ slice(edit_text, e, len(edit_text))
      }
    }
  }
}
on blur(evt) {
  if (editing) {
    if (edit_text != "" and (edit_text != ~.text or edit_notes != ~.notes)) {
      emit("update_item", {id: ~.id, text: edit_text, notes: edit_notes})
    }
    editing = false
  }
}

// Todo list / category section: edit template for model mutation
edit <todo_list> state adding: false, new_text: "" {
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
        apply(<todo_item text:item.text, done:item.done, id:item.id, notes:item.notes>)
    >
    if (adding) {
      <div class:"add-inline"
        <input type:"text", class:"inline-add-input", placeholder:"What needs to be done?", value:new_text, autofocus:"true">
      >
    }
    <div class:"add-row"
      <span class:"add-icon"; "+">
    >
    if (done_count > 0) {
      <div class:"list-actions"
        <button class:"clear-done-btn"; "Clear completed">
      >
    }
  >
}
on click(evt) {
  if (evt.target_class == "add-icon") {
    adding = true
    new_text = ""
  }
  if (evt.target_class == "clear-done-btn") {
    ~.items = for (item in ~.items where not item.done) item
  }
}
on input(evt) {
  if (adding) {
    let pos = evt.caret_pos
    new_text = slice(new_text, 0, pos) ++ evt.char ++ slice(new_text, pos, len(new_text))
  }
}
on keydown(evt) {
  if (adding) {
    if (evt.key == "Backspace") {
      let pos = evt.caret_pos
      if (pos > 0) {
        new_text = slice(new_text, 0, pos - 1) ++ slice(new_text, pos, len(new_text))
      }
    }
    if (evt.key == "Enter") {
      if (new_text != "") {
        let max_id = if (len(~.items) == 0) 0 else max(for (i in ~.items) i.id)
        let next_id = max_id + 1
        let new_item = {id: next_id, text: new_text, done: false, notes: ""}
        ~.items = ~.items ++ [new_item]
        new_text = ""
        adding = false
      }
    }
    if (evt.key == "Escape") {
      adding = false
      new_text = ""
    }
  }
}
on delete_item(evt) {
  ~.items = for (item in ~.items where item.id != evt.id) item
}
on update_item(evt) {
  ~.items = for (item in ~.items)
    if (item.id == evt.id) {id: item.id, text: evt.text, done: item.done, notes: evt.notes}
    else item
}

// File entry in the sidebar
view <file_entry> state pending_delete: false {
  if (pending_delete) {
    <div class:"file-entry confirm-delete"
      <span class:"confirm-text"; "Delete?">
      <span class:"confirm-yes"; "Yes">
      <span class:"confirm-no"; "No">
    >
  } else {
    let active_class = if (~.name == ~.active) "file-entry active" else "file-entry"
    <div class:active_class
      <span class:"file-icon"; "📄">
      <span class:"file-name"; ~.name>
      <span class:"file-delete"; "×">
    >
  }
}
on click(evt) {
  if (evt.target_class == "file-delete") {
    pending_delete = true
  } else if (evt.target_class == "confirm-yes") {
    emit("delete_file", ~)
    pending_delete = false
  } else if (evt.target_class == "confirm-no") {
    pending_delete = false
  } else if (evt.target_class == "file-name" or evt.target_class == "file-icon" or evt.target_class == "file-entry active" or evt.target_class == "file-entry") {
    emit("select_file", ~)
  }
}

// Main app: edit template manages file list and active file
edit <todo_app> state active_file: "", creating_file: false, new_file_name: "" {
  let dir_listing^err = input(data_dir)
  let json_files = if (err != null) [] else for (f in dir_listing where ends_with(f.name, ".json")) f
  let file_names = for (f in json_files) replace(f.name, ".json", "")

  // auto-select first file if none active
  let eff_active = if (active_file == "" and len(file_names) > 0) file_names[0]
                   else active_file

  // load active file data
  let active_path = data_dir ++ eff_active ++ ".json"
  let file_data^err2 = if (eff_active != "") input(active_path, 'json') else null

  <html lang:"en"
  <head
    <meta charset:"UTF-8">
    <title "Lambda Todo App v2">
    <style
      "* { margin: 0; padding: 0; box-sizing: border-box; }
      body {
        font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, 'Helvetica Neue', Arial, sans-serif;
        background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
        min-height: 100vh;
        padding: 40px 20px;
      }
      .container {
        max-width: 900px;
        margin: 0 auto;
        background: white;
        border-radius: 16px;
        box-shadow: 0 20px 60px rgba(0,0,0,0.3);
        overflow: hidden;
        min-height: 600px;
        display: flex;
        flex-direction: column;
      }
      .header {
        background: linear-gradient(135deg, #5c6bc0 0%, #7e57c2 100%);
        color: white;
        padding: 20px 30px;
        text-align: center;
      }
      .header h1 { font-size: 1.5em; font-weight: 700; }
      .header p  { font-size: 0.85em; opacity: 0.85; margin-top: 4px; }

      /* Two-column layout */
      .app-layout {
        display: flex;
        flex-direction: row;
        flex: 1;
        min-height: 500px;
      }

      /* Left panel: file list */
      .file-panel {
        width: 180px;
        min-width: 180px;
        border-right: 1px solid #e0e0e0;
        background: #f8f9fa;
        display: flex;
        flex-direction: column;
        padding: 12px 0;
      }
      .panel-title {
        font-size: 0.75em;
        font-weight: 600;
        color: #999;
        text-transform: uppercase;
        letter-spacing: 0.05em;
        padding: 0 16px 8px;
      }
      .file-entry {
        display: flex;
        align-items: center;
        gap: 6px;
        padding: 8px 16px;
        cursor: pointer;
        font-size: 0.9em;
        color: #555;
      }
      .file-entry:hover { background: #e9ecef; }
      .file-entry.active {
        background: #e3e7fd;
        color: #5c6bc0;
        font-weight: 600;
      }
      .file-entry.confirm-delete {
        background: #fff3f3;
        padding: 8px 12px;
        gap: 4px;
      }
      .confirm-text {
        flex: 1;
        font-size: 0.85em;
        color: #e53935;
      }
      .confirm-yes {
        font-size: 0.8em;
        color: #e53935;
        cursor: pointer;
        font-weight: 600;
        padding: 2px 6px;
        border-radius: 4px;
      }
      .confirm-yes:hover { background: #ffcdd2; }
      .confirm-no {
        font-size: 0.8em;
        color: #888;
        cursor: pointer;
        padding: 2px 6px;
        border-radius: 4px;
      }
      .confirm-no:hover { background: #e0e0e0; }
      .file-icon { font-size: 0.9em; flex-shrink: 0; }
      .file-name { flex: 1; overflow: hidden; }
      .file-delete {
        font-size: 0.9em;
        color: #ccc;
        cursor: pointer;
        flex-shrink: 0;
      }
      .file-delete:hover { color: #e53935; }
      .new-file-btn {
        display: flex;
        align-items: center;
        gap: 6px;
        padding: 8px 16px;
        cursor: pointer;
        font-size: 0.85em;
        color: #5c6bc0;
        margin-top: 8px;
      }
      .new-file-btn:hover { background: #e9ecef; }
      .new-file-input {
        margin: 4px 12px;
        padding: 4px 8px;
        font-size: 0.85em;
        border: 1px solid #5c6bc0;
        border-radius: 4px;
        color: #333;
      }

      /* Right panel: todo lists */
      .todo-panel {
        flex: 1;
        overflow-y: auto;
        padding: 20px 24px;
      }
      .empty-panel {
        display: flex;
        align-items: center;
        justify-content: center;
        flex: 1;
        color: #aaa;
        font-size: 0.95em;
      }
      .todo-list { margin-bottom: 20px; }
      .list-header {
        display: flex;
        align-items: center;
        gap: 10px;
        padding: 10px 12px;
        background: #f1f3f5;
        border-radius: 8px;
      }
      .list-name {
        font-weight: 600;
        font-size: 1.0em;
        color: #333;
        flex: 1;
      }
      .list-count {
        font-size: 0.8em;
        color: #888;
        background: #dee2e6;
        padding: 2px 8px;
        border-radius: 10px;
      }
      .items {
        list-style: none;
        padding: 4px 0 4px 16px;
      }
      .todo-item {
        display: flex;
        align-items: center;
        gap: 8px;
        padding: 8px 10px;
        border-bottom: 1px solid #f0f0f0;
        cursor: pointer;
      }
      .todo-item:hover { background: #f8f9fa; }
      .checkbox {
        width: 22px;
        height: 22px;
        border: 2px solid #ccc;
        border-radius: 5px;
        display: flex;
        align-items: center;
        justify-content: center;
        font-size: 0.8em;
        color: white;
        flex-shrink: 0;
      }
      .todo-item.done .checkbox {
        background: #66bb6a;
        border-color: #66bb6a;
      }
      .todo-text {
        flex: 1;
        font-size: 0.9em;
        color: #333;
      }
      .todo-item.done .todo-text {
        text-decoration: line-through;
        color: #aaa;
      }
      .inline-edit {
        flex: 1;
        font-size: 0.9em;
        padding: 2px 6px;
        border: 1px solid #5c6bc0;
        border-radius: 4px;
        color: #333;
      }
      .delete-btn {
        width: 22px;
        height: 22px;
        display: flex;
        align-items: center;
        justify-content: center;
        font-size: 1.0em;
        color: #ccc;
        cursor: pointer;
        border-radius: 4px;
        flex-shrink: 0;
      }
      .delete-btn:hover {
        color: #e53935;
        background: #ffebee;
      }
      .item-content {
        flex: 1;
        display: flex;
        flex-direction: column;
        gap: 2px;
        min-width: 0;
      }
      .todo-notes {
        font-size: 0.8em;
        color: #888;
        overflow: hidden;
        white-space: nowrap;
      }
      .todo-item.done .todo-notes {
        color: #bbb;
      }
      .notes-edit {
        width: 100%;
        font-size: 0.85em;
        padding: 4px 6px;
        border: 1px solid #5c6bc0;
        border-radius: 4px;
        color: #555;
        margin-top: 4px;
        resize: none;
      }
      .add-inline {
        padding: 4px 10px 4px 46px;
      }
      .inline-add-input {
        width: 100%;
        font-size: 0.9em;
        padding: 4px 8px;
        border: 1px solid #5c6bc0;
        border-radius: 4px;
        color: #333;
      }
      .add-row {
        padding: 6px 12px;
      }
      .add-icon {
        display: inline-block;
        width: 22px;
        height: 22px;
        text-align: center;
        line-height: 22px;
        font-size: 1.1em;
        color: #5c6bc0;
        cursor: pointer;
        border-radius: 4px;
        font-weight: 600;
      }
      .add-icon:hover { background: #e3e7fd; }
      .list-actions {
        padding: 6px 12px;
        text-align: right;
      }
      .clear-done-btn {
        font-size: 0.8em;
        color: #888;
        background: #f1f3f5;
        border: 1px solid #dee2e6;
        border-radius: 6px;
        padding: 3px 10px;
        cursor: pointer;
      }
      .clear-done-btn:hover {
        background: #e9ecef;
        color: #555;
      }
      .footer {
        text-align: center;
        padding: 12px;
        color: #aaa;
        font-size: 0.75em;
        border-top: 1px solid #f0f0f0;
      }"
    >
  >
  <body
    <div class:"container"
      <div class:"header"
        <h1 "Todo App">
        <p "Two-column layout with file management">
      >
      <div class:"app-layout"
        <div class:"file-panel"
          <div class:"panel-title"; "Todo Files">
          for (name in file_names)
            apply(<file_entry name:name, active:eff_active>)
          if (creating_file) {
            <div class:"file-entry"
              <input type:"text", class:"new-file-input", placeholder:"filename", value:new_file_name, autofocus:"true">
            >
          } else {
            <div class:"new-file-btn"
              <span class:"add-icon"; "+">
              <span "New file">
            >
          }
        >
        if (file_data != null) {
          <div class:"todo-panel"
            for (lst in file_data.lists)
              apply(<todo_list name:lst.name, items:lst.items>, {mode: "edit"})
          >
        } else {
          <div class:"empty-panel"
            <span "Select a file to view">
          >
        }
      >
      <div class:"footer"
        if (file_data != null) {
          let all_items = [for (lst in file_data.lists) for (item in lst.items) item]
          let total = len(all_items)
          let done_total = len(for (item in all_items where item.done) item)
          (string(done_total) ++ " of " ++ string(total) ++ " tasks completed")
        } else {
          "No file selected"
        }
      >
    >
  >
  >
}
on click(evt) {
  if (evt.target_class == "new-file-btn" or
      (evt.target_class == "add-icon" and creating_file == false) or
      evt.target_text == "New file") {
    creating_file = true
    new_file_name = ""
  }
}
on input(evt) {
  if (creating_file) {
    let pos = evt.caret_pos
    new_file_name = slice(new_file_name, 0, pos) ++ evt.char ++ slice(new_file_name, pos, len(new_file_name))
  }
}
on keydown(evt) {
  if (creating_file) {
    if (evt.key == "Backspace") {
      let pos = evt.caret_pos
      if (pos > 0) {
        new_file_name = slice(new_file_name, 0, pos - 1) ++ slice(new_file_name, pos, len(new_file_name))
      }
    }
    if (evt.key == "Enter") {
      if (new_file_name != "") {
        let path = data_dir ++ new_file_name ++ ".json"
        let empty_data = {name: new_file_name, lists: [{name: "Tasks", items: []}]}
        let _^write_err = output(empty_data, path, 'json')
        active_file = new_file_name
        creating_file = false
        new_file_name = ""
      }
    }
    if (evt.key == "Escape") {
      creating_file = false
      new_file_name = ""
    }
  }
}
on select_file(evt) {
  active_file = evt.name
}
on delete_file(evt) {
  let del_path = data_dir ++ evt.name ++ ".json"
  let _^del_err = io_delete(del_path)
  if (active_file == evt.name) {
    active_file = ""
  }
}

// ============================================================================
// Entry point — render the app
// ============================================================================

apply(<todo_app>, {mode: "edit"})
