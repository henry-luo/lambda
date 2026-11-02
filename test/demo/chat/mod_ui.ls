import chat: lambda.chat

let PATHS = {
    chat: "M4 4h16v12H9l-5 4z M8 8h8 M8 12h5",
    search: "M10.5 4a6.5 6.5 0 1 0 0 13 6.5 6.5 0 0 0 0-13 M16 16l5 5",
    send: "M3 3l19 9-19 9 4-9z M7 12h15",
    more: "M12 4v1 M12 11v1 M12 18v1",
    check: "M4 12l5 5L20 6",
    double: "M2 12l5 5L18 6 M12 15l2 2L25 6",
    lock: "M6 10h12v11H6z M8 10V7a4 4 0 0 1 8 0v3",
    people: "M16 21v-3a4 4 0 0 0-4-4H6a4 4 0 0 0-4 4v3 M9 3a4 4 0 1 0 0 8 4 4 0 0 0 0-8 M19 4a4 4 0 0 1 0 7 M22 21v-3a4 4 0 0 0-3-4",
    disconnect: "M8 3v5 M16 3v5 M5 8h14v3a7 7 0 0 1-14 0z M12 18v4"
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
fn bubble(st, m) => (let mine = m.from == st.user,
    <div class:if (mine) "message-line outgoing" else "message-line incoming",
        <div class:"bubble", 'data-id':m.id, 'data-status':m.status,
            <div class:"message-text", m.text>
            <div class:"message-meta", <span slice(m.stamp, 0, 5)>;
                if (mine) <span class:"receipt " ++ m.status, title:if (m.status == "sent") "Saved" else m.status,
                    'aria-label':m.status,
                    if (m.status == "queued") "◷" else icon(if (m.status == "sent") "check" else "double")>>>>)
fn profile_picker(st) => <div class:"profile-picker",
    <span class:"field-label", "YOUR PROFILE">
    <select id:"profile", 'aria-label':"Your profile",
        for (p in chat.profiles) <option value:p.id, *:(if (st.user == p.id) {selected:""} else {}), p.name>>>

pub fn page(st) => (let selected = chat.profile(st.peer),
    let messages = chat.conversation((st.inboxes[st.user] or []), st.user, st.peer),
    let online = st.online[st.user] == true,
    <div id:"chat", 'data-user':st.user, 'data-peer':st.peer, 'data-ready':if (st.ready) "true" else "false",
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
            <div class:"sidebar-bottom", <span class:"demo-pill", "LOCAL DEMO">
                <p "A conversation has two sides.">
                <span "Switch your profile above to reply.">>>
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
                <div class:"composer",
                    <textarea id:"draft", rows:"1", placeholder:"Write a message…", value:st.draft,
                        'aria-label':"Message", maxlength:"4000">
                    <button id:"send", class:"send-button", 'data-action':"send", 'aria-label':"Send message",
                        *:(if (online) {} else {disabled:""}), (icon("send"))>>
                <div class:"composer-hint", <span <span class:if (online) "connection-dot online" else "connection-dot">;
                    if (online) "Connected" else if (st.ready) "Offline" else "Connecting…">;
                    <span "Enter to send · Shift + Enter for a new line">>>>
    >)
