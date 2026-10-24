// The mail demo's layout (vibe/Lambda_Demo_Mail.md §4.2–§4.6), in the manner
// of macOS Mail: the sidebar's mailbox tree per account, the message list
// with a preview line per row, the viewer with its toolbar, the compose
// window, and the sheets (account, new mailbox, attachment preview). The list
// and the viewer are templates of their own, so scrolling the list re-runs
// only the list, and the message frame keeps its document while the rest of
// the window changes.
import dom
import san: lambda.mail.sanitize
import prov: lambda.mail.providers
import icons: ~~.mod_icons
import model: .mod_model

fn button(id, cls, action, value, label, extra) => <button id:id, type:"button", class:cls, 'data-action':action,
    'data-value':value or "", *:extra, label>
// a toolbar button: an icon, its name as the tooltip and accessible label
fn tool(id, action, graphic, label, enabled) => <button id:id, type:"button", class:"tool-button", 'data-action':action,
    'data-value':"", title:label, 'aria-label':label, *:(if (enabled) {} else {disabled:""}), icons.icon(graphic, 18)>
fn current_if(flag) => if (flag) {'aria-current':"true"} else {}

// ---- sidebar ----

let ROLE_ICONS = {inbox: "inbox", drafts: "document", sent: "send", junk: "junk", trash: "trash", archive: "archive",
    all: "mail", flagged: "flag"}

// A mailbox row. A folder with children has a disclosure toggle; a parent
// that cannot be selected is a label row.
fn box_row(st, a, b) => <div class:"box depth-" ++ string(min(b.depth, 3)) ++ (if (b.role != null) " role-" ++ string(b.role) else "") ++
        (if (b.selectable) "" else " group"),
    role:"treeitem", *:(if (b.selectable) {'data-action':"mailbox"} else {}), 'data-value':b.path, 'data-account':a.id,
    *:current_if(a.id == st.account and b.path == st.box), *:(if (b.parent) {'aria-expanded':string(not b.collapsed)} else {}),
    if (b.parent) <span class:"disclosure", 'data-action':"toggle", 'data-value':model.count_key(a.id, b.path), title:if (b.collapsed) "Expand" else "Collapse",
        icons.icon(if (b.collapsed) "next" else "down", 10)>
    else <span class:"disclosure-space">;
    <span class:"box-icon", icons.icon(if (b.role != null) ROLE_ICONS[b.role] or "folder" else "folder", 16)>
    <span class:"box-name", b.name>;
    if (b.unread != null and b.unread > 0) <span class:"count", string(b.unread)> else null>

fn account_section(st, a) {
    let status = model.status_of(st, a.id)
    let collapsed = model.is_collapsed(st, a.id)
    let rows = if (collapsed) [] else [for (b in model.sidebar(st, a.id)) box_row(st, a, b)];
    <section class:"account" ++ (if (collapsed) " collapsed" else ""), 'data-account':a.id,
        <div class:"account-head",
            <span class:"account-toggle", 'data-action':"toggle", 'data-value':a.id, 'aria-expanded':string(not collapsed),
                <h2 class:"account-name", a.name>; <span class:"chevron", icons.icon(if (collapsed) "next" else "down", 9)>>
            <span class:"account-tools",
                button(null, "icon-button new-box", "new-box", a.id, icons.icon("folder_add", 15), {title:"New Mailbox", 'aria-label':"New Mailbox"});
                button(null, "remove-account", "remove-account", a.id, "Remove", {title:"Remove this account"})>>;
        if (st.confirm_remove == a.id) <div class:"confirm-remove", <span "Remove this account?">;
            button("remove-account-confirm", "text-button danger", "remove-account-confirm", a.id, "Remove", {});
            button("keep-account", "text-button", "keep-account", a.id, "Keep", {})>
        else null;
        <div class:"account-status " ++ status.state, model.status_label(status)>
        <div class:"boxes", role:"tree", *rows>>
}

fn sidebar(st) => <aside class:"sidebar",
    <div class:"sidebar-top",
        <button id:"get-mail", type:"button", class:"tool-button", 'data-action':"get-mail", title:"Get Mail", 'aria-label':"Get Mail",
            icons.icon("mail", 18)>
        <button id:"compose-new", type:"button", class:"tool-button", 'data-action':"compose", title:"New Message", 'aria-label':"New Message",
            *:(if (st.account == null) {disabled:""} else {}), icons.icon("compose", 18)>>
    <nav class:"accounts", *[for (a in st.accounts) account_section(st, a)]>;
    if (st.note != null) <p id:"note", class:"note", st.note> else null;
    <div class:"sidebar-bottom",
        <button id:"add-account", type:"button", class:"add-account", 'data-action':"add-account", icons.icon("plus", 14); <span "Add Account">>>>

// ---- the message list ----

fn row(e, selected, now, role) => <div class:"row" ++ (if (model.is_unread(e)) " unread" else "") ++ (if (e.uid == selected) " selected" else "") ++
        (if (model.is_flagged(e)) " flagged" else ""),
    role:"option", 'data-action':"message", 'data-value':string(e.uid), *:current_if(e.uid == selected),
    <div class:"row-gutter", <span class:"dot", 'aria-hidden':"true">;
        if (model.is_flagged(e)) <span class:"row-flag", title:"Flagged", icons.icon("flag", 12)> else null>
    <div class:"row-main",
        <div class:"row-top", <span class:"sender", model.who_label(e, role)>;
            if ((e.attachments or 0) > 0) <span class:"clip", title:"Attachments", icons.icon("paperclip", 12)> else null;
            <span class:"date", model.date_label(e, now)>>
        <div class:"subject", model.subject_label(e.subject)>
        <div class:"snippet", e.snippet or "">>>

// Only the rows near the viewport are mounted; spacers stand for the rest,
// so a mailbox of any size scrolls through the same few dozen rows (MG8).
view <mail_list> state top: 0.0, height: 900.0 {
    let entries = ~.entries
    let w = model.window(len(entries), top, height);
    <div id:"list", class:"list", role:"listbox", 'aria-label':"Messages", 'data-rows':string(len(entries)),
        'data-first':string(w.first),
        <div class:"spacer", style:"height:" ++ string(int(w.before)) ++ "px">;
        for (e in model.rows_in(entries, w)) row(e, ~.selected, ~.now, ~.role);
        <div class:"spacer", style:"height:" ++ string(int(w.after)) ++ "px">;
        if (len(entries) == 0) <div class:"list-empty", "No Messages"> else null>
}
on scroll(evt) {
    let y = float(dom.scroll_state(evt.target).y)
    let h = float(dom.scroll_metrics(evt.target).client_height)
    // re-render only when the mounted rows change
    if (model.window(len(~.entries), y, h) != model.window(len(~.entries), top, height)) {
        top = y
        height = h
    }
}

fn list_pane(st) {
    let c = st.counts[model.count_key(st.account or "", st.box or "")]
    let total = len(st.entries)
    let unread = if (c == null) 0 else c.unread;
    <section class:"list-pane",
        <header class:"list-head",
            <h1 id:"box-title", class:"box-title", model.box_title(st)>
            <div class:"box-meta", string(total) ++ (if (total == 1) " message" else " messages") ++
                (if (unread > 0) ", " ++ string(unread) ++ " unread" else "")>>
        apply(<mail_list entries:st.entries, selected:st.selected, now:st.now, role:model.box_role(st)>)>
}

// ---- the viewer ----

fn people(label, who) => if (len(who) == 0) null
    else <div class:"header-line", <span class:"header-label", label>; <span class:"header-value", model.addresses_label(who)>>

fn text_body(text) => <div id:"text-body", class:"text-body",
    *[for (r in model.quote_runs(text)) <div class:"quote depth-" ++ string(min(r.depth, 3)), r.text>]>

fn file_icon(file_type) => if (starts_with(file_type or "", "image/")) "album" else if (file_type == "application/pdf") "document"
    else if (starts_with(file_type or "", "text/")) "document" else "paperclip"

fn attachment(i, a) => <button type:"button", class:"attachment", 'data-action':"preview", 'data-value':string(i), title:a.type,
    <span class:"attachment-icon", icons.icon(file_icon(a.type), 22)>
    <span class:"attachment-text", <span class:"attachment-name", a.filename or "attachment">; <span class:"attachment-size", model.size_label(a.size)>>>

fn attachments(files) => if (len(files) == 0) null
    else <div id:"attachments", class:"attachments", 'data-count':string(len(files)),
        <div class:"attachments-label", icons.icon("paperclip", 13); <span (string(len(files)) ++ (if (len(files) == 1) " attachment" else " attachments"))>>
        <div class:"attachment-list", *[for (i, a in files) attachment(i, a)]>>

// The viewer's toolbar: replies, then the actions on the open message,
// which an account keeping flags and moves locally offers (organizes_locally).
fn viewer_toolbar(m, local, role) {
    let has_message = m != null
    let flagged = has_message and "\\Flagged" in (m.flags or [])
    let unread = has_message and not ("\\Seen" in (m.flags or []));
    <div class:"viewer-toolbar",
        <div class:"tool-group", tool("reply", "reply", "reply", "Reply", has_message); tool("reply-all", "reply-all", "reply_all", "Reply All", has_message);
            tool("forward", "forward", "forward", "Forward", has_message)>;
        if (local) <div class:"tool-group",
            tool("archive", "archive", "archive", "Archive", has_message and role != 'archive');
            tool("junk", "junk", "junk", "Move to Junk", has_message and role != 'junk');
            tool("delete", "delete", "trash", if (role == 'trash') "Delete Permanently" else "Delete", has_message)> else null;
        if (local) <div class:"tool-group",
            <button id:"flag", type:"button", class:"tool-button" ++ (if (flagged) " on" else ""), 'data-action':"flag", title:if (flagged) "Unflag" else "Flag",
                'aria-label':if (flagged) "Unflag" else "Flag", 'aria-pressed':string(flagged), *:(if (has_message) {} else {disabled:""}), icons.icon("flag", 18)>
            tool("mark-unread", "mark-unread", "unread", if (unread) "Mark as Read" else "Mark as Unread", has_message)> else null>
}

fn message_header(m) {
    let sender = if (len(m.from) == 0) {name: "(no sender)", address: ""} else m.from[0];
    <header class:"message-header",
        <div class:"avatar", style:"background:hsl(" ++ string(model.avatar_hue(sender)) ++ ",52%,56%)", 'aria-hidden':"true", model.initials(sender)>
        <div class:"header-main",
            <div class:"header-top", <span class:"from-name", sender.name or sender.address>;
                <span class:"from-address", if (sender.name != null and sender.name != "") sender.address else "">;
                <span class:"header-date", model.long_date(m.date)>>
            <h1 id:"message-subject", class:"message-subject", m.subject>;
            (people("To:", m.to)); (people("Cc:", m.cc))>>
}

// The open message. HTML loads in a sandboxed frame, which Radiant runs with
// scripting disabled (ML5v2, MQ11), after the sanitizer's second pass.
view <mail_viewer> {
    let m = ~.shown;
    if (m == null) <section id:"viewer", class:"viewer empty", 'data-uid':"",
        (viewer_toolbar(null, ~.local, ~.role));
        <div class:"placeholder", <span class:"placeholder-icon", icons.icon("mail", 44)>;
            <p if (~.fetching != null) "Downloading the message…" else "No Message Selected">>>
    else (let cleaned = if (m.html == null) null else san.clean(m.html, {remote: ~.remote, cids: san.cid_urls(m.cids)}),
        <section id:"viewer", class:"viewer", 'data-uid':string(m.uid), 'data-remote':string(~.remote),
            'data-blocked':string(if (cleaned == null) 0 else cleaned.blocked),
            (viewer_toolbar(m, ~.local, ~.role));
            (message_header(m));
            if (~.role == 'drafts') <div id:"draft-bar", class:"info-bar", <span "This message is a draft.">;
                button("edit-draft", "text-button", "edit-draft", null, "Edit Draft", {})> else null;
            if (cleaned != null and cleaned.blocked > 0 and not ~.remote)
                <div id:"remote-bar", class:"info-bar", <span "This message contains remote content.">;
                    button("load-remote", "text-button", "remote", null, "Load Remote Content", {})>
            else null;
            if (cleaned != null) <iframe id:"message-frame", class:"message-frame", sandbox:"", title:m.subject, srcdoc:cleaned.html>
            else text_body(m.text or "");
            (attachments(m.attachments))>)
}

// ---- the compose window ----

fn compose_field(label, id, key, value, kind) => <label class:"compose-field", <span class:"compose-label", label>;
    <input id:id, type:kind, 'data-compose':key, value:value, autocomplete:"off", spellcheck:"false">>

fn compose_window(c, from) => <div id:"compose", class:"compose-backdrop", 'data-mode':c.mode,
    <div class:"compose-window", role:"dialog", 'aria-label':model.compose_title(c),
        <div class:"compose-titlebar", <span class:"compose-title", model.compose_title(c)>>
        <div class:"compose-toolbar",
            <button id:"compose-send", type:"button", class:"send-button", 'data-action':"compose-send", title:"Send",
                *:(if (c.sending) {disabled:""} else {}), icons.icon("send", 16); <span if (c.sending) "Sending…" else "Send">>
            <span class:"toolbar-spacer">
            button("compose-save", "text-button", "compose-save", null, "Save Draft", {});
            button("compose-discard", "text-button danger", "compose-discard", null, "Discard", {})>;
        (compose_field("To:", "compose-to", "to", c.to, "text"));
        (compose_field("Cc:", "compose-cc", "cc", c.cc, "text"));
        (compose_field("Subject:", "compose-subject", "subject", c.subject, "text"));
        <div class:"compose-field from", <span class:"compose-label", "From:">; <span class:"compose-from", from>>
        if (len(c.attachments or []) > 0) <div class:"compose-attachments",
            *[for (a in c.attachments) <span class:"chip", icons.icon("paperclip", 12); <span (a.filename or "attachment")>>]> else null;
        <textarea id:"compose-body", class:"compose-body", 'data-compose':"text", value:c.text, spellcheck:"true">;
        if (c.note != null) <p id:"compose-error", class:"compose-note", c.note> else null>>

// ---- the new-mailbox sheet ----

fn new_box_sheet(st, nb) => <div id:"new-box", class:"sheet-backdrop",
    <div class:"sheet narrow", role:"dialog", 'aria-label':"New Mailbox",
        <h2 "New Mailbox">;
        <label class:"field", <span "Location">;
            <select id:"new-box-parent", 'data-newbox':"parent",
                *[for (c in model.mailbox_parents(st, nb.account)) <option value:c[0], *:(if (c[0] == nb.parent) {selected:""} else {}), c[1]>]>>
        <label class:"field", <span "Name">; <input id:"new-box-name", type:"text", 'data-newbox':"name", value:nb.name, autocomplete:"off">>
        if (nb.note != null) <p id:"new-box-error", class:"error", nb.note> else null;
        <div class:"sheet-buttons",
            button("new-box-cancel", "text-button", "new-box-cancel", null, "Cancel", {});
            button("new-box-save", "primary", "new-box-save", null, "Create", {})>>>

// ---- the account sheet ----

let PROVIDER_CHOICES = [["auto", "Automatic"], *[for (p in prov.PRESETS) [p.id, p.name]], ["manual", "Other (manual)"]]
let SECURITY_CHOICES = [["tls", "TLS"], ["starttls", "STARTTLS"], ["plain", "None (this computer only)"]]
let PROTOCOL_CHOICES = [["imap", "IMAP"], ["pop3", "POP3 (download to this computer)"]]

fn choice(id, field, value, choices) => <select id:id, 'data-field':field,
    *[for (c in choices) <option value:c[0], *:(if (c[0] == value) {selected:""} else {}), c[1]>]>
fn text_field(label, id, key, value, kind) => <label class:"field", <span label>;
    <input id:id, type:kind, 'data-field':key, value:value, autocomplete:"off">>

fn manual_fields(form) => <div class:"manual",
    (text_field("Incoming server", "setup-host", "host", form.host, "text"));
    (text_field("Port", "setup-port", "port", form.port, "text"));
    <label class:"field", <span "Security">; (choice("setup-security", "security", form.security, SECURITY_CHOICES))>;
    (text_field("User name", "setup-user", "user", form.user, "text"));
    (text_field("Certificate pin", "setup-pin", "pin", form.pin, "text"))>

// Gmail in one press, where the app has a Google client; until it has one,
// the press leads to google_setup_panel
fn gmail_login(has_client) => <div class:"login-gmail",
    button("login-gmail", "google-button", "login-gmail", null, "Login with Gmail", {});
    if (has_client) button("google-change", "text-button google-change", "google-change", null, "Change Google client", {}) else null;
    <p class:"divider", "or add an account by its settings">>

// The one-time registration Google asks of every app that signs in to Gmail:
// the person's own client (MQ4), pasted here and kept in oauth.mark.
fn google_setup_panel(form) => <div id:"google-setup", class:"sheet", role:"dialog", 'aria-label':"Set up Google sign-in",
    <h2 "Set up Google sign-in">;
    <p class:"hint", "Google signs people in to Gmail only through an app it knows, so register this demo as your own app once:">;
    <ol class:"steps",
        <li "In Google Cloud, create a project and set up its Google Auth Platform as an External app in Testing.">
        <li "Add your Gmail address as a test user.">
        <li "Under Clients, create a client of type Desktop app, then paste its ID and secret here.">>;
    button("google-console", "text-button", "google-console", null, "Open Google Cloud", {});
    (text_field("Client ID", "google-client-id", "client_id", form.client_id, "text"));
    // shown in the clear: a desktop client's secret is not confidential, and a stray pasted character must be visible
    (text_field("Client secret", "google-client-secret", "client_secret", form.client_secret, "text"));
    if (form.note != null) <p id:"setup-error", class:"error", form.note> else null;
    <div class:"sheet-buttons",
        button("google-setup-back", "text-button", "google-setup-back", null, "Back", {});
        button("google-setup-save", "primary", "google-setup-save", null, "Save", {})>>

fn setup_sheet(form, clients) {
    let preset = model.setup_preset(form)
    let manual = form.provider == "manual" or (form.provider == "auto" and preset == null and trim(form.email) != "")
    let endpoints = model.setup_oauth(form, clients)
    let auth = model.setup_auth(form, clients)
    // a local bridge (Proton Mail Bridge) presents a self-signed certificate to pin
    let bridge = preset != null and preset.imap != null and preset.imap.host == "127.0.0.1";
    <div id:"setup", class:"sheet-backdrop", 'data-auth':auth, 'data-signing':string(form.signing),
        'data-google':string(model.oauth_client(clients, prov.GOOGLE) != null),
        if (form.google_setup) google_setup_panel(form)
        else <div class:"sheet", role:"dialog", 'aria-label':"Add a mail account",
            <h2 "Add a mail account">;
            (gmail_login(model.oauth_client(clients, prov.GOOGLE) != null));
            (text_field("Name", "setup-name", "name", form.name, "text"));
            (text_field("Email address", "setup-email", "email", form.email, "email"));
            <label class:"field", <span "Provider">; (choice("setup-provider", "provider", form.provider, PROVIDER_CHOICES))>;
            if (endpoints == null) null
            else <label class:"field", <span "Sign in">; (choice("setup-auth", "auth", auth,
                [["oauth", "With " ++ endpoints.name], ["password", "With a password or app password"]]))>;
            if (auth == "password") text_field("Password", "setup-password", "password", form.password, "password") else null;
            <label class:"field", <span "Protocol">; (choice("setup-protocol", "protocol", form.protocol, PROTOCOL_CHOICES))>;
            if (form.looking) <p class:"hint", "Looking up the settings…">
            else if (preset != null) <p id:"setup-preset", class:"hint", preset.name ++ (if (preset.note != null) ". " ++ preset.note else "")>
            else null;
            if (manual) manual_fields(form)
            else if (bridge) text_field("Certificate pin", "setup-pin", "pin", form.pin, "text")
            else null;
            if (form.signing) <p id:"setup-signing", class:"hint", ("Finish signing in to " ++ form.signing_with ++ " in your browser…")> else null;
            if (form.notice != null) <p id:"setup-notice", class:"hint", form.notice> else null;
            if (form.note != null) <p id:"setup-error", class:"error", form.note> else null;
            <div class:"sheet-buttons",
                // made-up mail on this computer, for showing the window without an account
                button("add-demo", "text-button demo-button", "add-demo", null, "Try the demo mailbox", {title:"Add a demo account with sample mail"});
                button("setup-cancel", "text-button", "setup-cancel", null, "Cancel", {});
                button("setup-save", "primary", "setup-save", null,
                    if (auth == "oauth") "Sign in with " ++ endpoints.name else "Add Account", {})>>>
}

// ---- the attachment preview (Quick Look) ----

fn preview(pv) => <div id:"preview", class:"sheet-backdrop",
    <div class:"preview", role:"dialog", 'aria-label':pv.name,
        <div class:"preview-bar", <span class:"preview-name", pv.name>;
            button("preview-save", "text-button", "save-attachment", null, "Save to Downloads", {});
            button("preview-close", "text-button", "preview-close", null, "Close", {})>;
        if (pv.saved != null) <p id:"preview-saved", class:"hint", ("Saved to " ++ pv.saved)> else null;
        if (pv.image != null) <img class:"preview-image", alt:pv.name, src:pv.image>
        else if (pv.text != null) <pre class:"preview-text", pv.text>
        else if (pv.file != null) <iframe class:"preview-frame", sandbox:"", src:pv.file, title:pv.name>
        else <p class:"placeholder", ("No preview for " ++ pv.type ++ ".")>>>

pub fn page(st, shown) {
    let a = model.current(st);
    <main id:"mail", 'data-loaded':string(st.loaded), 'data-account':st.account or "",
        'data-box':st.box or "", 'data-rows':string(len(st.entries)),
        'data-status':(if (st.account == null) "none" else model.status_of(st, st.account).state),
        (sidebar(st));
        (list_pane(st));
        apply(<mail_viewer shown:shown, remote:shown != null and shown.file in st.remote, fetching:st.fetching,
            local:model.organizes_locally(a), role:model.box_role(st)>);
        if (st.compose != null) compose_window(st.compose, if (a == null) "" else model.address_label(model.sender_of(a))) else null;
        if (st.new_box != null) new_box_sheet(st, st.new_box) else null;
        if (st.setup != null) setup_sheet(st.setup, st.clients) else null;
        if (st.preview != null) preview(st.preview) else null>
}
