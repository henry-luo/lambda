// The mail demo's pure model (test/demo/mail/mod_model.ls): ids, the sidebar,
// the list window, labels, sync reports, quoting and the account sheet.
import model: ~~.mod_model

"=== ids ==="
model.account_id("Ada.Lovelace@Example.org", [])
model.account_id("ada@example.org", ["ada-example-org"])
model.box_id("INBOX")
model.box_id("[Gmail]/Sent Mail")
model.box_id("Entwürfe_2")

"=== sidebar ==="
let boxes = [
    {path: "Work/2026", name: "Work/2026", delimiter: "/", role: null, selectable: true},
    {path: "[Gmail]", name: "[Gmail]", delimiter: "/", role: null, selectable: false},
    {path: "[Gmail]/Sent Mail", name: "[Gmail]/Sent Mail", delimiter: "/", role: 'sent', selectable: true},
    {path: "Archive", name: "Archive", delimiter: "/", role: null, selectable: true},
    {path: "INBOX", name: "INBOX", delimiter: "/", role: 'inbox', selectable: true}]
let st = {*: model.initial(), account: "ada", mailboxes: {ada: boxes}, box: "INBOX",
    counts: {'ada/INBOX': {unread: 2, total: 5}}};
model.sidebar(st, "ada")
model.inbox_path(boxes)
model.inbox_path([])

"=== window ==="
model.window(50000, 0.0, 640.0)
model.window(50000, 64000.0, 640.0)
model.window(50000, 3200000.0, 640.0)
model.window(3, 0.0, 640.0)
let entries = [for (u in 1 to 5) {uid: u, flags: if (u % 2 == 0) ["\\Seen"] else []}];
[for (e in model.rows_in(entries, model.window(5, 0.0, 128.0))) e.uid]
model.counts_of(entries)

"=== labels ==="
let now = {year: 2026, month: 10, day: 22}
let e = {date: "Thu, 22 Oct 2026 09:14:00 +0000", internaldate: null};
model.date_label(e, now)
model.date_label({date: "Mon, 5 Oct 2026 18:00:00 +0200"}, now)
model.date_label({date: "1 Jan 25 00:00 GMT"}, now)
model.date_label({date: null, internaldate: "21-Oct-2026 09:14:00 +0200"}, now)
model.date_label({date: "garbage"}, now);
[model.size_label(900), model.size_label(48210), model.size_label(25000000)];
[model.sender_label({from: {name: "Ada", address: "ada@x"}}), model.sender_label({from: {name: null, address: "bob@x"}}),
 model.sender_label({from: null})]
model.subject_label("  ")

"=== reports ==="
let s1 = model.apply_report(st, {type: "status", account: "ada", state: "syncing", text: null})
let s2 = model.apply_report(s1, {type: "index", account: "ada", path: "INBOX", entries: entries})
let s3 = model.apply_report({*: s2, box: "Gone"}, {type: "mailboxes", account: "ada", boxes: boxes});
[model.status_label(model.status_of(s1, "ada")), model.status_label(model.status_of(s1, "bob")), len(s2.entries),
 s2.counts["ada/INBOX"], s3.box]

"=== quoting ==="
model.quote_runs("Thanks!\n> first\n>> deeper\n> > also deeper\nend")

"=== account sheet ==="
let form = {*: model.setup_initial(), email: "ada@gmail.com", password: "app pw"};
model.account_from_setup(form, [], null)
model.account_from_setup({*: form, password: ""}, [], null)
model.account_from_setup({*: form, email: "ada@nowhere.test"}, [], null)
model.account_from_setup({*: form, email: "ada@nowhere.test", provider: "manual", host: "127.0.0.1",
    port: "1143", security: "plain", user: "ada", pin: " "}, ["ada-nowhere-test"], null)
model.account_from_setup({*: form, protocol: "pop3"}, [], null)
model.account_from_setup({*: form, email: "ada@icloud.com", protocol: "pop3"}, [], null)
model.account_from_setup({*: form, email: "ada@nowhere.test", provider: "manual", protocol: "pop3", host: "127.0.0.1",
    security: "plain", user: "ada"}, [], null);
[model.is_pop3({protocol: "pop3"}), model.is_pop3({protocol: "imap"}), model.is_pop3(null)]

"=== sign-in ===";
let clients = {google: {client_id: "id.apps.googleusercontent.com", client_secret: "s"}}
let gmail = {*: model.setup_initial(), email: "ada@gmail.com"};
[model.setup_auth(gmail, clients), model.setup_auth(gmail, null), model.setup_auth({*: gmail, auth: "password"}, clients),
 model.setup_auth({*: gmail, email: "ada@fastmail.com"}, clients), model.setup_oauth(gmail, clients).provider,
 model.oauth_client(clients, model.setup_oauth(gmail, clients)), model.oauth_client(clients, null)]
model.account_from_setup(gmail, [], clients);

"=== Login with Gmail ===";
let google = {provider: "google", name: "Google"}
let others = [{id: "bob-example-com", email: "bob@example.com", name: "Bob"},
    {id: "ada-gmail-com", email: "Ada@Gmail.com", name: "Ada", provider: "gmail", protocol: "pop3"}];
[model.gmail_account("ada@gmail.com", "Ada L", others, google).id, model.gmail_account("ada@gmail.com", "Ada L", others, google).name,
 model.gmail_account("ada@gmail.com", "Ada L", others, google).protocol, model.gmail_account("ada@gmail.com", "Ada L", others, google).pop3.host,
 model.gmail_account("cy@gmail.com", "Cy", others, google).id, model.gmail_account("cy@gmail.com", "", others, google).name,
 model.gmail_account("cy@gmail.com", "Cy", others, google).imap.host, model.gmail_account("cy@gmail.com", "Cy", others, google).auth];
[for (a in model.with_account(others, {id: "bob-example-com", email: "bob@example.com", name: "Robert"})) a.name];
[for (a in model.with_account(others, {id: "new", name: "New"})) a.name];
[for (a in model.without_account(others, "bob-example-com")) a.id];
let ID = "123456789012-abcdefghij0123456789abcdefghij01.apps.googleusercontent.com"
let SECRET = "GOCSPX-abc_DEF-123";
[model.google_client_error("", "s"), model.google_client_error("x.apps.googleusercontent.com", SECRET),
 model.google_client_error("- " ++ ID, SECRET), model.google_client_error(ID, " "), model.google_client_error(ID, "- " ++ SECRET),
 model.google_client_error(" " ++ ID ++ "\n", " " ++ SECRET ++ " ")]
let fixing = model.google_setup_form(model.setup_initial(), {id: "- " ++ ID, secret: SECRET}, "bad");
[fixing.google_setup, fixing.client_id == "- " ++ ID, fixing.client_secret == SECRET, fixing.note,
 model.google_setup_form(model.setup_initial(), null, null).client_id]
