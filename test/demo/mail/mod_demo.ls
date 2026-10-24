// The mail demo's demo account: a mailbox store on this computer, seeded
// once with a few hundred made-up messages, so the window can be shown
// without a real account. Its task (mod_sync.ls serve_demo) never connects:
// moves, flags, new mailboxes, drafts and sent mail all stay in this store,
// and Get Mail delivers one more message from a small pool. Every name,
// address and company here is invented (.example and lambda.demo domains).
// Messages are written by lambda.doc.misc's eml writer, most of them as
// multipart/alternative with an HTML part.
import store: lambda.mail.store
import sync: lambda.mail.sync
import compose: lambda.mail.compose
import misc: lambda.doc.misc

pub let ID = "demo"
let DOMAIN = "lambda.demo"
let MINUTE = 60000
let HOUR = 60
let DAY = 1440

// the account record, as account_from_setup makes one; protocol "demo" has no server
pub fn account() => {id: ID, name: "Demo Mailbox", full_name: "Alex Rivera", email: "alex@" ++ DOMAIN, user: "alex", provider: "demo",
    protocol: "demo", imap: null, pop3: null, smtp: null, pin: null, auth: "none", oauth: null}

// ---- people ----

pub let ME = {name: "Alex Rivera", address: "alex@" ++ DOMAIN}
let P = {
    maya: {name: "Maya Chen", address: "maya.chen@northwind.example"},
    jose: {name: "José Álvarez", address: "jose@alvarez.example"},
    priya: {name: "Priya Natarajan", address: "priya@fabrikam.example"},
    sam: {name: "Sam O'Connell", address: "sam@northwind.example"},
    lena: {name: "Lena Hoffmann", address: "lena.hoffmann@contoso.example"},
    kenji: {name: "Kenji Watanabe", address: "kenji@tailspin.example"},
    noor: {name: "Noor Haddad", address: "noor@northwind.example"},
    tom: {name: "Tom Becker", address: "tom.becker@contoso.example"},
    mom: {name: "Mom", address: "linda.rivera@familymail.example"},
    ci: {name: "Forge CI", address: "builds@forge.example"},
    forge: {name: "Forge", address: "notifications@forge.example"},
    cal: {name: "Northwind Calendar", address: "calendar@northwind.example"},
    billing: {name: "Northwind Cloud Billing", address: "billing@northwind-cloud.example"},
    weekly: {name: "Lambda Weekly", address: "hello@weekly.lambda.demo"},
    ship: {name: "Parcelpost", address: "tracking@parcelpost.example"},
    secure: {name: "Account Security", address: "no-reply@accounts.example"},
    list: {name: "lambda-dev", address: "lambda-dev@lists." ++ DOMAIN},
    spam: {name: "Prize Center", address: "winner@prizes-4-u.example"}
}
let TEAM = [P.maya, P.jose, P.priya, P.sam, P.lena, P.kenji, P.noor, P.tom]

// ---- attachments: small generated files ----

let CHART_PNG = b'\64iVBORw0KGgoAAAANSUhEUgAAARgAAACgCAMAAADgvoMSAAAAD1BMVEX///84vfgOdJDi6PBjZvGtSLzFAAABFklEQVR42u3aSQqAMBAAwbj8/80evWQUzEISq89BsXBAZNKhbEmSJEmSJEmvbWFgwIABAwYMGDBgwIABAwbMB5gzDAyYIpiVdlpimPtMDLPwfoxRAvNzmFrfH2DAgAEDBgwYMGDAgAEDBgyYpweqdQYMGDBgwIABAwYMmLE/5cGAATMnTOt9lFrXqXWmy36MUQIDBgwYMGDAgAEDBgwYMGBm+lcLBgwYMGDAgAEDBgwYMGDAgAEDZnGY1vsxo50p3o+Z8W3oMkpgwIABAwYMGDBgwIABAwYMGDBgwIABAwYMGDBgwIABAwYMGDBz7cfEN7EfY5TAgAEDBgwYMGDAgAEDBgwYMGDAgAEzKsyubEmSJEnSn7sAYYp0tdAZpfUAAAAASUVORK5CYII='
let LOGO_PNG = b'\64iVBORw0KGgoAAAANSUhEUgAAADAAAAAwCAMAAABg3Am1AAAAD1BMVEX///84vfgOdJDi6PBjZvGtSLzFAAAAvElEQVR42q3WwQ7EIAgEUEn4/2/ei7orzgxglqvz2qZV6Bh/KN/VS5eMgyrEbVZCzvCJaN5AEUHzRPD4Ip38LVbeq2Jd36UADyQe7LjFzrkWV968JgLwDNybJxG/CSkQkGKCsC7EfLNxVQsAhCCACwaooIAJDogQAAsFoNigKPz7qa0k4F5SIgV2d87REejESQEPtRCha6TCY2OyRFydryBGUzTaNx0QnYHSH1n9odgfuw+D/eHX4eHnJKsPU/cNRJD8S9EAAAAASUVORK5CYII='
let INVOICE_PDF = b'\64JVBERi0xLjQKMSAwIG9iago8PCAvVHlwZSAvQ2F0YWxvZyAvUGFnZXMgMiAwIFIgPj4KZW5kb2JqCjIgMCBvYmoKPDwgL1R5cGUgL1BhZ2VzIC9LaWRzIFszIDAgUl0gL0NvdW50IDEgPj4KZW5kb2JqCjMgMCBvYmoKPDwgL1R5cGUgL1BhZ2UgL1BhcmVudCAyIDAgUiAvTWVkaWFCb3ggWzAgMCA2MTIgNzkyXSAvQ29udGVudHMgNCAwIFIgL1Jlc291cmNlcyA8PCAvRm9udCA8PCAvRjEgNSAwIFIgPj4gPj4gPj4KZW5kb2JqCjQgMCBvYmoKPDwgL0xlbmd0aCAxNzkgPj4Kc3RyZWFtCkJUIC9GMSAyMCBUZiA3MiA3MjAgVGQgKEludm9pY2UgSU5WLTIwMjYtMDQxOCkgVGogMCAtMzIgVGQgL0YxIDEyIFRmIChOb3J0aHdpbmQgQ2xvdWQgLSBPY3RvYmVyIDIwMjYpIFRqIDAgLTI0IFRkIChDb21wdXRlIDQxMi4wMCBVU0QgICBTdG9yYWdlIDM3LjUwIFVTRCAgIFRvdGFsIDQ0OS41MCBVU0QpIFRqIEVUCmVuZHN0cmVhbQplbmRvYmoKNSAwIG9iago8PCAvVHlwZSAvRm9udCAvU3VidHlwZSAvVHlwZTEgL0Jhc2VGb250IC9IZWx2ZXRpY2EgPj4KZW5kb2JqCnhyZWYKMCA2CjAwMDAwMDAwMDAgNjU1MzUgZiAKMDAwMDAwMDAwOSAwMDAwMCBuIAowMDAwMDAwMDU4IDAwMDAwIG4gCjAwMDAwMDAxMTUgMDAwMDAgbiAKMDAwMDAwMDI0MSAwMDAwMCBuIAowMDAwMDAwNDcxIDAwMDAwIG4gCnRyYWlsZXIKPDwgL1NpemUgNiAvUm9vdCAxIDAgUiA+PgpzdGFydHhyZWYKNTQxCiUlRU9GCg=='

fn png(name, content) => {filename: name, type: "image/png", content: content}
fn pdf(name) => {filename: name, type: "application/pdf", content: INVOICE_PDF}
fn text_file(name, mime_type, text) => {filename: name, type: mime_type, content: text}

// ---- mailboxes ----

let BOXES = [
    {path: "INBOX", role: 'inbox'}, {path: "Drafts", role: 'drafts'}, {path: "Sent", role: 'sent'},
    {path: "Junk", role: 'junk'}, {path: "Trash", role: 'trash'}, {path: "Archive", role: 'archive'},
    {path: "Projects", role: null}, {path: "Projects/Radiant", role: null}, {path: "Projects/Mail", role: null},
    {path: "Receipts", role: null}]

// a mailbox as lambda.mail.sync.mailboxes lists one
fn mailbox(path, role) => {path: path, name: path, delimiter: "/", role: role, selectable: true}

// ---- HTML building blocks (inline styles, as mail clients need) ----

fn page(accent, brand, inner, footer) => join([
    "<!DOCTYPE html><html><body style='margin:0;padding:0;background:#f3f4f6'>",
    "<div style='max-width:560px;margin:0 auto;padding:28px 18px;font-family:-apple-system,Helvetica,Arial,sans-serif;color:#1f2937;font-size:14px;line-height:1.55'>",
    "<div style='font-size:12px;font-weight:700;letter-spacing:.08em;text-transform:uppercase;color:" ++ accent ++ ";margin:0 0 14px'>" ++ brand ++ "</div>",
    "<div style='background:#ffffff;border:1px solid #e5e7eb;border-radius:12px;padding:26px'>", inner, "</div>",
    "<p style='margin:16px 4px 0;font-size:11px;color:#9ca3af'>" ++ footer ++ "</p>",
    "</div></body></html>"], "\n")
fn h1(text) => "<h1 style='margin:0 0 10px;font-size:20px;line-height:1.3;color:#111827'>" ++ text ++ "</h1>"
fn para(text) => "<p style='margin:0 0 12px'>" ++ text ++ "</p>"
fn button(accent, label, href) => "<p style='margin:20px 0 4px'><a href='" ++ href ++ "' style='display:inline-block;background:" ++ accent ++
    ";color:#ffffff;text-decoration:none;padding:10px 18px;border-radius:8px;font-weight:600'>" ++ label ++ "</a></p>"
fn facts(rows) => join(["<table style='width:100%;border-collapse:collapse;margin:14px 0;font-size:13px'>",
    *[for (r in rows) "<tr><td style='padding:8px 0;border-top:1px solid #eef0f3;color:#6b7280'>" ++ r[0] ++
        "</td><td style='padding:8px 0;border-top:1px solid #eef0f3;text-align:right;font-weight:600'>" ++ r[1] ++ "</td></tr>"],
    "</table>"], "\n")
fn badge(color, text) => "<span style='display:inline-block;padding:3px 10px;border-radius:999px;background:" ++ color ++
    ";color:#ffffff;font-size:12px;font-weight:700'>" ++ text ++ "</span>"
fn bullets(items) => "<ul style='margin:0 0 12px;padding-left:20px'>" ++ join([for (i in items) "<li style='margin:4px 0'>" ++ i ++ "</li>"], "\n") ++ "</ul>"
// a personal note as a mail client's rich-text editor sends it
fn note(parts) => "<!DOCTYPE html><html><body><div style='font-family:-apple-system,Helvetica,Arial,sans-serif;font-size:14px;line-height:1.55;color:#1d1d1f'>\n" ++
    join(parts, "\n") ++ "\n</div></body></html>"

// ---- the messages ----

// A message spec: from, to, subject, age in minutes, a plain body, and
// optionally cc, html (else the plain body's own HTML, unless plain is set),
// inline (a cid image for the html), files and flags.
fn msg(from: any, subject: any, age: any, text: any) => {from: from, to: [ME], cc: [], subject: subject, age: age, text: text,
    html: null, plain: false, inline: null, files: [], unread: null, flagged: false}
fn lines(parts) => join(parts, "\n")
fn pick(items, i) => items[i % len(items)]
fn first_name(p) => split(p.name, " ")[0]

// hand-written threads, newest first by age
fn stories() => [
    {*: msg(P.maya, "Offsite agenda — final draft", 25, lines([
        "Hi Alex,", "",
        "Here is the final agenda for next week's offsite. Main changes since Monday:", "",
        "  • Day 1 starts at 9:30 instead of 9:00 (the ferry schedule changed)",
        "  • Priya's roadmap session moved to the afternoon",
        "  • We added a 45-minute demo slot, and I'd love for the mail client to be in it", "",
        "Could you confirm by Friday whether you need a projector or just a screen?", "",
        "Thanks!", "Maya"])), flagged: true, html: note([
        "<p>Hi Alex,</p>", "<p>Here is the <b>final agenda</b> for next week's offsite. Main changes since Monday:</p>",
        bullets(["Day 1 starts at <b>9:30</b> instead of 9:00 (the ferry schedule changed)", "Priya's roadmap session moved to the afternoon",
            "We added a <span style='background:#fff3bf'>45-minute demo slot</span>, and I'd love for the mail client to be in it"]),
        "<table style='border-collapse:collapse;margin:8px 0 14px;font-size:13px'>",
        "<tr><th style='text-align:left;padding:6px 14px 6px 0;border-bottom:2px solid #e5e7eb'>Time</th><th style='text-align:left;padding:6px 0;border-bottom:2px solid #e5e7eb'>Session</th></tr>",
        "<tr><td style='padding:6px 14px 6px 0;color:#6b7280'>9:30</td><td>Welcome and goals</td></tr>",
        "<tr><td style='padding:6px 14px 6px 0;color:#6b7280'>11:00</td><td>Architecture deep-dive</td></tr>",
        "<tr><td style='padding:6px 14px 6px 0;color:#6b7280'>14:00</td><td>Roadmap (Priya)</td></tr>",
        "<tr><td style='padding:6px 14px 6px 0;color:#6b7280'>15:30</td><td><b>Demos</b></td></tr></table>",
        "<p>Could you confirm by Friday whether you need a projector or just a screen?</p>", "<p>Thanks!<br>Maya</p>"])},
    {*: msg(P.jose, "Re: Layout regression in the flex suite", 70, lines([
        "Found it. The intrinsic width pass was measuring the scrollbar gutter twice",
        "when overflow:auto met a percentage max-width. Patch is up for review.", "",
        "On Thu, Alex Rivera wrote:",
        "> The flex baseline started failing on three fixtures this morning.",
        "> All three have a scroll container inside a percentage-width item.",
        "> Any chance it's the change from last night?", ">",
        "> > Jose Alvarez wrote:",
        "> > I'm landing the gutter refactor tonight; shout if anything looks off.", "",
        "— José"])), cc: [P.priya]},
    {*: msg(P.priya, "Q4 roadmap: numbers for the planning doc", 190, lines([
        "Alex,", "",
        "Attaching the usage chart we talked about. Weekly active scripts are up 18%",
        "since the interpreter tier landed, and the long tail of very short scripts is",
        "the main driver. I think that's the headline for the planning doc.", "",
        "The CSV has the raw weekly counts if you want to re-cut it.", "",
        "Priya"])), files: [png("weekly-active.png", CHART_PNG),
        text_file("weekly-active.csv", "text/csv", lines(["week,scripts", "36,10412", "37,10980", "38,11240", "39,11902",
            "40,12233", "41,12871", "42,12290"]))]},
    {*: msg(P.sam, "Lunch Thursday?", 260, lines([
        "There's a new ramen place on 4th that opened last week. Thursday at 12:30?",
        "Noor and Kenji are in.", "", "Sam"]))},
    {*: msg(P.mom, "Grandma's lemon cake", 9 * HOUR, lines([
        "Hi sweetheart,", "",
        "You asked for the recipe after the weekend, so here it is, typed up from",
        "Grandma's card. Don't skip the lemon zest, that's the whole trick.", "",
        "Call us on Sunday if you can!", "", "Love, Mom"])), files: [text_file("lemon-cake.txt", "text/plain", lines([
        "Grandma's lemon cake", "", "200 g butter, soft", "200 g sugar", "4 eggs", "200 g flour", "1 tsp baking powder",
        "zest and juice of 2 lemons", "", "Beat butter and sugar, add eggs one by one, fold in the flour and the zest.",
        "Bake 45 minutes at 175 °C. Pour the juice mixed with 2 tbsp sugar over the warm cake."]))]},
    {*: msg(P.lena, "Contract renewal — action needed", 27 * HOUR, lines([
        "Hello Alex,", "",
        "Your team's support contract renews on November 30. Nothing changes in the",
        "terms; I just need a signature from someone with purchasing authority.", "",
        "If that is not you, could you point me to the right person?", "",
        "Best regards,", "Lena Hoffmann", "Contoso Partner Success"])), flagged: true, html: note([
        "<p>Hello Alex,</p>", "<p>Your team's support contract renews on <b>November 30</b>. Nothing changes in the terms; I just need a signature from someone with purchasing authority.</p>",
        "<p>If that is not you, could you point me to the right person?</p>",
        "<p>Best regards,</p>",
        "<table style='border-collapse:collapse'><tr><td style='padding-right:12px;border-right:3px solid #2563eb'><b>Lena Hoffmann</b><br><span style='color:#6b7280'>Partner Success · Contoso</span></td>",
        "<td style='padding-left:12px;color:#6b7280;font-size:12px'>+49 30 5550 1234<br>contoso.example</td></tr></table>"])},
    {*: msg(P.kenji, "Re: Re: Benchmark numbers look off", 31 * HOUR, lines([
        "You were right, I was running the debug build. Release numbers:", "",
        "  json parse 50MB     0.41s   (was 0.44s)",
        "  markdown → html     0.12s   (was 0.19s)",
        "  layout, 2k nodes    38ms    (was 41ms)", "",
        "> Kenji, are those from a release build? The markdown number looks like",
        "> the debug allocator to me.", ">",
        "> > Kenji Watanabe wrote:",
        "> > Markdown to HTML got 2x slower after the merge, see attached.", "",
        "Kenji"])), plain: true},
    {*: msg(P.noor, "Design review notes", 2 * DAY + 3 * HOUR, lines([
        "Notes from today's review of the mail window:", "",
        "1. The unread dot should stay visible on selected rows.",
        "2. The sidebar counts read well; keep them right-aligned.",
        "3. HTML mail: keep remote content blocked by default, the bar is clear enough.",
        "4. Attachments strip: add file-type icons later, not a blocker.", "",
        "Overall: ship it for the demo.", "", "Noor"])), html: note([
        "<p>Notes from today's review of the mail window:</p>",
        "<ol style='margin:0 0 12px;padding-left:22px'><li>The unread dot should stay visible on selected rows.</li>",
        "<li>The sidebar counts read well; keep them right-aligned.</li>",
        "<li>HTML mail: keep remote content <b>blocked by default</b>, the bar is clear enough.</li>",
        "<li>Attachments strip: add file-type icons later, not a blocker.</li></ol>",
        "<p style='padding:10px 12px;background:#ecfdf5;border-radius:8px;color:#065f46'><b>Overall:</b> ship it for the demo. 🎉</p>", "<p>Noor</p>"])},
    {*: msg(P.tom, "Intro: Alex <> Tom (data pipeline)", 3 * DAY + 5 * HOUR, lines([
        "Hi Alex,", "",
        "Lena mentioned you built the document conversion tooling. We are moving",
        "a few thousand legacy LaTeX reports to HTML and would love to compare notes.", "",
        "Would 30 minutes next week work?", "", "Tom"])), cc: [P.lena]},
    {*: msg(P.maya, "Re: Offsite agenda", 4 * DAY, lines([
        "Booked the venue. Rooms are on the second floor, with a view of the harbor.", "",
        "> Can we get a room with natural light this time?", "", "M."]))},
    {*: msg(P.priya, "Hiring: two loops next week", 5 * DAY + 2 * HOUR, lines([
        "Could you take the systems design interview for both candidates?",
        "Tuesday 14:00 and Thursday 10:00. Calendar invites are coming.", "", "Thanks, Priya"]))},
    {*: msg(P.jose, "Weekend plans?", 8 * DAY, lines([
        "A few of us are hiking Mount Tam on Saturday morning. Car leaves at 8.",
        "Bring water, it gets warm by noon.", "", "José"]))},
    {*: msg(P.sam, "Re: Old laptop", 12 * DAY, lines([
        "IT says you can keep it if you wipe it first. They need the asset tag back though.", "",
        "> Do I need to return the old laptop or can I keep it for testing?", "", "Sam"]))},
    {*: msg(P.kenji, "Conference talk accepted!", 19 * DAY, lines([
        "Our talk on incremental layout got accepted. 30 minutes, second day.", "",
        "Let's sync on the outline next week.", "", "Kenji"])), flagged: true}
]

// one HTML newsletter with a cid logo and a remote hero image (blocked until asked)
fn newsletter(i) {
    let issue = 112 - i
    let topics = [["Pattern matching for strings", "How the new \\( … ) patterns compile to a DFA."],
        ["A tour of the T0 interpreter", "Why short scripts no longer pay for the JIT."],
        ["Laying out tables, fast", "Column widths in one pass, and where the second pass still hides."],
        ["Documents in, documents out", "Markdown, LaTeX and PDF through one Mark tree."],
        ["Reading mail with Lambda", "IMAP, POP3 and a sandboxed HTML viewer in a few hundred lines."]]
    let t = pick(topics, i)
    let html = join(["<!DOCTYPE html><html><body style='margin:0;background:#f1f5f9;font-family:-apple-system,Helvetica,Arial,sans-serif;color:#1f2937'>",
        "<div style='max-width:600px;margin:0 auto;background:#ffffff'>",
        "<div style='background:#0f172a;padding:18px 24px'><img src='cid:logo@weekly' width='48' height='48' alt='' style='vertical-align:middle'>",
        "<span style='color:#e2e8f0;font-size:20px;margin-left:12px;vertical-align:middle'>Lambda Weekly · issue " ++ string(issue) ++ "</span></div>",
        "<img src='https://images.weekly.example/hero-" ++ string(issue) ++ ".jpg' width='600' height='200' alt='' style='display:block;width:100%;height:auto;background:#cbd5e1'>",
        "<div style='padding:8px 24px 24px'><h1 style='font-size:24px;margin:18px 0 8px'>" ++ t[0] ++ "</h1>",
        "<p style='font-size:15px;line-height:1.6;color:#334155'>" ++ t[1] ++ "</p>",
        "<h3 style='margin:22px 0 6px;font-size:13px;letter-spacing:.06em;text-transform:uppercase;color:#64748b'>Also this week</h3>",
        bullets(["Three new packages: <b>csv</b>, <b>ics</b> and <b>units</b>", "A faster CSV reader, 2.3× on wide files", "Call for talks: LambdaConf 2027"]),
        "<p><a href='https://weekly.example/issues/" ++ string(issue) ++ "' style='display:inline-block;background:#0ea5e9;color:#ffffff;text-decoration:none;padding:10px 18px;border-radius:8px;font-weight:600'>Read the full issue</a></p>",
        "<p style='color:#94a3b8;font-size:12px;border-top:1px solid #e2e8f0;padding-top:14px;margin-top:24px'>You are receiving this because you subscribed at weekly.example. ",
        "<a href='https://weekly.example/unsubscribe' style='color:#64748b'>Unsubscribe</a></p></div></div></body></html>"], "\n");
    {*: msg(P.weekly, "Lambda Weekly #" ++ string(issue) ++ ": " ++ t[0], 30 * HOUR + i * 7 * DAY,
        t[0] ++ "\n\n" ++ t[1] ++ "\n\nRead the full issue: https://weekly.example/issues/" ++ string(issue)),
     html: html, inline: {cid: "logo@weekly", file: png("logo.png", LOGO_PNG)}}
}

let PR_TITLES = ["Fix scrollbar gutter in intrinsic sizing", "Add POP3 read state to the mail store",
    "Speed up shape transitions for wide maps", "Teach the CSV reader about BOMs", "Grid: honor min-content tracks",
    "Interp: cache field offsets per call site", "Docs: tutorial chapter 7 examples", "SVG: numeric attributes on <image>",
    "Mark builder: share element shapes", "Table layout: collapse borders in one pass", "LaTeX: support \\newenvironment",
    "Mail: virtualize the message list"]
let COMMENTS = ["Could we keep the old fast path behind the flag for one release?", "Nit: this comment explains what, not why.",
    "Do we have a test for the empty case?", "This reads much better now, thanks."]

fn escaped(text) => replace(replace(replace(text, "&", "&amp;"), "<", "&lt;"), ">", "&gt;")

fn review(i) {
    let n = 912 - i
    let who = pick(TEAM, i * 3 + 1)
    let action = pick(["requested your review on", "approved", "commented on", "merged"], i)
    let title = pick(PR_TITLES, i)
    let url = "https://forge.example/acme/lambda/pull/" ++ string(n)
    let comment = pick(COMMENTS, i)
    let stat = string(3 + i % 9) ++ " files changed, +" ++ string(40 + i * 13 % 300) ++ " −" ++ string(5 + i * 7 % 120)
    let body = if (action == "commented on") lines([first_name(who) ++ " commented:", "", "> " ++ comment, "", "View it on Forge: " ++ url])
        else lines([first_name(who) ++ " " ++ action ++ " this pull request.", "", "  " ++ title, "  " ++ stat, "", "View it on Forge: " ++ url])
    let color = {approved: "#16a34a", merged: "#7c3aed"}[pick(["requested", "approved", "commented", "merged"], i)] or "#2563eb"
    let html = page("#7c3aed", "Forge · acme/lambda", join([
        "<p style='margin:0 0 10px;color:#6b7280'><b style='color:#111827'>" ++ escaped(who.name) ++ "</b> " ++ action ++ "</p>",
        h1(escaped(title) ++ " <span style='color:#9ca3af;font-weight:400'>#" ++ string(n) ++ "</span>"),
        if (action == "commented on") "<blockquote style='margin:12px 0;padding:10px 14px;background:#f9fafb;border-left:3px solid #d1d5db;border-radius:4px'>" ++ escaped(comment) ++ "</blockquote>"
        else "<p style='margin:8px 0'>" ++ badge(color, upper(slice(action, 0, 1)) ++ slice(action, 1)) ++ " <span style='color:#6b7280;font-size:13px;margin-left:8px'>" ++ stat ++ "</span></p>",
        button("#111827", "View pull request", url)], "\n"), "You are receiving this because you are watching acme/lambda.");
    {*: msg({*: P.forge, name: who.name ++ " (Forge)"}, "[acme/lambda] " ++ title ++ " (PR #" ++ string(n) ++ ")", 5 * HOUR + i * 47 * HOUR, body), html: html}
}

fn build(i) {
    let n = 4821 - i
    let failed = i % 7 == 2
    let branch = pick(["master", "master", "mail-demo", "flex-gutter", "master", "interp-tier"], i)
    let status = if (failed) "failed" else "passed"
    let sha = slice("d45222121ea85cf9eb6747b9eaea2a23f4677b1f517", i % 30, i % 30 + 9)
    let tests = if (failed) "5,912 passed, 17 failed" else "5,929 passed"
    let took = string(11 + i % 6) ++ "m " ++ string(i * 17 % 60) ++ "s"
    let url = "https://forge.example/acme/lambda/builds/" ++ string(n)
    let html = page(if (failed) "#dc2626" else "#16a34a", "Forge CI", join([
        badge(if (failed) "#dc2626" else "#16a34a", if (failed) "✕ Failed" else "✓ Passed"),
        "<div style='height:10px'></div>", h1("Build #" ++ string(n) ++ " " ++ status),
        facts([["Branch", branch], ["Commit", "<code>" ++ sha ++ "</code>"], ["Tests", tests], ["Duration", took]]),
        if (failed) "<p style='margin:0;padding:10px 12px;background:#fef2f2;border-radius:8px;color:#991b1b;font-size:13px'>First failure: <code>ViewTree.SvgImageNumericAttrs</code></p>" else "",
        button("#111827", "View build", url)], "\n"), "Forge CI · acme/lambda");
    {*: msg(P.ci, "[acme/lambda] Build #" ++ string(n) ++ " " ++ status ++ " on " ++ branch, 3 * HOUR + i * 31 * HOUR, lines([
        "Build #" ++ string(n) ++ " " ++ status ++ " in " ++ took ++ ".", "",
        "  branch   " ++ branch, "  commit   " ++ sha, "  tests    " ++ tests, "",
        if (failed) "First failure: test_radiant_view_gtest  ViewTree.SvgImageNumericAttrs" else "All checks passed.", "", url])),
     html: html, unread: if (failed and i < 12) true else null}
}

fn invite(i) {
    let who = pick(TEAM, i + 2)
    let what = pick(["1:1", "Design review: mail window", "Sprint planning", "Interview loop debrief", "Release go/no-go",
        "Reading group: incremental layout"], i)
    let day = pick(["Mon", "Tue", "Wed", "Thu", "Fri"], i * 2)
    let long_day = pick(["Monday", "Tuesday", "Wednesday", "Thursday", "Friday"], i * 2)
    let hour = 9 + i % 8
    let place = pick(["Room Harbor", "Video call", "Room Lighthouse", "Cafe corner"], i)
    let html = page("#4f46e5", "Northwind Calendar", join([
        "<table style='border-collapse:collapse'><tr><td style='width:64px;padding:8px 0;text-align:center;background:#eef2ff;border-radius:10px'>",
        "<div style='font-size:11px;font-weight:700;color:#4f46e5;text-transform:uppercase'>" ++ day ++ "</div>",
        "<div style='font-size:26px;font-weight:700;color:#1e1b4b'>" ++ string(hour) ++ "</div>",
        "<div style='font-size:11px;color:#6366f1'>:00</div></td>",
        "<td style='padding-left:16px;vertical-align:top'>" ++ h1(escaped(what)),
        "<div style='color:#6b7280'>" ++ long_day ++ ", " ++ string(hour) ++ ":00 – " ++ string(hour) ++ ":30 · " ++ place ++ "</div></td></tr></table>",
        para("<br>" ++ escaped(who.name) ++ " invited you and 2 others."),
        "<p style='margin:18px 0 0'><a href='https://calendar.northwind.example/rsvp/yes' style='display:inline-block;padding:8px 16px;border-radius:8px;background:#4f46e5;color:#fff;text-decoration:none;font-weight:600'>Yes</a> ",
        "<a href='https://calendar.northwind.example/rsvp/maybe' style='display:inline-block;padding:8px 16px;border-radius:8px;border:1px solid #c7d2fe;color:#4f46e5;text-decoration:none;font-weight:600'>Maybe</a> ",
        "<a href='https://calendar.northwind.example/rsvp/no' style='display:inline-block;padding:8px 16px;border-radius:8px;border:1px solid #e5e7eb;color:#6b7280;text-decoration:none;font-weight:600'>No</a></p>"], "\n"),
        "Invitation from Northwind Calendar");
    {*: msg(P.cal, "Invitation: " ++ what ++ " @ " ++ day ++ " " ++ string(hour) ++ ":00", 20 * HOUR + i * 5 * DAY, lines([
        who.name ++ " has invited you to " ++ what ++ ".", "",
        "  When    " ++ long_day ++ ", " ++ string(hour) ++ ":00 – " ++ string(hour) ++ ":30",
        "  Where   " ++ place, "  Guests  " ++ who.name ++ ", " ++ ME.name, "", "Going?  Yes · Maybe · No"])), html: html}
}

fn invoice(i, base) {
    let month = pick(["October", "September", "August", "July", "June", "May", "April", "March", "February", "January", "December", "November"], i)
    let amount = string(380 + i * 37 % 120) ++ ".50"
    let html = page("#0369a1", "Northwind Cloud", join([h1("Your invoice for " ++ month),
        para("Thanks for building on Northwind Cloud. Here is your summary; the PDF is attached."),
        facts([["Compute", string(300 + i * 29 % 100) ++ ".00 USD"], ["Storage", "37.50 USD"], ["Network", "43.00 USD"],
            ["<b style='color:#111827'>Total due</b>", "<span style='font-size:16px'>" ++ amount ++ " USD</span>"]]),
        button("#0369a1", "View invoice", "https://billing.northwind-cloud.example/invoices")], "\n"),
        "Northwind Cloud · 1 Harbor Way · invoices are due on the 15th");
    {*: msg(P.billing, "Your Northwind Cloud invoice for " ++ month, base + i * 30 * DAY, lines([
        "Hello Alex,", "", "Your invoice for " ++ month ++ " is ready.", "",
        "  Amount due   " ++ amount ++ " USD", "  Due date     the 15th", "",
        "The PDF is attached. Thank you for using Northwind Cloud."])), html: html, files: [pdf("invoice-" ++ lower(month) ++ ".pdf")]}
}

fn shipment(i) {
    let item = pick(["mechanical keyboard", "USB-C dock", "standing desk mat", "noise-cancelling headphones", "book: Crafting Interpreters"], i)
    let step = i % 3
    let stage = pick(["has shipped", "is out for delivery", "was delivered"], i)
    let tracking = "PP" ++ string(48213907 + i * 7919)
    let bar = join([for (k in [0, 1, 2]) "<td style='width:33%;padding:0 3px'><div style='height:6px;border-radius:3px;background:" ++
        (if (k <= step) "#f97316" else "#e5e7eb") ++ "'></div><div style='font-size:11px;margin-top:6px;color:" ++
        (if (k <= step) "#c2410c" else "#9ca3af") ++ "'>" ++ ["Shipped", "Out for delivery", "Delivered"][k] ++ "</div></td>"], "")
    let html = page("#ea580c", "Parcelpost", join([h1("Your " ++ escaped(item) ++ " " ++ stage),
        "<table style='width:100%;border-collapse:collapse;margin:16px 0'><tr>" ++ bar ++ "</tr></table>",
        facts([["Tracking number", tracking], ["Carrier", "Parcelpost Ground"], ["Ship to", "Alex Rivera, San Francisco"]]),
        button("#ea580c", "Track package", "https://parcelpost.example/track/" ++ tracking)], "\n"), "Parcelpost delivery updates");
    {*: msg(P.ship, "Your " ++ item ++ " " ++ stage, 14 * HOUR + i * 4 * DAY + i * 5 * HOUR, lines([
        "Good news! Your " ++ item ++ " " ++ stage ++ ".", "",
        "  Tracking number   " ++ tracking, "  Carrier           Parcelpost Ground", "",
        "Track it: https://parcelpost.example/track/" ++ tracking])), html: html}
}

fn security_alert(i) {
    let what = pick(["New sign-in on Mac", "Your password was changed", "New sign-in on iPhone", "Two-step verification turned on"], i)
    let seen_at = pick(["Tuesday morning", "last Friday", "Sunday evening", "Monday"], i)
    let where_ = pick(["San Francisco, CA", "Oakland, CA", "Seattle, WA", "San Francisco, CA"], i)
    let html = page("#b91c1c", "Account Security", join([h1(what), para("We noticed a new security event on your account."),
        facts([["When", seen_at], ["Where", where_], ["Device", pick(["MacBook Pro", "Chrome on Mac", "iPhone 17", "MacBook Air"], i)]]),
        para("If this was you, there is nothing to do."), button("#b91c1c", "Review activity", "https://accounts.example/security")], "\n"),
        "This is a mandatory service message.");
    {*: msg(P.secure, what, 2 * DAY + i * 17 * DAY, lines([
        "We noticed a new security event on your account.", "", "  When      " ++ seen_at, "  Where     " ++ where_, "",
        "If this was you, there is nothing to do. If not, secure your account at https://accounts.example/security."])), html: html}
}

let LIST_TOPICS = ["Proposal: string patterns in split()", "Release 0.9 checklist", "Why is my map shape not shared?",
    "Windows build failing on MSVC 19.40", "RFC: a unified inbox for the mail demo", "Interpreter tier-up thresholds",
    "Docs: where should packages live?", "Grid auto-placement and dense packing", "Question about pn vs fn",
    "Flaky test: radiant baseline zengarden", "Idea: Mark diff for document updates", "PDF text extraction accuracy"]
let LIST_LINES = ["I think we should keep this behavior and document it.", "+1, this has bitten me twice this month.",
    "Can you share a minimal script that reproduces it?", "The spec ruling covers this case; see the formal semantics doc.",
    "I tried this on the release build and it's about 30% faster.", "Let's discuss at the sync on Thursday.",
    "Patch attached on the tracker. Reviews welcome.", "Agreed with the direction, minor comments inline."]

// mailing-list traffic, plain text as lists send it: replies quote the previous message
fn list_post(i, base) {
    let who = pick(TEAM, i * 5 + 2)
    let earlier = pick(TEAM, i * 5 + 4)
    let topic = pick(LIST_TOPICS, i)
    let reply = i % 3 != 0
    let body = lines([pick(LIST_LINES, i * 3 + 1), "", pick(LIST_LINES, i + 5), "",
        *(if (reply) ["On " ++ pick(["Monday", "Tuesday", "Wednesday", "Thursday", "Friday"], i) ++ ", " ++ earlier.name ++ " wrote:",
            "> " ++ pick(LIST_LINES, i * 7 + 2), "> " ++ pick(LIST_LINES, i * 7 + 3), ""] else []),
        "-- ", first_name(who), "", "_______________________________________________",
        "lambda-dev mailing list · https://lists." ++ DOMAIN ++ "/lambda-dev"]);
    {*: msg({name: who.name ++ " via lambda-dev", address: P.list.address}, (if (reply) "Re: " else "") ++ "[lambda-dev] " ++ topic,
        base + i * 37 * HOUR + i * i * 3, body), to: [P.list], plain: true}
}

fn sent(i) {
    let to = pick(TEAM, i * 3);
    {*: msg(ME, pick(["Re: ", "Re: ", ""], i) ++ pick(["Notes from today", "Quick question about the build", "Slides for Thursday",
        "Re: Offsite agenda", "Your review", "Following up", "Thanks!", "Draft of the planning doc"], i), 3 * HOUR + i * 53 * HOUR,
        lines(["Hi " ++ first_name(to) ++ ",", "", pick(LIST_LINES, i + 2), "", pick(LIST_LINES, i * 5), "", "Alex"])), to: [to]}
}

fn junk(i) => {*: msg(pick([P.spam, {name: "Crypto Desk", address: "alerts@coin-moon.example"},
        {name: "Pharmacy Deals", address: "deals@cheap-meds.example"}], i),
    pick(["You have WON a $1,000 gift card!!!", "URGENT: confirm your wallet now", "Exclusive offer just for you",
        "Re: your invoice #88213", "Last chance: claim your reward"], i), 6 * HOUR + i * 2 * DAY + i * 3 * HOUR,
    lines(["Dear valued customer,", "", "Click the link below within 24 hours to claim your prize.",
        "http://totally-legit.example/claim?id=" ++ string(9012 + i), "", "This message was sent to many recipients."])),
    unread: true, html: join(["<!DOCTYPE html><html><body style='margin:0;background:#fde047;font-family:Arial,sans-serif;text-align:center'>",
        "<img src='https://track.prizes-4-u.example/open.gif?id=" ++ string(9012 + i) ++ "' width='1' height='1' alt=''>",
        "<h1 style='font-size:40px;color:#dc2626;margin:30px 0 10px'>CONGRATULATIONS!!!</h1>",
        "<p style='font-size:20px'>You have been <b>selected</b> for an exclusive reward.</p>",
        "<p><a href='http://totally-legit.example/claim?id=" ++ string(9012 + i) ++ "' style='display:inline-block;background:#16a34a;color:#fff;font-size:22px;padding:16px 30px;border-radius:40px;text-decoration:none'>CLAIM NOW &gt;&gt;</a></p>",
        "<p style='font-size:10px;color:#854d0e'>This message was sent to many recipients.</p></body></html>"], "\n")}

fn radiant_issue(i) {
    let title = pick(["Table border collapse off by one pixel", "Sticky header jumps on fast scroll", "Grid gap ignored with auto-fill",
        "Text overflow ellipsis on RTL", "SVG image with numeric width", "Flex baseline with scroll container"], i)
    let repro = "test/layout/data/" ++ pick(["table", "position", "grid", "text", "svg", "flex"], i) ++ "/case-" ++ string(140 + i * 11) ++ ".html"
    let off = string(1 + i % 4) ++ "px"
    let html = page("#7c3aed", "Forge · acme/lambda issues", join([h1("[radiant] " ++ title),
        facts([["Repro", "<code>" ++ repro ++ "</code>"], ["Difference", off ++ " on the second row"], ["Labels", badge("#0891b2", "layout") ++ " " ++ badge("#9333ea", "regression")]]),
        para("Looks related to the float→int cast cleanup."), button("#111827", "View issue", "https://forge.example/acme/lambda/issues/" ++ string(200 + i))], "\n"),
        "You are receiving this because you are assigned.");
    {*: msg(pick(TEAM, i + 1), "[radiant] " ++ title, 6 * DAY + i * 3 * DAY, lines(["Repro: " ++ repro, "",
        "Expected (Chrome) vs actual (Radiant) differ by " ++ off ++ " on the second row.", "Looks related to the float→int cast cleanup."])), html: html}
}

fn mail_issue(i) => msg(pick(TEAM, i + 4), "[mail] " ++ pick(["Mark IMAP messages read on the server", "IDLE push",
    "Keyboard navigation in the list", "Unified Inbox", "Open links in the system browser", "Search"], i),
    4 * DAY + i * 4 * DAY, lines(["Milestone M2 item.", "", pick(LIST_LINES, i * 2), "", "Tracking: https://forge.example/acme/lambda/issues/" ++ string(300 + i)]))

fn draft(i) => {*: msg(ME, pick(["Re: Contract renewal — action needed", "Talk outline: incremental layout", "(no subject yet)"], i),
    2 * HOUR + i * 26 * HOUR, pick([lines(["Hi Lena,", "", "Thanks for the reminder. I'll get you a signature by", ""]),
        lines(["1. Why incremental layout", "2. Dirty bits and where they lie", "3. Demo: the mail window", "4. Numbers", ""]),
        "Note to self: ask Priya about the"], i)), to: [pick([P.lena, P.kenji, P.priya], i)]}

// The specs of every mailbox, by path. Ages are minutes before the seeding.
fn specs() => {
    INBOX: [*stories(), *[for (i in 0 to 15) newsletter(i)], *[for (i in 0 to 29) review(i)], *[for (i in 0 to 39) build(i)],
        *[for (i in 0 to 9) invite(i)], *[for (i in 0 to 3) invoice(i, 2 * DAY)], *[for (i in 0 to 9) shipment(i)],
        *[for (i in 0 to 3) security_alert(i)], *[for (i in 0 to 59) list_post(i, 2 * HOUR)]],
    Drafts: [for (i in 0 to 2) draft(i)],
    Sent: [for (i in 0 to 24) sent(i)],
    Junk: [for (i in 0 to 7) junk(i)],
    Trash: [*[for (i in 0 to 5) build(i + 40)], *[for (i in 0 to 3) shipment(i + 10)]],
    Archive: [*[for (i in 0 to 49) list_post(i + 60, 70 * DAY)], *[for (i in 0 to 9) review(i + 30)]],
    Projects: [for (i in 0 to 5) msg(pick(TEAM, i), pick(["Q4 goals", "Team rituals", "On-call rotation", "Retro notes",
        "Budget for conferences", "Tooling wishlist"], i), 3 * DAY + i * 9 * DAY, lines([pick(LIST_LINES, i), "", pick(LIST_LINES, i + 3)]))],
    ["Projects/Radiant"]: [for (i in 0 to 11) radiant_issue(i)],
    ["Projects/Mail"]: [for (i in 0 to 5) mail_issue(i)],
    Receipts: [for (i in 0 to 11) invoice(i, 120 * DAY)]
}

// what Get Mail delivers, one per press
let ARRIVALS = [
    msg(P.maya, "Quick one: demo slot confirmed", 0, lines(["You're on at 15:30 tomorrow, 10 minutes. Good luck!", "", "Maya"])),
    msg(P.kenji, "Slides look great", 0, lines(["Just went through them. Maybe one more slide on the virtualized list?", "", "Kenji"])),
    msg(P.jose, "Coffee?", 0, lines(["Heading to the cafe downstairs in 5, want anything?", "", "José"])),
    msg(P.priya, "Planning doc is live", 0, lines(["Shared it with the whole team. Comments welcome until Friday.", "", "Priya"])),
    msg(P.noor, "Re: Design review notes", 0, lines(["One more: the compose window looks great. Ship it!", "", "Noor"]))]

// ---- writing a message ----

fn text_part(text) => {content: text, type: "text/plain; charset=UTF-8", encoding: compose.transfer_encoding(text)}
fn html_part(s, html) => (let part = {content: html, type: "text/html; charset=UTF-8", encoding: compose.transfer_encoding(html)},
    if (s.inline == null) part
    else {type: "multipart/related", parts: [part, {*: s.inline.file, disposition: "inline", content_id: "<" ++ s.inline.cid ++ ">"}]})

// plain text alone, or multipart/alternative with the spec's HTML or the text's own
fn body(s) => if (s.plain) text_part(s.text)
    else {type: "multipart/alternative", parts: [text_part(s.text), html_part(s, s.html or compose.html_of_text(s.text))]}

fn rfc5322(s, message_id, sent_at) string^ => misc.format({from: s.from, to: s.to, *: (if (len(s.cc) > 0) {cc: s.cc} else {}),
    subject: s.subject, date: sent_at, message_id: message_id, body: body(s), attachments: s.files}, {type: "eml"})^

// read by default, except recent mail in the Inbox and the Junk folder
fn flags(path, s, k) => [*(if (s.unread == true or (s.unread == null and path == "INBOX" and s.age < 2 * DAY and k % 3 != 2)) [] else ["\\Seen"]),
    *(if (s.flagged) ["\\Flagged"] else []), *(if (path == "Drafts") ["\\Draft"] else [])]

// ---- the store ----


fn entry(uid, id, flag_list, message, size) => {uid: uid, file: id, flags: flag_list, *: sync.header_entry(message),
    *: sync.body_entry(message), internaldate: null, size: size, thread: null, has_file: true}

// Files one message's text under store id `id`; its index entry.
pn file_text(dir, id, text, uid, flag_list) map^ {
    let message = sync.file_message(dir, id, binary(text))^
    entry(uid, id, flag_list, message, len(binary(text)))
}

pn marker(dir) { if (exists(dir ++ "/demo.mark")) (input(dir ++ "/demo.mark", 'mark') ^ { null }) else null }

pub pn is_seeded(dir) { marker(dir) != null }

// The next store ids, one counter for the whole store: a moved message keeps
// its file, so ids are never derived from a mailbox and a UID.
pn take_ids(dir, count) int^ {
    let m = marker(dir)
    let first = if (m == null or m.files == null) 1 else m.files + 1
    output({*: (m or {}), files: first + count - 1}, dir ++ "/demo.mark", {atomic: true})^
    first
}

pub pn load(dir, path) {
    let loaded = store.load_index(store.index_path(dir, store.safe_name(path)))
    return if (loaded == null) {version: 1, path: path, uidvalidity: 1, uidnext: 1, highestmodseq: null, entries: [], delivered: 0} else loaded
}

// Saves a mailbox's entries; UIDs only grow, as on a server (RFC 3501 §2.3.1.1).
pn saved(dir, index, entries) map^ {
    let next = {*: index, entries: entries, uidnext: max(index.uidnext or 1, store.highest_uid({entries: entries}) + 1)}
    store.save_index(store.index_path(dir, store.safe_name(index.path)), next)^
    next
}

pn next_uid(index) { max(index.uidnext or 1, store.highest_uid(index) + 1) }

// The account's mailboxes, as last saved.
pub pn mailboxes(dir) {
    let saved_boxes = if (exists(dir ++ "/mailboxes.mark")) (input(dir ++ "/mailboxes.mark", 'mark') ^ { null }) else null
    return if (saved_boxes == null) [for (b in BOXES) mailbox(b.path, b.role)] else saved_boxes
}

// Writes every mailbox once: the oldest message gets UID 1, as on a server.
pub pn seed(dir) null^ {
    let now_ms = datetime().unix
    let all = specs()
    var next_id = 1
    for (b in BOXES) {
        let ordered = sort(all[b.path], (s) => 0 - s.age)
        var entries = []
        for (i, s in ordered) {
            let id = "demo-" ++ string(next_id)
            let text = rfc5322(s, "<" ++ id ++ "@" ++ DOMAIN ++ ">", compose.rfc5322_date(now_ms - s.age * MINUTE))^
            entries = [*entries, file_text(dir, id, text, i + 1, flags(b.path, s, len(ordered) - 1 - i))^]
            next_id = next_id + 1
        }
        saved(dir, {*: load(dir, b.path), delivered: 0}, entries)^
    }
    output([for (b in BOXES) mailbox(b.path, b.role)], dir ++ "/mailboxes.mark", {atomic: true})^
    output({seeded: datetime(), files: next_id - 1}, dir ++ "/demo.mark", {atomic: true})^
    null
}

// Files RFC 5322 `text` at the end of a mailbox; the saved index.
pub pn file_into(dir, path, text, flag_list) map^ {
    let index = load(dir, path)
    let id = "demo-" ++ string(take_ids(dir, 1)^)
    saved(dir, index, [*index.entries, file_text(dir, id, text, next_uid(index), flag_list)^])
}

// Get Mail: the next arrival lands in the Inbox, dated now; null once all have arrived.
pub pn deliver(dir) map^ {
    let inbox = load(dir, "INBOX")
    let n = inbox.delivered or 0
    if (n >= len(ARRIVALS)) { return null }
    let now_ms = datetime().unix
    let text = rfc5322(ARRIVALS[n], "<arrival-" ++ string(now_ms) ++ "@" ++ DOMAIN ++ ">", compose.rfc5322_date(now_ms))^
    let filed = file_into(dir, "INBOX", text, [])^
    saved(dir, {*: filed, delivered: n + 1}, filed.entries)
}

pub pn set_flag(dir, path, uid, flag, enabled) map^ {
    let index = load(dir, path)
    saved(dir, index, store.with_flag(index.entries, uid, flag, enabled))
}

// Moves one message to another mailbox under a new UID; [source, target]
// indexes, or null when the message is gone.
pub pn move(dir, path, uid, to_path) array^ {
    let source = load(dir, path)
    let found = [for (e in source.entries where e.uid == uid) e]
    if (len(found) == 0 or to_path == path) { return null }
    let target = load(dir, to_path)
    let left = saved(dir, source, [for (e in source.entries where e.uid != uid) e])^;
    [left, saved(dir, target, [*target.entries, {*: found[0], uid: next_uid(target)}])^]
}

// Deletes one message for good, with its files.
pub pn erase(dir, path, uid) map^ {
    let index = load(dir, path)
    for (e in index.entries) {
        if (e.uid == uid) {
            io.delete(store.message_path(dir, e.file)) ^ { null }
            io.delete(store.text_path(dir, e.file)) ^ { null }
        }
    }
    saved(dir, index, [for (e in index.entries where e.uid != uid) e])
}

// A new mailbox (a child when `path` has a "/"); the account's mailboxes.
pub pn create_mailbox(dir, path) array^ {
    let boxes = mailboxes(dir)
    if (path in [for (b in boxes) b.path]) { return boxes }
    let next = [*boxes, mailbox(path, null)]
    output(next, dir ++ "/mailboxes.mark", {atomic: true})^
    next
}
