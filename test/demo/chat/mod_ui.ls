import chat: lambda.chat
import files: .mod_files

let EMOJI = [{text:"😀", label:"Grinning face"}, {text:"😊", label:"Smiling face"},
    {text:"😂", label:"Tears of joy"}, {text:"🥰", label:"Feeling loved"},
    {text:"😎", label:"Cool"}, {text:"🤔", label:"Thinking"}, {text:"🥳", label:"Celebration"},
    {text:"❤️", label:"Heart"}, {text:"👍", label:"Thumbs up"}, {text:"👎", label:"Thumbs down"},
    {text:"👋", label:"Wave"}, {text:"🙏", label:"Thanks"}, {text:"👏", label:"Applause"},
    {text:"🎉", label:"Party"}, {text:"🔥", label:"Fire"}, {text:"✨", label:"Sparkles"},
    {text:"✅", label:"Done"}, {text:"💬", label:"Chat"}, {text:"☕", label:"Coffee"}, {text:"🌍", label:"World"}]

let PATHS = {
    chat: "M4 4h16v12H9l-5 4z M8 8h8 M8 12h5",
    search: "M10.5 4a6.5 6.5 0 1 0 0 13 6.5 6.5 0 0 0 0-13 M16 16l5 5",
    send: "M3 3l19 9-19 9 4-9z M7 12h15",
    more: "M12 4v1 M12 11v1 M12 18v1",
    check: "M4 12l5 5L20 6",
    double: "M2 12l5 5L18 6 M12 15l2 2L25 6",
    lock: "M6 10h12v11H6z M8 10V7a4 4 0 0 1 8 0v3",
    people: "M16 21v-3a4 4 0 0 0-4-4H6a4 4 0 0 0-4 4v3 M9 3a4 4 0 1 0 0 8 4 4 0 0 0 0-8 M19 4a4 4 0 0 1 0 7 M22 21v-3a4 4 0 0 0-3-4",
    disconnect: "M8 3v5 M16 3v5 M5 8h14v3a7 7 0 0 1-14 0z M12 18v4",
    emoji: "M12 2a10 10 0 1 0 0 20 10 10 0 0 0 0-20 M7 14q5 6 10 0 M8 8v2 M16 8v2",
    attach: "M8 13l7-7a3 3 0 0 1 4 4L9 20a5 5 0 0 1-7-7L13 2 M6 14l9-9",
    image: "M3 3h18v18H3z M3 17l6-6 4 4 3-3 5 5 M16 7h1",
    save: "M12 3v12 M7 10l5 5 5-5 M4 16v5h16v-5",
    close: "M6 6l12 12 M18 6L6 18"
}
fn icon(key) => <svg width:"22", height:"22", viewBox:if (key == "double") "0 0 28 24" else "0 0 24 24",
    fill:"none", stroke:"currentColor", 'stroke-width':"1.7", 'stroke-linecap':"round", 'stroke-linejoin':"round",
    'aria-hidden':"true", <path d:PATHS[key]>>
fn avatar(user, size = "") => (let p = chat.profile(user),
    <span class:"avatar " ++ p.color ++ " " ++ size, p.initials>)
fn contact(st, p) => (let unread = chat.unread((st.inboxes[st.user] or []), st.user, p.id),
    <button type:"button", class:"contact" ++ (if (st.peer == p.id) " selected" else ""),
        'data-action':"contact", 'data-value':p.id, 'aria-label':"Chat with " ++ p.name,
        (avatar(p.id));
        <span class:"contact-copy",
            <span class:"contact-top", <b p.name>; if (unread > 0) <span class:"unread", string(unread)>>
            <span class:"contact-preview", chat.preview((st.inboxes[st.user] or []), st.user, p.id)>>>)
fn attachment(a, pending = false) => <div class:"attachment" ++ (if (pending) " pending" else ""),
    'data-kind':a.kind, 'data-size':string(a.size), 'data-source-size':string(a.source_size), 'data-mime':a.mime,
    if (a.kind == "image") <img class:"attachment-image", src:a.data, alt:a.name, width:string(a.width), height:string(a.height)>;
    <div class:"attachment-info", <strong a.name>
        <span files.size_label(a.size) ++ (if (a.kind == "file") " · ZIP compressed" else " · " ++ string(a.width) ++ " × " ++ string(a.height))>
        if (pending) <span "Original: " ++ files.size_label(a.source_size)>>>
fn bubble(st, m) => (let mine = m.from == st.user,
    <div class:if (mine) "message-line outgoing" else "message-line incoming",
        <div class:"bubble", 'data-id':m.id, 'data-status':m.status,
            if (m.attachment != null) <div (attachment(m.attachment));
                <button class:"save-attachment", 'data-action':"save-attachment", 'data-id':m.id,
                    'data-from':m.from, 'aria-label':"Save " ++ m.attachment.name, (icon("save")); <span "Save…">>>;
            if (m.text != "") <div class:"message-text", m.text>;
            <div class:"message-meta", <span slice(m.stamp, 0, 5)>;
                if (mine) <span class:"receipt " ++ m.status, title:if (m.status == "sent") "Saved" else m.status,
                    'aria-label':m.status,
                    if (m.status == "queued") "◷" else icon(if (m.status == "sent") "check" else "double")>>>>)
fn profile_picker(st) => <div class:"profile-picker",
    <span class:"field-label", "YOUR PROFILE">
    <select id:"profile", 'aria-label':"Your profile",
        *:(if (st.shared) {disabled:""} else {}),
        for (p in chat.profiles) <option value:p.id, *:(if (st.user == p.id) {selected:""} else {}), p.name>>>

fn dialog(d) => if (d == null) null else <div class:"chat-dialog-layer",
    <div class:"chat-dialog-backdrop", 'data-action':"dialog-cancel">
    <section id:"attachment-dialog", class:"chat-dialog", role:"dialog", 'aria-modal':"true",
        'aria-label':if (d.kind == "attach") "Attach a file" else "Save attachment",
        <h2 if (d.kind == "attach") "Attach a file" else "Save attachment">
        <p if (d.kind == "attach") "Files are zipped before sending. Up to 8 MiB original / 4 MiB ZIP." else "Choose where to save " ++ d.attachment.name ++ ".">
        if (d.kind == "attach") <div class:"folder-bar", <button 'data-action':"folder-up", 'aria-label':"Enclosing folder", "↑">; <span d.folder>>;
        if (d.kind == "attach") <div id:"attachment-files", class:"attachment-files", role:"list",
            for (e in files.rows(d)) <button class:"attachment-file", 'data-action':"file-select", 'data-path':e.path,
                'data-dir':string(e.dir), role:"listitem", <span if (e.dir) "📁 " ++ e.name else e.name>;
                <span if (e.dir) "Folder" else files.size_label(e.size)>>>;
        <label 'for':"attachment-path", if (d.kind == "attach") "File or folder path" else "Destination path">
        <input id:"attachment-path", type:"text", value:d.path, autocomplete:"off", 'aria-label':"Attachment path">
        if (d.error != null) <p id:"attachment-error", role:"alert", d.error>;
        <div class:"dialog-actions", <button 'data-action':"dialog-cancel", "Cancel">
            <button id:"attachment-confirm", 'data-action':"dialog-confirm", class:"primary",
                if (d.kind == "attach") "Attach ZIP" else "Save">>>>

pub fn page(st) => (let selected = chat.profile(st.peer),
    let messages = chat.conversation((st.inboxes[st.user] or []), st.user, st.peer),
    let online = st.online[st.user] == true,
    <div id:"chat", 'data-user':st.user, 'data-peer':st.peer, 'data-ready':if (st.ready) "true" else "false",
        'data-mode':if (st.shared) "client" else "embedded", 'data-endpoint':st.endpoint or "",
        'data-online':if (online) "true" else "false", 'data-messages':string(len(messages)),
        <nav class:"rail", 'aria-label':"App navigation",
            <div class:"brand-mark", "λ">
            <button class:"rail-button active", title:"Chats", (icon("chat")); <span class:"rail-dot">>
            <div class:"rail-spacer">
            <span class:"rail-footer", (icon("lock"))>;
            (avatar(st.user, "small"))>
        <aside class:"sidebar",
            <header class:"sidebar-heading", <h1 "Chats">; <span class:"wordmark", "lambda">>;
            (profile_picker(st));
            <label class:"search", (icon("search"));
                <input id:"search", type:"search", placeholder:"Search conversations", value:st.query,
                    'aria-label':"Search conversations">>
            <div class:"filters", <span class:"filter selected", "All">; <span class:"filter", "Personal">>
            <div id:"contacts", class:"contacts",
                for (p in chat.profiles where p.id != st.user and
                    (st.query == "" or contains(lower(p.name ++ " " ++ chat.preview((st.inboxes[st.user] or []), st.user, p.id)), lower(st.query))))
                    contact(st, p)>
            <div class:"sidebar-bottom", <span class:"demo-pill", if (st.shared) "SHARED SERVER" else "LOCAL DEMO">
                <p "A conversation has two sides.">
                <span if (st.shared) "Open another window with a different profile to reply." else "Switch your profile above to reply.">>>
        <main class:"conversation",
            <header class:"conversation-heading",
                (avatar(st.peer));
                <div class:"conversation-person", <h2 selected.name>;
                    <span class:"presence" ++ (if (st.online[st.peer] == true) " is-online" else ""),
                        <span class:"presence-dot">; if (st.online[st.peer] == true) "Online" else "Offline · messages will wait">>
                <select id:"peer-select", 'aria-label':"Conversation",
                    for (p in chat.profiles where p.id != st.user) <option value:p.id, *:(if (st.peer == p.id) {selected:""} else {}), p.name>>
                <button id:"connection-toggle", class:"connection-toggle", 'data-action':"connection",
                    title:if (online) "Disconnect this profile" else "Reconnect this profile",
                    (icon("disconnect")); <span if (online) "Go offline" else "Reconnect">>>
            <div class:"privacy-note", (icon("lock")); "Local demo · not end-to-end encrypted">
            <div id:"messages", class:"messages", role:"log", 'aria-live':"polite",
                if (len(messages) == 0) <div class:"empty-conversation",
                    <div class:"empty-icon", (icon("chat"))>
                    <span class:"eyebrow", "A LITTLE HELLO GOES A LONG WAY">
                    <h3 "Say hello to " ++ split(selected.name, " ")[0] ++ ".">
                    <p "Big ideas, small updates, and everything in between.">
                    <button class:"hello-button", 'data-action':"hello", "Start the conversation →">>
                else <div class:"message-stack", <span class:"day-label", "YOUR CONVERSATION">;
                    for (m in messages) bubble(st, m)>>
            if (st.note != null) <div id:"notice", class:"notice", role:"status", st.note>;
            <footer class:"composer-area",
                if (st.emoji) <div id:"emoji-picker", class:"emoji-picker", role:"group", 'aria-label':"Emoji",
                    for (e in EMOJI) <button class:"emoji-choice", 'data-action':"emoji-insert", 'data-value':e.text,
                        title:e.label, 'aria-label':e.label, e.text>>;
                if (st.attachment != null) <div id:"pending-attachment", (attachment(st.attachment, true));
                    <button id:"remove-attachment", class:"tool-button", 'data-action':"attachment-remove",
                        'aria-label':"Remove attachment", (icon("close"))>>;
                <div class:"composer-tools",
                    <button id:"emoji-toggle", class:"tool-button", 'data-action':"emoji-toggle", 'aria-label':"Choose emoji",
                        'aria-expanded':string(st.emoji), (icon("emoji"))>
                    <button id:"attach-file", class:"tool-button", 'data-action':"attach-file", 'aria-label':"Attach a file", (icon("attach"))>
                    <button id:"paste-image", class:"tool-button", 'data-action':"paste-image", 'aria-label':"Paste clipboard image", (icon("image"))>
                    <label class:"image-compression", <input id:"compress-images", type:"checkbox",
                        *:if (st.compress_images) {checked:""} else {}>; <span "Compress pasted images">>>
                <div class:"composer",
                    <textarea id:"draft", rows:"1", placeholder:"Write a message…", value:st.draft,
                        'aria-label':"Message", maxlength:"4000">
                    <button id:"send", class:"send-button", 'data-action':"send", 'aria-label':"Send message",
                        *:(if (online) {} else {disabled:""}), (icon("send"))>>
                <div class:"composer-hint", <span <span class:if (online) "connection-dot online" else "connection-dot">;
                    if (online) "Connected" else if (st.ready) "Offline" else "Connecting…">;
                    <span "Enter to send · Shift + Enter for a new line">>>>;
        (dialog(st.dialog))
    >)
