// Reactive Todo App - Lambda Script
// Run: ./lambda.exe view test/lambda/ui/todo.ls

let data^err = input('./test/lambda/ui/todos.json', 'json')

// Helper: render a single todo item
fn todo_item(item) {
    let done_class = if (item.done) "todo-item done" else "todo-item"
    let check_mark = if (item.done) "✓" else " "
    <li class:done_class
        <span class:"checkbox"; check_mark>
        <span class:"todo-text"; item.text>
        <button class:"delete-btn"; "✕">
    >
}

// Helper: render a collapsible list section
fn todo_list(lst) {
    let arrow = if (lst.open) "▼" else "▶"
    let item_count = len(lst.items)
    let done_count = len(for (item in lst.items where item.done) item)
    let count_text = string(done_count) ++ "/" ++ string(item_count)
    <div class:"todo-list"
        <div class:"list-header"
            <span class:"toggle-arrow"; arrow>
            <span class:"list-name"; lst.name>
            <span class:"list-count"; count_text>
        >
        if (lst.open) {
            <ul class:"items"
                for (item in lst.items) todo_item(item)
            >
        }
    >
}

// Build the full HTML page
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
        .header h1 {
            font-size: 1.8em;
            font-weight: 700;
            margin-bottom: 5px;
        }
        .header p {
            font-size: 0.9em;
            opacity: 0.85;
        }
        .content {
            padding: 20px 30px 30px;
        }
        .add-form {
            display: flex;
            gap: 10px;
            margin-bottom: 20px;
            padding: 15px;
            background: #f8f9fa;
            border-radius: 10px;
        }
        .add-form input {
            flex: 1;
            padding: 10px 14px;
            border: 2px solid #e0e0e0;
            border-radius: 8px;
            font-size: 0.95em;
            outline: none;
        }
        .add-form input:focus {
            border-color: #7e57c2;
        }
        .add-form button {
            padding: 10px 20px;
            background: #7e57c2;
            color: white;
            border: none;
            border-radius: 8px;
            font-size: 0.95em;
            cursor: pointer;
            font-weight: 600;
        }
        .add-form button:hover {
            background: #5c6bc0;
        }
        .todo-list {
            margin-bottom: 15px;
        }
        .list-header {
            display: flex;
            align-items: center;
            gap: 10px;
            padding: 12px 15px;
            background: #f1f3f5;
            border-radius: 10px;
            cursor: pointer;
            user-select: none;
        }
        .list-header:hover {
            background: #e9ecef;
        }
        .toggle-arrow {
            font-size: 0.8em;
            color: #666;
            width: 16px;
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
        }
        .todo-item:last-child {
            border-bottom: none;
        }
        .checkbox {
            width: 24px;
            height: 24px;
            border: 2px solid #ccc;
            border-radius: 6px;
            display: flex;
            align-items: center;
            justify-content: center;
            cursor: pointer;
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
            width: 28px;
            height: 28px;
            border: none;
            background: transparent;
            color: #ccc;
            font-size: 1.1em;
            cursor: pointer;
            border-radius: 6px;
            display: flex;
            align-items: center;
            justify-content: center;
        }
        .delete-btn:hover {
            background: #fee;
            color: #e53935;
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
            <p ("Manage your tasks with Lambda Script")>
        >
        <div class:"content"
            <div class:"add-form"
                <input type:"text", placeholder:"Add a new todo...", id:"new-todo">
                <button class:"add-btn"; "Add">
            >
            for (lst in data.lists) todo_list(lst)
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
