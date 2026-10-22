// The mail demo's layout (vibe/Lambda_Demo_Mail.md §4.2–§4.4): the sidebar of
// accounts and mailboxes, the message list, the viewer, the account sheet and
// the attachment preview. The list and the viewer are templates of their own,
// so scrolling the list re-runs only the list, and the message frame keeps its
// document while the rest of the window changes.
import dom
import san: lambda.mail.sanitize
import prov: lambda.mail.providers
import model: .mod_model

fn button(id, cls, action, value, label, extra) => <button id:id, type:"button", class:cls, 'data-action':action,
    'data-value':value or "", *:extra, label>
fn current_if(flag) => if (flag) {'aria-current':"true"} else {}

// ---- sidebar ----

fn box_row(st, a, b) => <button type:"button", class:"box depth-" ++ string(min(b.depth, 3)) ++ (if (b.role != null) " role-" ++ string(b.role) else ""),
    'data-action':"mailbox", 'data-value':b.path, 'data-account':a.id,
    *:current_if(a.id == st.account and b.path == st.box),
    <span class:"box-name", b.name>;
    if (b.unread != null and b.unread > 0) <span class:"count", string(b.unread)> else null>

fn account_section(st, a) {
    let status = model.status_of(st, a.id)
    let rows = [for (b in model.sidebar(st, a.id)) box_row(st, a, b)];
    <section class:"account", 'data-account':a.id,
        <h2 class:"account-name", a.name>
        <div class:"account-status " ++ status.state, model.status_label(status)>
        <div class:"boxes", *rows>>
}

fn sidebar(st) => <aside class:"sidebar",
    <div class:"toolbar",
        button("get-mail", "tool", "get-mail", null, "Get Mail", {title:"Check the shown mailbox now"});
        button("add-account", "tool", "add-account", null, "Add Account", {})>
    <div class:"accounts", *[for (a in st.accounts) account_section(st, a)]>;
    if (st.note != null) <p id:"note", class:"note", st.note> else null>

// ---- the message list ----

fn row(e, selected, now) => <div class:"row" ++ (if (model.is_unread(e)) " unread" else "") ++ (if (e.uid == selected) " selected" else ""),
    role:"option", 'data-action':"message", 'data-value':string(e.uid), *:current_if(e.uid == selected),
    <span class:"dot", 'aria-hidden':"true">
    <span class:"row-main",
        <span class:"row-top", <span class:"sender", model.sender_label(e)>; <span class:"date", model.date_label(e, now)>>
        <span class:"subject", model.subject_label(e.subject)>
        <span class:"meta", (if (model.is_flagged(e)) "⚑ " else "") ++ model.size_label(e.size)>>>

// Only the rows near the viewport are mounted; spacers stand for the rest,
// so a mailbox of any size scrolls through the same few dozen rows (MG8).
view <mail_list> state top: 0.0, height: 900.0 {
    let entries = ~.entries
    let w = model.window(len(entries), top, height);
    <div id:"list", class:"list", role:"listbox", 'aria-label':"Messages", 'data-rows':string(len(entries)),
        'data-first':string(w.first),
        <div class:"spacer", style:"height:" ++ string(int(w.before)) ++ "px">;
        for (e in model.rows_in(entries, w)) row(e, ~.selected, ~.now);
        <div class:"spacer", style:"height:" ++ string(int(w.after)) ++ "px">>
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

// ---- the viewer ----

fn people(label, who) => if (len(who) == 0) null
    else <div class:"header-line", <span class:"header-label", label>; <span class:"header-value", model.addresses_label(who)>>

fn text_body(text) => <div id:"text-body", class:"text-body",
    *[for (r in model.quote_runs(text)) <div class:"quote depth-" ++ string(min(r.depth, 3)), r.text>]>

fn attachment(i, a) => <button type:"button", class:"attachment", 'data-action':"preview", 'data-value':string(i),
    title:a.type, <span class:"attachment-name", a.filename or "attachment">; <span class:"attachment-size", model.size_label(a.size)>>

fn attachments(files) => if (len(files) == 0) null
    else <div id:"attachments", class:"attachments", 'data-count':string(len(files)),
        *[for (i, a in files) attachment(i, a)]>

// The open message. HTML loads in a sandboxed frame, which Radiant runs with
// scripting disabled (ML5v2, MQ11), after the sanitizer's second pass.
view <mail_viewer> {
    let m = ~.shown;
    if (m == null) <section id:"viewer", class:"viewer empty", 'data-uid':"",
        <p class:"placeholder", if (~.fetching != null) "Downloading the message…" else "No message selected">>
    else (let cleaned = if (m.html == null) null else san.clean(m.html, {remote: ~.remote, cids: san.cid_urls(m.cids)}),
        <section id:"viewer", class:"viewer", 'data-uid':string(m.uid), 'data-remote':string(~.remote),
            'data-blocked':string(if (cleaned == null) 0 else cleaned.blocked),
            <header class:"message-header",
                <h1 id:"message-subject", class:"message-subject", m.subject>;
                (people("From", m.from)); (people("To", m.to)); (people("Cc", m.cc));
                if (m.date == null) null
                else <div class:"header-line", <span class:"header-label", "Date">; <span class:"header-value", m.date>>>;
            if (cleaned != null and cleaned.blocked > 0 and not ~.remote)
                <div id:"remote-bar", class:"remote-bar", <span "This message contains remote content.">;
                    button("load-remote", "text-button", "remote", null, "Load Remote Content", {})>
            else null;
            if (cleaned != null) <iframe id:"message-frame", class:"message-frame", sandbox:"", title:m.subject, srcdoc:cleaned.html>
            else text_body(m.text or "");
            (attachments(m.attachments))>)
}

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

fn setup_sheet(form) {
    let preset = model.setup_preset(form)
    let manual = form.provider == "manual" or (form.provider == "auto" and preset == null and trim(form.email) != "");
    <div id:"setup", class:"sheet-backdrop",
        <div class:"sheet", role:"dialog", 'aria-label':"Add a mail account",
            <h2 "Add a mail account">;
            (text_field("Name", "setup-name", "name", form.name, "text"));
            (text_field("Email address", "setup-email", "email", form.email, "email"));
            (text_field("Password", "setup-password", "password", form.password, "password"));
            <label class:"field", <span "Provider">; (choice("setup-provider", "provider", form.provider, PROVIDER_CHOICES))>;
            <label class:"field", <span "Protocol">; (choice("setup-protocol", "protocol", form.protocol, PROTOCOL_CHOICES))>;
            if (form.looking) <p class:"hint", "Looking up the settings…">
            else if (preset != null) <p id:"setup-preset", class:"hint", preset.name ++ (if (preset.note != null) ". " ++ preset.note else "")>
            else null;
            if (manual) manual_fields(form) else null;
            if (form.note != null) <p id:"setup-error", class:"error", form.note> else null;
            <div class:"sheet-buttons",
                button("setup-cancel", "text-button", "setup-cancel", null, "Cancel", {});
                button("setup-save", "primary", "setup-save", null, "Add Account", {})>>>
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

pub fn page(st, shown) => <main id:"mail", 'data-loaded':string(st.loaded), 'data-account':st.account or "",
    'data-box':st.box or "", 'data-rows':string(len(st.entries)),
    'data-status':(if (st.account == null) "none" else model.status_of(st, st.account).state),
    (sidebar(st));
    apply(<mail_list entries:st.entries, selected:st.selected, now:st.now>);
    apply(<mail_viewer shown:shown, remote:shown != null and shown.file in st.remote, fetching:st.fetching>);
    if (st.setup != null) setup_sheet(st.setup) else null;
    if (st.preview != null) preview(st.preview) else null>
