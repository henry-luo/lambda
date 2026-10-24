// lambda.mail.compose — new messages, replies and forwards (vibe/Lambda_Demo_Mail.md
// §4.6): the draft a reply or forward starts from, quoting, and the RFC 5322
// text a draft becomes through lambda.doc.misc's eml writer. Pure functions.
//
// A draft is {to, cc, subject, text, in_reply_to, references, attachments}:
// `to` and `cc` are the address lists as typed ("Ann <ann@x.org>, bo@y.org"),
// `text` the plain-text body, and `attachments` [{filename, type, content}].
// A message is what lambda.mail.mime reads: {from, to, cc, reply_to,
// subject, date, message_id, references, text, attachments}.
import misc: lambda.doc.misc

pub fn blank() => {to: "", cc: "", subject: "", text: "", in_reply_to: null, references: null, attachments: []}

pub fn person_label(a) => if (a.name != null and a.name != "") a.name ++ " <" ++ a.address ++ ">" else a.address
pub fn people_label(people) => join([for (a in people) person_label(a)], ", ")

fn prefixed(subject, prefix) => lower(slice(trim(subject or ""), 0, len(prefix))) == prefix
pub fn reply_subject(subject) => if (prefixed(subject, "re:")) trim(subject) else "Re: " ++ trim(subject or "")
pub fn forward_subject(subject) => if (prefixed(subject, "fwd:") or prefixed(subject, "fw:")) trim(subject)
    else "Fwd: " ++ trim(subject or "")

// ---- quoting ----

// one more quote level on every line ("> " on new ones, ">" on quoted ones)
pub fn quote(text) => join([for (line in split(replace(text or "", "\r\n", "\n"), "\n"))
    if (starts_with(line, ">")) ">" ++ line else "> " ++ line], "\n")

fn quote_depth(line) => (let marks = [for (ch in split(replace(line, " ", ""), "")) ch], let n = [for (i, ch in marks where ch != ">") i],
    if (len(n) == 0) len(marks) else n[0])
fn unquoted(line, depth) => if (depth == 0) line else (let rest = [for (i, ch in split(line, "")) {i: i, ch: ch}],
    let seen = [for (r in rest where r.ch == ">") r.i], if (len(seen) < depth) line
    else (let cut = slice(line, seen[depth - 1] + 1), if (starts_with(cut, " ")) slice(cut, 1) else cut))

// plain text as runs of lines at one quote depth ("> > " is depth 2)
pub fn quote_runs(text) {
    let lines = split(replace(text or "", "\r\n", "\n"), "\n")
    let tagged = [for (line in lines) {depth: quote_depth(line), line: line}]
    let starts = [for (i, t in tagged where i == 0 or tagged[i - 1].depth != t.depth) i];
    [for (k, s in starts) (let end = if (k + 1 < len(starts)) starts[k + 1] else len(tagged),
        {depth: tagged[s].depth, text: join([for (i in range(s, end, 1)) unquoted(tagged[i].line, tagged[s].depth)], "\n")})]
}

fn escaped(text) => replace(replace(replace(text, "&", "&amp;"), "<", "&lt;"), ">", "&gt;")
fn repeated(n, tag) => join([for (i in range(0, n, 1)) tag], "")

// The HTML alternative of a plain-text body, as mail clients send it: line
// breaks kept, and quote levels as nested <blockquote type="cite">. Each run
// opens or closes only the levels its depth differs from the previous one by.
pub fn html_of_text(text) {
    let cite = "<blockquote type=\"cite\" style=\"margin:4px 0 4px 2px;padding-left:10px;border-left:2px solid #8fb3e8;color:#3a5378\">"
    let runs = quote_runs(text)
    let depths = [0, *[for (r in runs) r.depth]]
    let body = join([for (k, r in runs) repeated(max(0, r.depth - depths[k]), cite) ++ repeated(max(0, depths[k] - r.depth), "</blockquote>") ++
        join([for (line in split(r.text, "\n")) escaped(line)], "<br>\n")], "") ++ repeated(depths[len(runs)], "</blockquote>");
    "<!DOCTYPE html><html><body><div style=\"font-family:-apple-system,Helvetica,Arial,sans-serif;font-size:14px;line-height:1.5;color:#1d1d1f\">" ++
        body ++ "</div></body></html>"
}

// ---- replies and forwards ----

fn unique_people(people, except) => [for (i, a in people where not (lower(a.address) in except) and
    len([for (j, b in people where j < i and lower(b.address) == lower(a.address)) b]) == 0) a]

pub fn attribution(m) => "On " ++ (m.date or "an earlier date") ++ ", " ++ people_label(m.from) ++ " wrote:"

// A reply to `m` from `me` ({name, address}); `all` adds the other recipients.
// A reply to one's own message goes to its recipients instead.
pub fn reply(m, me, all) {
    let mine = [lower(me.address)]
    let senders = if (len(m.reply_to or []) > 0) m.reply_to else m.from
    let own = len(senders) > 0 and len(unique_people(senders, mine)) == 0
    let to = unique_people(if (own) m.to else if (all) [*senders, *m.to] else senders, if (all or own) mine else [])
    let cc = if (all) unique_people(m.cc or [], [*mine, *[for (a in to) lower(a.address)]]) else [];
    {*: blank(), to: people_label(to), cc: people_label(cc), subject: reply_subject(m.subject),
     text: "\n\n" ++ attribution(m) ++ "\n" ++ quote(m.text),
     in_reply_to: m.message_id, references: trim(join([m.references or "", m.message_id or ""], " "))}
}

// A forward of `m`, its attachments included.
pub fn forward(m) => {*: blank(), subject: forward_subject(m.subject), attachments: m.attachments or [],
    text: "\n\n---------- Forwarded message ----------\nFrom: " ++ people_label(m.from) ++ "\nDate: " ++ (m.date or "") ++
        "\nSubject: " ++ (m.subject or "") ++ "\nTo: " ++ people_label(m.to) ++ "\n\n" ++ (m.text or "")}

// ---- sending ----

// "Sat, 24 Oct 2026 02:53:07 +0000" for a Unix time in milliseconds, in UTC
pub fn rfc5322_date(unix_ms) => datetime(unix_ms).format("ddd, D MMM YYYY hh:mm:ss") ++ " +0000"

// The addresses typed in a To or Cc field, as [{name, address}]; the eml
// reader's address parser reads them, quoting and encoded words included.
pub fn recipients(typed) {
    let text = trim(typed or "")
    let parsed = if (text == "") null else (parse(binary("To: " ++ text ++ "\r\n\r\n"), 'eml') ^ { null });
    if (parsed == null or parsed.addresses.to == null) [] else parsed.addresses.to
}

// why the draft cannot be sent yet, or null
pub fn problem(draft) {
    let people = [*recipients(draft.to), *recipients(draft.cc)]
    let bad = [for (a in people where not contains(a.address or "", "@")) a];
    if (len(people) == 0) "Add at least one recipient."
    else if (len(bad) > 0) "“" ++ (bad[0].address or bad[0].name) ++ "” is not an email address."
    else null
}

// lines a mail server accepts as 8bit are at most 998 octets (RFC 5322 §2.1.1)
pub fn transfer_encoding(text) => if (len([for (line in split(text, "\n") where len(binary(line)) > 900) line]) > 0) "quoted-printable" else "8bit"

// The RFC 5322 text of `draft` sent by `from` ({name, address}) at `sent_at`
// (an RFC 5322 date) as `id` (a Message-ID): multipart/alternative of the
// plain text and its HTML, then any attachments.
pub fn eml(draft, from, sent_at, id) string^ {
    let text = replace(draft.text or "", "\r\n", "\n")
    let html = html_of_text(text)
    let to = recipients(draft.to)
    let cc = recipients(draft.cc)
    let threading = if (draft.in_reply_to == null) []
        else [{name: "in-reply-to", value: draft.in_reply_to}, {name: "references", value: draft.references or draft.in_reply_to}];
    misc.format({headers: threading, from: from, subject: trim(draft.subject or ""), date: sent_at, message_id: id,
        *: (if (len(to) > 0) {to: to} else {}), *: (if (len(cc) > 0) {cc: cc} else {}),
        body: {type: "multipart/alternative", parts: [
            {content: text, type: "text/plain; charset=UTF-8", encoding: transfer_encoding(text)},
            {content: html, type: "text/html; charset=UTF-8", encoding: transfer_encoding(html)}]},
        attachments: [for (a in draft.attachments or []) {content: a.content, filename: a.filename or "attachment",
            type: a.type or "application/octet-stream"}]}, {type: "eml"})^
}
