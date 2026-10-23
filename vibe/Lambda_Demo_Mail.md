# Mail Demo — Proposal

**Status:** Proposal, 2026-10-22. Nothing is implemented. The demo needs a
libcurl rebuild and some new IO surface: mail sessions, a loopback listener for
OAuth, a secret store and a few crypto primitives. The decisions marked
*proposed* in §5.2 need the user's ruling before M1. This document introduces
no language rulings. Revised the same day: the user ruled that no script runs
in mail content until Lambda implements sandboxing (ML5v2); POP3 joins the
scope (ML9); §2.6 answers whether to fork curl's protocol code. Second
revision, same day: the user ruled **ML1** (curl unmodified plus a new IMAP
engine) and directed that the native mail API be exposed, minimal, as
`lambda.io.mail` (§5.3, ML10). Third revision, same day: the user ruled that
storage stays on what Lambda has today, with raw message files plus a Mark
index per mailbox (ML7v3), and that search uses Lambda's own `io.text_search`
and `io.grep` (ML12). The known limitations of that store are listed as
SL1–SL5 for later phases (§5.4.4). **Implementation started 2026-10-22:**
M0 (transport) and M1a (MIME decoding, the `lambda.mail` package and the store) are
delivered; see the [M0](#m0-checkpoint--2026-10-22) and [M1a](#m1a-checkpoint--2026-10-22)
checkpoints.

**Scope:** a new demo under `test/demo/mail/`, registered as `lambda demo
mail`. It is a desktop mail client in the manner of macOS Mail that connects to
Gmail, Outlook.com / Microsoft 365, Proton Mail (through Proton Mail Bridge),
iCloud, Fastmail and standard IMAP, POP3 and SMTP hosts. The scope also covers
the native `lambda.io.mail` module, a `lambda.mail` package, a reusable
`lambda.oauth` package, the mail store, and the libcurl configure flags in the
setup scripts.
§2 answers the question that started this proposal: is the integrated curl
sufficient for these protocols?

**Specification linkage:** [Formal Semantics](../doc/Lambda_Formal_Semantics.md)
S1.4 (values never alias; session state is explicit), S12.1.1v3 and S12.1.3
(pure `fn` templates, effects only in `pn` handlers), S12.4.1v3 and S12.4.2
(`open()` is `pn`-only resource acquisition that auto-closes at block end),
S13.1.2v2 (calls may park invisibly), S13.2.2 (`send` never blocks), S13.3.1v2
(document-owned tasks), S13.3.2 (cancellation at park points).
[Formal Design](../doc/Lambda_Formal_Design.md) D7.1.2v2 (all resource IO lives
in `lambda-io`; raw context-free mechanisms stay in `lib`), D7.5.2 (one central
IO door that carries policy), D7.2.4v4 (shipped `lambda.*` packages and built-in `lambda.io.<leaf>` modules), D7.2.5
(editing policy lives in the DOM behaviour package), D7.5.3v2 (the
Lambda↔Radiant boundary), D4.5.1v4 and D4.5.2 (native ownership; Radiant never
retains GC pointers), D5.3.3 (precise rooting in native helpers). Where no
`S#`/`D#` ruling exists, this document cites vibe IDs: RDB11 and JA16 in
[Lambda_IO_RDB.md](Lambda_IO_RDB.md), AR-E4 and AR-O13 in
[Lambda_Pkg_Archive.md](Lambda_Pkg_Archive.md), K30g in
[Lambda_Design_Concurrency.md](Lambda_Design_Concurrency.md).

**Related records:** [Finder demo](Lambda_Demo_Finder.md) (viewport-windowed
rows, preview panel, Quick Look), [Photo album demo](Lambda_Demo_Photo_Album.md)
(proposal shape, shared `test/demo/mod_publish.ls`),
[archive package](Lambda_Pkg_Archive.md) (needs the same document-owned tasks
and task-to-view messages), [readability package](Lambda_Pkg_Readability.md)
(HTML cleaning that the mail sanitizer reuses), the `.eml` reader
`lambda/input/input-eml.cpp`, and the RFC 5322 writer `doc.misc.eml()` in
`lmd/package/doc/misc/mail.ls` ([Lambda_Packages.md](../doc/Lambda_Packages.md) §18).

---

## 1. Purpose and product direction

Mail is the most common desktop application that combines live network
protocols, untrusted rich documents, large lists and background
synchronisation. Lambda can host all of these well:

- **Data processing.** A mailbox is a stream of semi-structured documents.
  MIME trees map naturally onto Mark elements. Envelopes, threads and
  search results are ordinary Lambda values.
- **Document rendering.** Radiant renders HTML mail. Plain text, PDF,
  image and archive attachments reuse Finder's previews.
- **Concurrency.** Each account syncs in its own task. The window stays
  responsive while sessions park on the network.

The product is a three-pane window in the manner of macOS Mail: mailboxes on the
left, a message list in the middle, and the reading pane on the right, with a
separate compose window. The demo should feel complete for everyday reading,
triage and replies against real accounts. It is not a product: it does not aim
for every edge case of every server.

**Non-goals.** Running scripts from mail content (ML5v2); Exchange EWS and
ActiveSync; Proton's native API (SRP login and OpenPGP; Bridge covers Proton);
rules engines; calendars and contacts beyond reading `.ics`/`.vcf`
attachments; S/MIME and OpenPGP signing or decryption (a later stretch could
display them).

## 2. Is the integrated curl sufficient?

### 2.1 Short answer

**No, not as built.** The mail protocols are compiled out of the libcurl that
Lambda links on macOS and Windows. On every platform, Lambda's script surface
can reach only HTTP and local files.

**Rebuilt with IMAP, POP3 and SMTP enabled, curl is enough for the transport
layer:** TLS and STARTTLS, certificate trust, SASL authentication including the
OAuth mechanisms that Gmail and Microsoft require, all of SMTP submission, and
all of POP3 retrieval.

**It is not enough as the IMAP client for a Mail-class application.** libcurl's
IMAP support runs one command per transfer. It has no IDLE push, no pipelining
and no literal-aware parsing of custom command responses. The proposal (ML1)
therefore uses curl to connect and authenticate, then hands the encrypted
connection to a small native IMAP session engine (§2.5).

### 2.2 What is linked today

Checked on this tree on 2026-10-22.

| Platform | libcurl | TLS | Protocols compiled in | Mail protocols |
|---|---|---|---|---|
| macOS | static `mac-deps/curl-8.10.1/lib/libcurl.a`, built by `setup-mac-deps.sh` | mbedTLS 3, with roots from the host trust store (`lambda/network/curl_trust.h`) | file, ftp, ftps, http, https (`curl-config --protocols`; the `Curl_handler_*` symbols in `lambda.exe` agree) | **none.** Configure passes `--disable-imap --disable-pop3 --disable-smtp`, and also `--disable-mime`. |
| Windows | static `win-native-deps/lib/libcurl.a`, built by `setup-windows-deps.sh` | Schannel | minimal HTTP(S) build | **none.** The same three `--disable-*` flags. |
| Linux | system `-lcurl` (dynamic) | the distribution's (usually OpenSSL) | the distribution default | Usually present in distribution packages, but unchecked. Lambda never asks for them. |

The script surface is HTTP-only on every platform. `io.fetch` is documented for
HTTP requests and local files (`doc/Lambda_Sys_Func.md`, `io.fetch`), and no
other function opens a network connection. Lambda's path schemes are a fixed
set (`http`, `https`, `sys`, `temp` and file paths;
[Lambda_Data.md](../doc/Lambda_Data.md) "Path Schemes"). There is no `imaps.`
root.

### 2.3 What libcurl provides once IMAP and SMTP are enabled

- **Connection.** DNS, IPv4/IPv6, timeouts, connection reuse, and
  cancellation through the existing multi backend
  (`lambda/network/curl_multi_backend.cpp`). Transfers park the calling
  task, as `io.fetch` does.
- **TLS.** Implicit TLS (`imaps://` on 993, `smtps://` on 465) and STARTTLS.
  With `CURLOPT_USE_SSL = CURLUSESSL_ALL`, curl fails rather than continue
  in clear text when the upgrade is refused. Certificates are verified
  against the host trust store through the same `CURLOPT_SSL_CTX_FUNCTION`
  hook HTTP uses. `CURLOPT_PINNEDPUBLICKEY`, which the mbedTLS backend
  supports, can trust the self-signed certificate of a local server such as
  Proton Mail Bridge.
- **Authentication.** curl negotiates SASL from the server's capabilities:
  PLAIN, LOGIN, CRAM-MD5 and DIGEST-MD5. With `CURLOPT_XOAUTH2_BEARER` it also
  negotiates OAUTHBEARER (RFC 7628) and XOAUTH2, the OAuth mechanisms Gmail
  and Microsoft use. Our flags already leave bearer and digest
  authentication enabled. The disabled NTLM and the absent GSSAPI do not
  matter for the target providers.
- **SMTP submission, complete.** `CURLOPT_MAIL_FROM`, `CURLOPT_MAIL_RCPT` (any
  number of recipients), the message streamed through a read callback,
  end-of-data dot-stuffing done by curl, and `CURLOPT_MAIL_RCPT_ALLOWFAILS`
  for partial recipient failure. Nothing more is needed to send mail.
- **POP3 retrieval, complete.** `pop3s://` on 995 or STARTTLS (STLS) on 110,
  the same SASL mechanisms, LIST and RETR through URLs
  (`pop3s://host/` lists, `pop3s://host/7` retrieves), and UIDL, TOP, STAT
  and DELE through `CURLOPT_CUSTOMREQUEST` (with `CURLOPT_NOBODY` for the
  single-line replies). curl handles the multi-line terminator and
  dot-unstuffing. POP3 has no push, and a download-only client does not need
  its PIPELINING extension (RFC 2449), so the transfer API covers what the
  demo uses.
- **IMAP through URLs.** LIST (`imaps://host/`), EXAMINE/SELECT (`/INBOX`), one
  section of one message by UID (`/INBOX;UID=42;SECTION=TEXT`), SEARCH
  (`/INBOX?UNSEEN`), and APPEND by upload. Any other command goes through
  `CURLOPT_CUSTOMREQUEST`.

### 2.4 Where libcurl's IMAP stops

These findings come from the curl 8.10.1 sources in
`mac-deps/curl-8.10.1/`: `lib/imap.c`, `lib/pingpong.c`, `lib/multi.c` and
`docs/KNOWN_BUGS`.

1. **One command per transfer; no push.** Every `curl_easy_perform` sends one
   command and reads until its tagged completion. There is no IDLE (RFC 2177)
   and no NOTIFY, and unsolicited `EXISTS`/`EXPUNGE`/`FETCH` responses outside
   a running command are never delivered. Users of Mail expect new messages to
   appear on their own. Through the transfer API, polling is the only option.
2. **Custom-command responses are not literal-aware.** curl frames server
   output by line (`Curl_pp_readresp`). For a custom request it forwards every
   line to the header callback. It forwards to the body callback only the
   untagged lines whose keyword matches the command or a fixed list (`UID`,
   `SEARCH`, `EXPUNGE`, …; `imap_endofresp`,
   `imap_state_listsearch_resp`). IMAP sends header blocks, body sections, and
   every multi-line or 8-bit string inside ENVELOPE and BODYSTRUCTURE as a
   literal `{n}`, and curl passes those bytes on as ordinary lines. The client
   therefore has to re-frame the stream itself. Worse, a line inside a literal
   that begins with the command's tag ends the command early. Tags are
   predictable (`"%c%03d"`, for example `A004`). Message content is controlled
   by whoever sent the mail, so a crafted message could desynchronise a batch
   header or body fetch made through a custom request, or forge its response.
   Only the URL form `;UID=n;SECTION=…` reads a literal by its byte count, and
   it fetches one section of one message per round trip.
3. **No pipelining.** Each command waits for the previous one to complete.
   Syncing a large mailbox through URL fetches costs one round trip per
   message.
4. **A large-mailbox truncation bug is on record.** curl's own
   `docs/KNOWN_BUGS` §3.1 lists "IMAP SEARCH ALL truncated response". Whether
   it still applies to 8.10.1's dynamic buffers has not been verified.
5. **The caller's jobs.** MIME decoding of received mail, and mailbox names in
   modified UTF-7 (RFC 3501 §5.1.3), are left to the caller. These are not
   curl defects: every transport library leaves them to the client.

### 2.5 Verdict and the proposed split

curl does connection, TLS and authentication well. Its IMAP command layer
is the weak part, so stop curl before it:

- `CURLOPT_CONNECT_ONLY = 1` returns after the protocol-connect phase: the
  greeting, CAPABILITY, STARTTLS and SASL AUTHENTICATE. It returns before any
  DO-phase command (`lib/multi.c`, the `MSTATE_DO` branch). The connection
  stays open, encrypted and authenticated.
- A native IMAP session engine then drives the connection with
  `curl_easy_send`/`curl_easy_recv`. It handles tags, literal-aware framing
  and IDLE (Appendix B).
- SMTP and POP3 stay plain curl transfers.

**Alternative kept in reserve.** A host TLS socket built from `lib/trust_store`
and mbedTLS, in the shape of `lib/rdb_tunnel.c` (RDB11 "route B"), which
upgrades PostgreSQL and MySQL connections in-protocol. It is self-contained,
but it re-implements DNS, connect, timeouts, STARTTLS and SASL negotiation that
curl already has. Use it only if MG1 fails.

**Rejected.**
- curl URLs and custom requests alone: the framing hazard in 2.4 item 2, no
  push, no pipelining.
- JMAP or the Gmail API as the primary protocol: each serves one provider
  family. They stay possible later as optional adapters, since both are
  JSON over HTTPS that `io.fetch` already reaches.

The curl rebuild itself is in Appendix A. It changes configure flags in our
setup scripts. It does not patch vendored sources (AGENTS rule 16), but it is
still a dependency change for the user to approve.

### 2.6 Fork curl's protocol code, or write the IMAP layer from scratch?

Asked by the user on 2026-10-22. **Recommendation: neither a fork nor a full
rewrite.** Use libcurl unmodified for everything it does well, and write new
code only for the part curl's design does not have.

| Protocol | What curl's code covers | Plan |
|---|---|---|
| SMTP | All of submission (§2.3) | libcurl as a library, unmodified |
| POP3 | All of retrieval (§2.3) | libcurl as a library, unmodified |
| IMAP connect and login | Greeting, CAPABILITY, STARTTLS, LOGIN, SASL | libcurl unmodified, stopped by `CONNECT_ONLY` (ML1) |
| IMAP after login | One command at a time, line framing, no IDLE | New native session engine (Appendix B) |

**Why not fork curl's `imap.c`.** Its 2 115 lines (curl 8.10.1) divide roughly
as follows:

| Part | Lines | Fate under a fork |
|---|---|---|
| Connect and login: greeting, CAPABILITY, STARTTLS, LOGIN, AUTHENTICATE | ~400 | Kept, but ML1 already gets it unmodified |
| Command layer: LIST, SELECT, FETCH, APPEND, SEARCH, LOGOUT and their response handlers | ~465 | Rewritten: it is the one-command, literal-unaware model of §2.4 |
| State machine and libcurl plumbing (multi, do/done, connection setup) | ~475 | Rewritten or re-plumbed: it assumes one command in flight |
| IMAP URL parsing (RFC 5092) | ~260 | Dropped: the engine is not URL-driven |
| Tags, atoms, matching | ~200 | Mostly rewritten |

The code worth keeping is exactly the code ML1 uses without modification. The
code a fork would modify is the core of curl's design. A single in-flight tag,
line framing in `pingpong.c` (shared with FTP, POP3 and SMTP), and the
transfer-shaped easy API have no place for pipelining, literals or an IDLE
that the application ends. `imap.c` also calls 41 internal libcurl functions
(`Curl_pp_*`, `Curl_sasl_*`, `Curl_dyn_*`, the client writers and readers, the
connection filters). A fork therefore takes one of two forms, and both are
worse than new code:

- **Patched inside libcurl.** This is a vendor patch under AGENTS rule 16. It
  needs approval and a `patches/` record, it must be re-merged on every curl
  upgrade, and it changes `pingpong.c` under protocols we do not own. IDLE
  would also need new public curl options to end it.
- **Extracted into Lambda.** All 41 internal calls must be replaced. That is
  a rewrite of the plumbing, carrying code shaped for the wrong model.

**Security favours keeping the pre-login code upstream.** The hard,
security-relevant part of these protocols is the plain-text phase before TLS
and authentication. That is where curl has fixed real defects, for example
CVE-2021-22946 (a required TLS upgrade silently skipped for IMAP, POP3 and FTP)
and CVE-2021-22947 (responses cached before STARTTLS treated as trusted after
it). With ML1 that code stays curl's, and we get future fixes by upgrading. A
fork would freeze it.

**The new code is small and is the part Lambda must own anyway.** Framing and
tokenizing are about 500 lines. Tags, literal sending, IDLE, socket waiting,
cancellation and limits are about 1 000 more. The engine keeps one command in
flight per session and needs no pipelining: a UID set
(`UID FETCH 1:* (…)`, `UID FETCH 4,9,31 (BODY.PEEK[])`) batches the work of
many messages into one command. The bridge to
Lambda values adds a few hundred. Interpreting ENVELOPE and BODYSTRUCTURE is
Lambda code in `lambda.mail.imap` (ML2). That is roughly 1 500–2 000 lines of
native code, each piece written for the asynchronous tagged model IMAP actually
has, and testable offline against recorded transcripts (§8).

**Other import candidates.** A complete mail library such as libetpan (BSD
licensed: IMAP with IDLE and CONDSTORE/QRESYNC, POP3, SMTP and MIME) was
considered. It brings its own socket and TLS layers (OpenSSL or GnuTLS, not
mbedTLS with the host trust store) and a second MIME parser beside
`input-eml.cpp`. It would be vendored code under rule 16, from a project with
infrequent releases. It solves the same IMAP problem at a much larger cost.

## 3. Providers and authentication

Basic username/password IMAP is disappearing at the large providers. OAuth 2.0
support is therefore a core feature, not an extra.

| Provider | IMAP | SMTP | Authentication | Notes |
|---|---|---|---|---|
| **Gmail** | `imap.gmail.com:993` TLS | `smtp.gmail.com:465` TLS or `:587` STARTTLS | OAuth 2.0 with XOAUTH2. An app password works when 2-Step Verification is on and an admin has not disabled it. | Scope `https://mail.google.com/` is restricted. A client in Google's "Testing" status gets 7-day refresh tokens and at most 100 test users. A public release needs Google's verification. Labels appear as mailboxes. `X-GM-EXT-1` adds `X-GM-LABELS`, `X-GM-MSGID`, `X-GM-THRID` and `X-GM-RAW`. Gmail saves sent mail itself. Gmail also caps simultaneous IMAP connections per account. |
| **Outlook.com / Microsoft 365** | `outlook.office365.com:993` TLS | `smtp-mail.outlook.com:587` (personal) or `smtp.office365.com:587` STARTTLS | OAuth 2.0 only. Basic authentication and app passwords stopped working for personal Outlook.com accounts on 2024-09-16. Exchange Online is retiring basic SMTP AUTH on a date Microsoft keeps moving. | Needs an app registration with `IMAP.AccessAsUser.All`, `SMTP.Send` and `offline_access`. |
| **Proton Mail** | through Proton Mail Bridge, by default `127.0.0.1:1143` | Bridge, by default `127.0.0.1:1025` | A password generated by Bridge, over PLAIN/LOGIN | Bridge requires a paid plan. It offers STARTTLS or SSL with a self-signed certificate, so the account stores a certificate pin. Ports are configurable in Bridge. |
| **iCloud** | `imap.mail.me.com:993` TLS | `smtp.mail.me.com:587` STARTTLS | App-specific password | |
| **Fastmail** | `imap.fastmail.com:993` TLS | `smtp.fastmail.com:465` TLS | App password | Also offers JMAP; see MQ6. |
| **Yahoo** | `imap.mail.yahoo.com:993` TLS | `smtp.mail.yahoo.com:465` TLS | App password | |
| **Other IMAP hosts** (Dovecot, Cyrus, …) | 993 TLS or 143 STARTTLS | 465 TLS or 587 STARTTLS | PLAIN/LOGIN or SCRAM | Settings come from the domain's autoconfig (§4.1) or are entered by hand. |

**POP3:** Gmail `pop.gmail.com:995`, Outlook.com and Microsoft 365
`outlook.office365.com:995` (OAuth only), Yahoo `pop.mail.yahoo.com:995`,
Fastmail `pop.fastmail.com:995`, all over implicit TLS with the same
authentication as their IMAP. iCloud and Proton Mail Bridge offer no POP3.

Provider settings change. The preset table ships as data in
`lambda.mail.providers` (§5.6), not as code.

## 4. Features

Priorities: **P1** belongs to the first usable client, **P2** completes the
everyday experience, and **S** is a stretch goal.

### 4.1 Accounts and setup

- **P1** Add an account by email address. A known domain selects its preset.
  An unknown domain is looked up in Mozilla's ISPDB autoconfig, an XML
  document that `io.fetch` and `input(…, 'xml')` already handle. Manual
  settings remain available.
- **P1** Password and app-password accounts, including Proton Mail Bridge with
  its certificate pin. Secrets are stored in the macOS Keychain (ML6).
- **P2** OAuth sign-in for Gmail and Microsoft. The demo opens the system
  browser on the provider's consent page and receives the code on a loopback
  redirect. It uses PKCE (RFC 7636, RFC 8252) and refreshes tokens silently.
- **P2** POP3 accounts (ML9). POP3 offers only the Inbox, with no flags and
  no folders. The demo downloads new messages by UIDL into a local Inbox,
  keeps read state, flags and moves locally, and leaves messages on the
  server unless the user chooses otherwise (MQ10).
- **P2** Account status in the sidebar: offline, authentication failed,
  syncing.

### 4.2 Mailbox sidebar

- **P1** Favourites, a unified Inbox across accounts, and each account's
  mailbox tree with unread counts (`STATUS`).
- **P1** Special-use mailboxes (RFC 6154: `\Sent`, `\Drafts`, `\Trash`,
  `\Junk`, `\Archive`, `\All`, `\Flagged`) get their own icons and order.
  Unflagged servers fall back to name heuristics.
- **P2** Smart mailboxes: saved queries over the local index ("Unread
  today", "From VIPs with attachments").
- **P2** Drop messages on a mailbox to move them, or with ⌥ to copy them.

### 4.3 Message list

- **P1** A row per message: unread dot, sender, subject, date, a two-line
  snippet, flag, attachment clip. Classic columns are an alternative layout.
- **P1** Mailboxes with tens of thousands of messages. Rows use Finder's
  retained, viewport-windowed publication; only visible rows are mounted
  ([Finder §3](Lambda_Demo_Finder.md)).
- **P2** Conversation threading: `X-GM-THRID` on Gmail, otherwise
  `References`/`In-Reply-To` threading in the JWZ style.
- **P2** Sorting, filtering (unread, flagged, with attachments) and
  multi-selection with shift and ⌘.

### 4.4 Message viewer

- **P1** A header block (from, to, cc, date, subject) with expandable
  details.
- **P1** Plain-text bodies with quote levels and `format=flowed` (RFC 3676).
- **P1** HTML bodies rendered as untrusted documents. No script in a message
  ever runs (ML5v2). Remote resources are blocked behind a "Load Remote
  Content" bar. Inline `cid:` images come from the message itself.
- **P1** An attachment strip. Quick Look reuses Finder's previews for PDF,
  images, text and ZIP. Attachments are saved only when the user asks.
- **P2** Show the raw source, and "View → Message → All Headers".

### 4.5 Actions

- **P1** Read/unread, flag, delete (to Trash), archive, move, junk.
- **P2** Undo for every action. Actions apply optimistically in the model and
  are reconciled with the server, queued while offline and replayed on
  reconnect.
- **P2** Server-side moves use `MOVE` (RFC 6851) when advertised. Otherwise
  `COPY`, then `STORE \Deleted`, then `UID EXPUNGE` (UIDPLUS).

### 4.6 Compose and send

- **P1** New message, reply, reply-all and forward with correct quoting,
  `In-Reply-To`/`References`, and `Re:`/`Fwd:` handling.
- **P1** Plain-text composition, sent through SMTP. The message is built as
  RFC 5322 with MIME and RFC 2047 headers by `doc.misc.eml()`.
- **P2** Rich-text composition through the shipped DOM editing package
  (D7.2.5), sent as `multipart/alternative`. Attachments by drag and drop,
  signatures per account.
- **P2** Drafts saved with `APPEND` to `\Drafts`. Sent copies appended to
  `\Sent`, except on Gmail. An outbox for offline sending.

### 4.7 Search

- **P2** Local search over cached messages with Lambda's own search (ML12):
  `io.text_search` for ranked free text (BM25, phrases, prefixes, snippets),
  `io.grep` for exact text and string patterns, both over plain-text sidecars
  (§5.4.3).
- **P2** Server search for messages that are not cached: IMAP `SEARCH`, and
  `X-GM-RAW` for Gmail's own query syntax.

### 4.8 Sync, offline use and notifications

- **P1** An offline cache. A mailbox opens instantly from disk and then
  reconciles.
- **P2** Push for the selected Inbox through IDLE. Other mailboxes are polled
  with `STATUS`. Flag changes are resynchronised with CONDSTORE/QRESYNC
  (RFC 7162) where available.
- **P2** A dock badge with the unread count of the unified Inbox.
- **S** System notifications for new mail.

## 5. Architecture

### 5.1 Layers

```
test/demo/mail/          window: pure fn templates, pn handlers (S12.1.1v3, S12.1.3)
  lambda.mail            accounts, sync policy, IMAP response meaning, MIME,
                         threading, quoting, HTML sanitizer          (Lambda, mostly fn)
  lambda.oauth           PKCE, consent URL, loopback code, refresh   (Lambda, pn)
  lambda.io.mail         open, command, idle, send (§5.3)            (rt, D7.1.2v2, D7.5.2)
  lib                    curl connect/TLS/SASL; IMAP framing and
                         tokenizer (context-free)                    (C, D7.1.2v2)
  mail store             raw .eml files + Mark index per mailbox     (files, §5.4)
```

Policy, data processing and presentation live in Lambda. Native code keeps
only what has to be native: sockets, TLS, and framing an untrusted byte stream.

### 5.2 Decisions

| ID | Decision | Reasoning | Status |
|---|---|---|---|
| **ML1** | **curl connects and authenticates; a native engine speaks IMAP.** `CURLOPT_CONNECT_ONLY` hands an authenticated, encrypted connection to the IMAP session engine. SMTP and POP3 are plain curl transfers. curl's sources are not forked. | §2.4–§2.6. Reuses curl's TLS, trust store, STARTTLS and SASL, including XOAUTH2, and keeps their security fixes upstream. Avoids curl's command layer, which has no push, no pipelining and unsafe framing. | **ruled** (user, 2026-10-22) |
| **ML2** | **Framing is native; meaning is Lambda.** The `lib` tokenizer turns each complete server response into a Lambda value: atoms, numbers, quoted strings, literals, `NIL` and nested lists. Literals above a size threshold go to a temporary file. `lambda.mail` interprets ENVELOPE, BODYSTRUCTURE, FLAGS, LIST, STATUS and the extensions. | Framing is security-critical and length-prefixed, so it does not fit the grammar module. Interpretation is data processing, Lambda's strength, and the place where Gmail extensions, CONDSTORE and PREVIEW are added. | **ruled** (user, 2026-10-22); implemented (M0, M1a) |
| **ML3** | **A session is a scoped resource.** It is opened by `mail.open` in a `pn` (S12.4.1v3) and auto-closed at the end of its block (S12.4.2). There is no close function (S12.4.3). The host registers it as a long-lived connection (JA16). A long-lived session lives in one document-owned sync task per account (S13.3.1v2). | One cleanup mechanism; no session can leak past its owner. The task's lifetime is the account's online lifetime. | **ruled** (user, 2026-10-22), with the interim (M0): the session closes when its opening task ends until S12.4.2 is implemented |
| **ML4** | **Commands park; nothing blocks.** Running a command, idling and sending park the calling task as `io.fetch` does (S13.1.2v2). They are cancellable at park points (S13.3.2), and IDLE ends with `DONE` on cancel. | The window and other accounts keep running. Quitting cancels every sync task cleanly. | **ruled** (user, 2026-10-22); implemented (M0) |
| **ML5v2** | **No script in mail content runs.** Script support for mail waits until Lambda implements sandboxing. Each message renders in an `<iframe srcdoc>` whose document Radiant loads with scripting disabled: no `<script>`, no event-handler attributes, no `javascript:` URLs, and `<noscript>` content rendered. The flag is inherited by every nested document. Remote loads are blocked by a per-document resource policy until the user allows them for a message or a sender. A Lambda sanitizer strips the same constructs as a second layer. | Mail content is controlled by its sender, and remote images are trackers. A sanitizer alone is one missed vector (SVG `<script>`, a nested `srcdoc`, an `on*` attribute) away from running code, so the engine enforces the rule (MG4). The demo's own window chrome is local and trusted, so the flag is per document, not global. | **ruled** (user, 2026-10-22); scripting mechanism ruled (MQ11) and implemented (M1c). Remote loads are blocked by the sanitizer only; the engine-level policy is MQ14 |
| **ML6** | **Secrets live only in the OS secret store.** Passwords and refresh tokens are kept in the Keychain (macOS first), never in cache files, logs or `log.txt`. Access tokens stay in memory. | A mail password is the key to every other account the user has. | **ruled** (user, 2026-10-22): the store is the built-in module `lambda.io.secret` with `get`, `set` and `delete`, backed by the Keychain on macOS (MG6) |
| **ML7v3** | **Raw messages are files; each mailbox has a Mark index.** Each downloaded message is stored once as an immutable `.eml` file with a plain-text sidecar. Each mailbox has one index file in Mark notation, saved whole and atomically. Small per-account files hold the mailbox list and the pending-action queue (§5.4). | Uses only what Lambda has today: `input()`, `output(…, {atomic: true})` and `io.move`. It needs no new runtime feature before M1. Its costs, SL1–SL5, are accepted and left to later phases (§5.4.4). Supersedes ML7v2 (Appendix S). | **ruled** (user, 2026-10-22) |
| **ML8** | **Mail access is a capability.** Opening a mail session or reading a secret is a `pn` effect through the IO door (D7.5.2). It is allowed only to documents whose top-level source is a local file the user opened, never to network-loaded documents. | The same origin gate that D7.5.4 applies to audio. Mail credentials must never be reachable by a web page. | proposed (needs a D-ruling) |
| **ML9** | **POP3 is a download source.** A POP3 account is read with curl transfers (UIDL, RETR, TOP, DELE) into a local Inbox. Read state, flags and folders for it live only in the local cache. | POP3 has no server-side state beyond the message list. curl covers the protocol completely (§2.3), so POP3 costs only a sync policy in `mod_sync.ls`. | **ruled** (user, 2026-10-22); implemented (ML9 checkpoint): `lambda.mail.pop3` and POP3 accounts in the demo. No server deletion until MQ10 is ruled |
| **ML10** | **`lambda.io.mail` is four functions.** `open`, `command`, `idle` and `send` (§5.3). The module speaks protocols only: it has no mail model, no store, no MIME and no secrets. Everything above the wire is Lambda code. | Minimal surface, as the user directed. Commands pass through as text plus safely encoded arguments, so new IMAP extensions need no native change. | **ruled** (user, 2026-10-22): name and scope by user direction, signatures as in §5.3; implemented (M0) |
| **ML11** | **`lambda.io.mail` is a built-in runtime module, not a Jube module.** Its rows live in the runtime beside `io.fetch` and park the calling task the same way. The import path `lambda.io.mail` resolves to them. | A Jube module may not park a task beneath its frame (D7.4.2v2), and every mail call waits on the network. Event-loop-coupled language IO stays in the runtime (D7.1.2v2). D7.2.4v4 (ratified from MQ12) allows a built-in runtime module at `lambda.io.<leaf>`. | **ruled** (user, 2026-10-22; D7.2.4v4) |
| **ML12** | **Search is Lambda's own.** `io.text_search` for ranked free text, `io.grep` for exact text and string patterns, both over the plain-text sidecars. Header fields are filtered in memory from the loaded index. The server searches mail that is not cached. No database full-text index. | It reuses `lib/fts` and the grep engine rather than adding a second search engine. The cost of rescanning is SL4. | **ruled** (user, 2026-10-22) |

### 5.3 The `lambda.io.mail` API

The module carries bytes between Lambda and a mail server, and nothing else
(ML10). It has four functions, all `pn` (S12.1.1v3). ML8's origin gate is
not implemented yet: any document may call them today.

```lambda
import mail: lambda.io.mail

pn check_inbox(account) {
    let s = mail.open("imaps://imap.gmail.com",
                      {user: account.user, token: account.token})^
    mail.command(s, "SELECT", ["INBOX"])^
    let r = mail.command(s, "UID FETCH 1:* (UID FLAGS ENVELOPE)")^
    let changes = mail.idle(s, 600)^
    // ... apply r.responses and changes to the store
}   // the session closes when this task ends (ML3)
```

| Function | Returns | Behaviour |
|---|---|---|
| `mail.open(url, options)` | session | Connects, secures, authenticates, and returns once logged in. Schemes: `imaps` and `pop3s` (implicit TLS); `imap` and `pop3` (STARTTLS required). Options: `user`; `password` or `token` (an OAuth access token, sent as OAUTHBEARER or XOAUTH2); `pin` (a `sha256//…` public-key pin for a self-signed loopback server such as Bridge); `timeout` in seconds; `tls: false`, allowed only for a loopback server (local test servers, a Bridge set to plain). |
| `mail.command(session, command, args?, options?)` | `{status, code, text, responses}` | Sends one command and parks until it completes. `command` is the command name and any fixed atoms. Each value in `args` is appended in its protocol encoding (below). `options.to` names a directory: every literal (IMAP) or multi-line payload (POP3) over 64 KiB is streamed into a new file there and returned as its path. |
| `mail.idle(session, seconds)` | array of untagged responses | IMAP only. Enters IDLE and parks until the server reports a change or `seconds` pass (at most 1 740, under the 30-minute server limit). Sends `DONE` and returns what arrived, possibly nothing. Cancelling the task ends the IDLE cleanly. |
| `mail.send(url, options, message)` | array of rejected recipients | Schemes `smtps` (implicit TLS) and `smtp` (STARTTLS required). Options: `user`, `password` or `token`, `pin`, `timeout`, `from`, and `to` (every envelope recipient, including Cc and Bcc). `message` is the RFC 5322 text or bytes, as `doc.misc.eml()` builds it. Raises when no recipient is accepted. |

**Arguments out.** A string becomes a quoted string, or a literal when it holds
CR or LF or is too long to quote. Binary becomes a literal (APPEND sends a
message this way). An array becomes a parenthesised list, a symbol an atom, an
int a number, and `null` becomes `NIL`. Scripts never hand-quote, so a mailbox
name or search term cannot inject protocol text.

**Responses in (IMAP).** Each untagged response is an array of tokens: atoms
become symbols, numbers ints, quoted strings strings, literals binary (or a
path under `options.to`), `NIL` null, and parenthesised lists arrays. For
example, `* 12 FETCH (UID 48213 RFC822.SIZE 48210)` arrives as
`[12, 'FETCH', ['UID', 48213, 'RFC822.SIZE', 48210]]`. In the result,
`status` is `'ok'`, `code` holds the bracketed response code as tokens (for
example `COPYUID` after a move) or null, and `text` the rest of the line.
Untagged responses the server sends unprompted during a command, such as
`EXISTS` or another client's flag change, are included in that command's
`responses`. The caller applies them.

**Responses in (POP3).** `responses` holds the multi-line payload as one
binary (dot-unstuffed), or is empty for a single-line reply. `text` is the
`+OK` line.

**Errors.** `NO` or `BAD` (IMAP) and `-ERR` (POP3) raise an error whose
message holds the server's reply without its tag, for example
`io.mail.command: NO [TRYCREATE] Mailbox doesn't exist`. Error values carry a
code and a message, not fields, so the reply travels as text. Callers handle
it with `^` handlers ([Lambda_Error_Handling.md](../doc/Lambda_Error_Handling.md)).
A refused login raises `login denied` followed by the server's refusal, as in
`login denied: NO [AUTHENTICATIONFAILED] Invalid credentials`, since servers
say there why they refused (an app password required, a blocked sign-in). The
refusal comes from the reply lines curl hands to its header callback, which
hold only what the server sent, so it never echoes a credential.
A lost connection or a timeout raises and marks the session unusable. A
session runs one command at a time; a second task calling into a busy session
gets an error.

**Imports.** `import mail: lambda.io.mail` is the only form. An unaliased
import is refused, because it would put `open` and `send` over the built-in
ones. The fully qualified spelling, `lambda.io.mail.open(…)`, also resolves.

**Lifetime.** Until the S12.4 resource model lands, a session belongs to the
task that opened it and closes when that task ends (ML3). Closing sends `DONE`
when an IDLE is open, and curl's LOGOUT or QUIT runs on a worker thread so the
loop never waits for it.

**Deliberately absent.** No `close` (S12.4.3: auto-close is the only
cleanup), no pipelining (UID sets batch the work, §2.6), no `list` or `fetch`
conveniences (typed helpers live in `lambda.mail.imap`), no MIME, no store,
no secrets (MG6) and no OAuth (MG7). Those are Lambda code or general IO
capabilities, not mail protocol.

### 5.4 Storage

Raw messages are files, and each mailbox has its own index file in Mark
notation (ML7v3, user ruling 2026-10-22). The store uses only what Lambda
already has:
- `input(path, 'mark')` to load an index;
- `output(index, path, {atomic: true})` to save it, which writes a temporary
  file, syncs it to disk, and renames it over the old one;
- `io.move` to place message files;
- `io.text_search` and `io.grep` to search (ML12).

Its known costs are listed in §5.4.4 and left to later phases.

#### 5.4.1 Layout and message identity

```
Lambda Mail/                                   profile directory, mode 0700 (MQ8)
  accounts.mark                                accounts: settings and secret references, never secrets
  <account>/
    mailboxes.mark                             mailbox list: path, role, local id, counts
    pending.mark                               offline action queue, with undo payloads
    index/<mailbox-id>.mark                    one index per mailbox
    messages/<nn>/<server-id>.eml              raw RFC 5322 bytes, written once
    messages/<nn>/<server-id>.txt              plain-text sidecar for search
  incoming/                                    options.to target for fetches in flight
```

`<mailbox-id>` is a short local id assigned in `mailboxes.mark`, because
mailbox names can contain characters that are unsafe in file names. `<nn>` is
the last two characters of the server id, which keeps directories small.

**One file per message, not per mailbox entry.** An index entry names its
message file by the server's message identity:
- `X-GM-MSGID` on Gmail;
- `EMAILID` on servers with OBJECTID (RFC 8474);
- otherwise `<mailbox-id>-<uidvalidity>-<uid>`;
- the UIDL for POP3.

On Gmail and OBJECTID servers, one file therefore serves INBOX, All Mail and
every label that holds the message. A move made by the demo points the target
entry at the same file through the server's `COPYUID` response (UIDPLUS). A
move made by another client costs one re-download on servers without these
identities.

An index file looks like this (Appendix D has the full sketch):

```lambda
<mailbox path: "INBOX", role: 'inbox', uidvalidity: 1700000001,
         uidnext: 48214, highestmodseq: 912377,
    <m uid: 48213, file: "1781c0ffee2a", flags: ['seen'],
       date: t'2026-10-21T09:14:00Z',
       from: {name: "Ada Lovelace", address: "ada@example.org"},
       subject: "Draft review", snippet: "Thanks, two comments on section 3",
       thread: "1781c0ffee", size: 48210, attachments: 2>
>
```

#### 5.4.2 Life of an index

- **Open.** Opening a mailbox loads its index into the window's model. The
  list mounts only the visible rows, using Finder's row window.
- **Sync.** Server changes are applied to the in-memory model by pure
  functions in `lambda.mail`, then the index is saved.
- **Save.** The whole index is written with `output(…, {atomic: true})`. A
  crash leaves either the old index or the new one, never a torn file. Saves
  are coalesced to at most one per mailbox per second, plus one at the end of
  each sync step.
- **UIDVALIDITY change.** When the server's UIDVALIDITY differs from the
  file's, the index is discarded and rebuilt. Message files that other indexes
  still reference stay.
- **Sync order.**
  1. Headers first: ENVELOPE, FLAGS, BODYSTRUCTURE and a snippet fill the
     index.
  2. Bodies next: the open message at once, the rest newest first in the
     background. Each streams through `options.to` into `incoming/` and is
     moved into `messages/`.
  3. The `.txt` sidecar is written beside each body: From, To, Subject and
     the decoded text of the text parts.
- **Actions across files.** An action (flag, move, delete) is applied in four
  steps:
  1. Record it in `pending.mark`.
  2. Send it to the server.
  3. Update the affected indexes.
  4. Remove it from `pending.mark`.

  On start, pending actions are replayed and the affected mailboxes
  resynchronised. For IMAP the server is the source of truth, so a crash
  between two file saves heals on the next sync. POP3 has no such backstop
  (SL2).
- **Cleanup.** A message file is deleted when no index of its account
  references it. The check runs when the demo is idle.

#### 5.4.3 Search

Search uses Lambda's own search functions; there is no database full-text
index (ML12).

| Query | How |
|---|---|
| Sender, recipient, subject, date and flags in the open mailbox | Filter the loaded index in memory; no file is read |
| Free text in bodies, ranked | `io.text_search` over the account's `.txt` sidecars: BM25 ranking, phrases, prefix and suffix wildcards, snippets, accent folding |
| Exact text or a string pattern, such as an address or an order number | `io.grep` over the sidecars, with `files: true` to list the matching messages |
| Messages not downloaded yet | The server: IMAP `SEARCH`, or `X-GM-RAW` on Gmail |

A sidecar's file name is its server id, which leads back to the index entry.
The sidecars exist because the `.eml` files hold base64 and quoted-printable
bodies and HTML, which plain-text search would miss.

#### 5.4.4 Known limitations and future phases

The user accepted these costs for now (2026-10-22); later phases will remove
them. Each one has an ID so later work can cite it.

| ID | Limitation | When it bites | Direction for a later phase |
|---|---|---|---|
| **SL1** | **Whole-file rewrites.** Every change rewrites the mailbox's whole index, roughly 300 bytes per message, so about 15 MB for 50 000 messages. Coalescing limits how often saves happen, not how large they are. | Large mailboxes with frequent flag changes, such as Gmail All Mail | An append-only change log beside each index, compacted when the mailbox opens; or a database index (the ML7v2 design kept in Appendix S) once Lambda can write SQLite |
| **SL2** | **No atomic update across files.** A move touches two indexes and `pending.mark`, and they cannot be committed together. IMAP heals from the server on the next sync (§5.4.2). POP3's local-only flags and folders can be lost or duplicated by a crash during a move. Coalesced saves can also lose up to a second of local-only changes. | POP3 accounts; a crash or kill during a save | A single-file transaction log, or the database index |
| **SL3** | **Everything in memory.** Opening a mailbox loads its whole index. Sorting, filtering, counts and threading run over every summary. The unified Inbox loads every account's INBOX index. Cross-mailbox views (smart mailboxes, all flagged) and file cleanup load every index of an account. Cold-open time and memory grow with mailbox size (MG8 measures them). | Mailboxes of 50 000 messages or more; many accounts | Paged storage with indexed queries |
| **SL4** | **Search rescans.** `io.text_search` and `io.grep` build no persistent index, so every query reads every sidecar, and the cost grows with the amount of cached mail (MG12 measures it). | Large caches; search as you type | A persistent, incrementally updated index in `lib/fts` behind `io.text_search` |
| **SL5** | **Text stored twice.** Every body exists as `.eml` and as a `.txt` sidecar. | Disk use | Drop the sidecars once `lib/fts` extracts text from `.eml` itself (its HTML extraction is already planned) |

### 5.5 Data model

The model is plain Lambda values held in the window's session state (S1.4).

```lambda
// one row of the message list, built from ENVELOPE, FLAGS and the snippet
{uid: 48213, flags: ['seen', 'flagged'], date: t'2026-10-21T09:14:00Z',
 from: {name: "Ada Lovelace", address: "ada@example.org"},
 subject: "Draft review", snippet: "Thanks, two comments on section 3",
 thread: "1781c0ffee", size: 48210, attachments: 2}
```

- **Account:** id, display name, provider preset, server settings, auth kind,
  secret reference (never the secret), certificate pin.
- **Mailbox:** path, delimiter, special-use role, UIDVALIDITY, UIDNEXT,
  HIGHESTMODSEQ, unread and total counts.
- **Message:** the Mark element produced by `input(file, 'eml')`, with
  headers, a part tree and decoded bodies. Parts carry their content type,
  charset, disposition, filename and `cid`.
- **Pending action:** kind, UIDs, source and target mailbox, undo payload.
  Persisted in the account's `pending.mark` before it is sent, so it
  survives a restart (§5.4.2).

The open mailbox's whole index is in memory (SL3). The list mounts only the
visible rows.

### 5.6 Modules

| Module | Kind | Role |
|---|---|---|
| `test/demo/mail/mail.ls` | entry | Window shell, session state view, event handlers |
| `test/demo/mail/mod_ui.ls` | `fn` | Sidebar, list rows, viewer, compose markup |
| `test/demo/mail/mod_model.ls` | `fn` | Selection, sorting, filtering, unified Inbox |
| `test/demo/mail/mod_sync.ls` | `pn` | The per-account sync task: open, select, fetch, idle, apply actions |
| `test/demo/mail/mod_store.ls` | `pn` | Index load and coalesced atomic save, pending queue, message file placement, cleanup (§5.4) |
| `lambda.mail.imap` | `fn` | Command building, response interpretation, modified UTF-7 |
| `lambda.mail.mime` | `fn` | Part tree helpers over `input-eml`, `format=flowed`, `cid:` mapping |
| `lambda.mail.compose` | `fn` | Reply/forward quoting and headers; builds on `doc.misc.eml()` |
| `lambda.mail.thread` | `fn` | JWZ threading; `X-GM-THRID` fast path |
| `lambda.mail.sanitize` | `fn` | HTML mail cleaning; reuses readability's `prep.ls` and `clean.ls` |
| `lambda.mail.providers` | data | Presets and the ISPDB autoconfig lookup |
| `lambda.oauth` | `pn` | Generic OAuth 2.0 for native apps |
| `lambda.io.mail` | native | `open`, `command`, `idle`, `send` (§5.3); the IMAP engine, curl POP3 and SMTP |
| `lib` mail framing | native | Tokenizer and literal spill; context-free |

Package import paths follow D7.2.4v4. The names above are working names.

### 5.7 Reuse

- **From Finder:** the retained row window, the preview panel and Quick Look,
  and ZIP attachments as navigable containers.
- **From `test/demo/mod_publish.ls`:** write-only-on-change DOM updates.
- **From the readability package:** tag stripping, `<noscript>` handling,
  script removal, and `img`/`srcset` rewriting.
- **From the existing parsers:** `input-eml.cpp` for MIME, `doc.misc.eml()` for
  writing, and `input-ics.cpp`/`input-vcf.cpp` to preview invitations and
  cards.
- **Shared with the archive package:** document-owned tasks (AR-E4, S13.3.1v2)
  and task-to-view messages (AR-O13, K30g). Both are built once for both
  consumers, not separately for mail.

### 5.8 Security and privacy

- **TLS.** TLS is always verified, except for an explicit pin on a loopback
  server. STARTTLS downgrade is refused (`CURLUSESSL_ALL`). Plain IMAP or
  SMTP is allowed only to `127.0.0.1`/`::1`.
- **Framing limits.** The tokenizer has hard limits on line length, list
  depth and literal size, and spills large literals to disk. MIME decoding has
  limits on part count, nesting depth and decoded size, the same shared-quota
  idea as archive input (S12.4.1v3).
- **HTML mail.** No scripts run (ML5v2), and there are no forms, plugins or
  navigation inside the message view. A clicked link opens in the system browser, and the status bar shows
  its real target first. CSS that could paint outside the message is stripped
  (`position: fixed`, viewport units on overlays).
- **Remote content.** Blocked by default. The user allows it per message, or
  always for a sender.
- **Secrets.** Never logged. `AUTHENTICATE` and `LOGIN` arguments are redacted
  in `log_debug` output, and curl's verbose output is never enabled with real
  credentials.
- **Attachments.** Never opened automatically. Saving one goes through the
  normal file IO door.
- **Store.** The profile directory is created with mode 0700. It holds mail
  and the index, never secrets.

## 6. Engine gaps to verify

None is assumed. Each is checked by a focused test or a measurement at the
milestone that first needs it, and fixed at its root if real.

| ID | Question | First needed | If it is a real gap |
|---|---|---|---|
| **MG1** | After `CURLOPT_CONNECT_ONLY` on `imaps://`, and on `imap://` with STARTTLS, is the session authenticated with no bytes left in curl's pingpong buffer? Can the event loop wait on `CURLINFO_ACTIVESOCKET` and drive `curl_easy_recv`/`curl_easy_send` with parking? | M0 | **Resolved for plain IMAP** (M0): the handoff works against the scripted server, with no bytes left behind and waits on the loop. TLS and STARTTLS over the handoff are untested until a live server (M1). |
| **MG2** | Are IMAP(S), POP3(S) and SMTP(S) compiled in on all three platforms? On Linux, does the system libcurl report them in `curl_version_info()->protocols`? | M0 | **macOS done** (M0): libcurl rebuilt with the three protocols. Windows: setup flags changed, not built. Every platform: `open` and `send` check `curl_version_info()` and refuse a missing protocol clearly. |
| **MG3** | Can a sync task outlive the handler that starts it and report to the window? Document-owned tasks (S13.3.1v2) and task-to-view messages (AR-O13) are not implemented today. | M1 | **Resolved** (M1d): each account task is document-owned, and it reports through `dom.dispatch` (AR-O13, landed with the archive package). A frame rendered from such a report was never loaded; Radiant now loads it at the next quiescent host turn (RAD_20 §6.3). |
| **MG4** | Can an embedded document load with scripting disabled and remote loads blocked? Today Radiant has no runtime switch to turn scripts off. `<iframe srcdoc>` inherits its parent's `DocumentJsHostConfig` (`radiant/layout_block.cpp`), `html_scripting_enabled` is hard-coded on (`radiant/cmd_layout.cpp`), and the remote-resource policy is set only by a CLI flag. | M1 | **Resolved for scripting** (M1c): a `sandbox` frame loads with `DomDocument::scripting_disabled`, inherited and never cleared (RAD_20 §6.3); `allow-scripts` is not honoured yet. Remote loads are blocked by the sanitizer; the engine-level policy is MQ14. |
| **MG5** | MIME coverage. `input-eml.cpp` decodes base64 and quoted-printable but not RFC 2047 encoded words, and it does not convert charsets. Lambda has no legacy charset decoder (ISO-8859-x, Windows-125x, GB18030, Big5, Shift_JIS, EUC-KR, ISO-2022-JP). Nested multipart, `message/rfc822` parts and tolerance of malformed mail are unverified. | M1 | **Resolved** (M1a): `lib/mail_mime` decodes RFC 2047 words, RFC 2231 parameters and address lists, and converts charsets through iconv; the `eml` reader gains decoded subjects, `addresses` and the `mime` part tree, and keeps the last part of a cut-off multipart. Covered by `test_mail_mime_gtest` and the `test/lambda/mail` goldens. |
| **MG6** | Secret storage. There is no Keychain or secret-store API; the only Security framework use is `lib/trust_store.c`. | M1 | **Resolved** (M1b): `lambda.io.secret` (`get`, `set`, `delete`) over `lib/secret_store` (SecItem generic passwords on macOS; other platforms raise "no secret store on this platform", MQ7). Each call runs on the offload pool and parks. ML8's gate is not applied yet (MQ9). |
| **MG7** | OAuth enablers. No script can listen on a loopback port: `lambda serve` is a separate process, and its Lambda backend is stubbed. There is no SHA-256, base64url encoder or secure random for PKCE. Opening the system browser through `io.exec("open", [url])` is untested. | M4 | **Resolved** (M4): `lambda.io.loopback` (`listen`, `accept`) waits for one browser request on 127.0.0.1, parked on the event loop; `hash`, `uuid` and `url_encode` are system functions (the names Archive AR-E7 planned); the system browser opens through `io.exec` (`open`, `start`, `xdg-open`). |
| **MG8** | Scale. Does a 50 000-message mailbox open from its index and scroll within the frame budget with Finder's row window? | M1 | **Partly resolved** (M1d): the list mounts only the rows near its viewport. A 50 000-entry index opens and scrolls in the debug build. Two Radiant grid defects found on the way are fixed (M1 checkpoint). The release-build timing is still to be measured. |
| **MG9** | Large literals. Does a 25 MB attachment stream to disk without an in-memory copy, from the tokenizer through `input-eml`? | M1 | **Resolved for the store** (M1a): `fetch_message` fetches with `to`, so a large body streams into `incoming/` and moves into place without a copy in memory. Reading it back with `input(…, 'eml')` still holds the decoded parts in memory. |
| **MG10** | Compose editing. Is contenteditable through the DOM editing package (D7.2.5) good enough for mail: lists, links, pasted HTML, inline images? | M3 | Fix in the editing package. Plain-text compose remains the fallback. |
| **MG11** | *Withdrawn with ML7v3.* SQLite writes from Lambda code are no longer needed before M1. They return with the later phase that removes SL1–SL3 (Appendix S, ML7v2). | — | — |
| **MG12** | Is Lambda's own search fast enough? `io.text_search` and `io.grep` over the sidecars of 10 000 messages, within 300 ms in a release build. Both rescan the files on every call. | M5 | Measure and record the result. A miss is SL4, left to the later phase that adds a persistent index to `lib/fts`. No database full-text index (ML12). |
| **MG13** | Can `lambda.io.mail` resolve as a built-in runtime module? Import resolution checks exact built-in names (`math`, `io`, `mcp`), then static Jube modules, then shipped packages under `lmd/package/` (`build_ast.cpp`). | M0 | **Resolved** (M0): `builtin_module_name` maps the full path `lambda.io.mail` to the `io_mail_*` rows; an unaliased import is refused. |

## 7. Milestones and acceptance

Milestones are ordered by dependency and user impact. Each ships a usable
increment.

| Milestone | Delivers | Acceptance |
|---|---|---|
| **M0 Transport** | curl flags on macOS and Windows and the Linux check; `lambda.io.mail` with `open`, `command`, `idle` and `send`; the IMAP session engine over `CONNECT_ONLY`; the tokenizer; POP3 and SMTP through curl; an opt-in live test server | Login with PLAIN and with a bearer token. `LIST`, `SELECT` and one `UID FETCH 1:*` of 10 000 envelopes. A POP3 `UIDL` and `RETR` through `mail.command`. IDLE wakes within 1 s when another connection appends. A hostile message whose body lines start with tags, `* ` and `{5}` round-trips byte-identical. A message sent to self reads back intact. MG1, MG2 and MG13 resolved. |
| **M1 Read** | Accounts with app passwords and Bridge, presets and autoconfig, Keychain, sidebar, message list, viewer (plain text, sandboxed HTML, the remote-content bar, `cid:` images), attachments with Quick Look, the store (message files, sidecars and a Mark index per mailbox), a fixture corpus | Gmail (app password), iCloud, Fastmail and Proton Bridge accounts open and read. A 50 000-message mailbox opens and scrolls within budget, or the miss is recorded under SL3. Killing the demo mid-sync leaves every index file whole, and the next start replays pending actions and resumes. A hostile HTML fixture runs no script and makes no network request. MG3–MG6, MG8 and MG9 resolved. |
| **M2 Organise and sync** | Flags, move, archive, delete and junk with undo; drag to mailbox; IDLE push; CONDSTORE/QRESYNC; unread counts; the offline action queue; POP3 accounts (ML9) | A change made in another client appears within 2 s in an idling mailbox. Actions taken offline replay on reconnect. A UIDVALIDITY change discards that mailbox's cache instead of mixing messages. A POP3 account downloads each message once across restarts (UIDL) and leaves it on the server by default. |
| **M3 Compose** | Compose window; reply, reply-all and forward; attachments; signatures; drafts; SMTP send; the Sent copy; the outbox | Mail sent from the demo renders correctly in Gmail's web client and in macOS Mail, including `multipart/alternative`, UTF-8 subjects and attachments. MG10 resolved. |
| **M4 OAuth** | `lambda.oauth` with the PKCE loopback flow and refresh; Gmail and Microsoft over XOAUTH2/OAUTHBEARER | Sign-in with Google and with Microsoft from the demo. Refresh survives a restart. No token appears in `log.txt`. MG7 resolved. |
| **M5 Search and conversations** | Local search with `io.text_search` and `io.grep` (ML12), server search (`X-GM-RAW` on Gmail), conversation view, Gmail label chips, smart mailboxes, the dock badge | A phrase search over 10 000 cached messages returns within 300 ms in a release build, or the miss is recorded under SL4. MG12 measured. |

## 8. Verification budget

- **No network by default.** Framing, parsing and encoding are native gtests
  (`test_mail_imap_gtest`). The session engine runs against scripted loopback
  servers in Python (`test/mail/fake_mail.py`, `make test-mail`), which the
  scripts reach with `tls: false`. Hostile and edge cases include: a literal
  containing tag lines, zero-length literals, `{n+}` (LITERAL+), `NIL` versus
  `""` versus a literal, 8-bit bytes in quoted strings, `BYE` in the middle of
  a command, and untagged `EXISTS` during IDLE. (Recorded transcripts through
  a file-backed transport were the first plan; scripted servers also exercise
  curl's login and the socket waits.)
- **MIME fixtures.** An `.eml` corpus: RFC examples, encoded words, legacy
  charsets, nested multipart, attached messages and malformed mail. Each
  fixture has an `.ls` script and its `.txt` golden (AGENTS rule 8).
- **Pure-function goldens** for ENVELOPE and BODYSTRUCTURE interpretation,
  threading, quoting, modified UTF-7, the sanitizer, and applying server
  responses to a mailbox index.
- **Store.** Index round-trips through `output` and `input`. A kill during a
  save leaves the old or the new index, never a torn one. Pending actions
  replay after a restart.
- **UI replays.** `--event-file` replays under `test/ui/mail/`, run headless
  against a fixture cache directory with no server, in the form the Finder
  and photo-album tests use.
- **Live tests, opt-in.** `make test-mail-live` against Dovecot in an Apple
  `container`, the precedent the RDB drivers set, seeded with the fixture
  corpus. Gmail, Microsoft, Proton and iCloud need real accounts, so they get
  a manual checklist per milestone.
- **Performance** in a release build (AGENTS rule 10):

  | Measure | Target |
  |---|---|
  | Cold open to the painted list, 50 000 cached summaries | under 500 ms |
  | List scroll frame | under 16 ms |
  | Open a cached message | under 100 ms |
  | Envelope sync of 10 000 messages from a LAN server | under 5 s |

- **Engine gates.** Run `make test-lambda-baseline` or
  `make test-radiant-baseline` at a milestone boundary when that milestone
  changed the runtime or Radiant. Changes confined to the demo's `.ls` and CSS
  files need only the demo's own tests.

## 9. Open questions

| ID | Question | Recommendation |
|---|---|---|
| **MQ1** | ML1: hand curl's authenticated connection to a native engine, or build a host TLS socket in the `rdb_tunnel.c` shape? Forking curl's `imap.c` is answered in §2.6 (no). | **Answered: ML1 ruled** (user, 2026-10-22). The TLS socket stays the fallback if MG1 fails. |
| **MQ2** | Script surface: a generic `open()` with a mail URL, or a separate family? | **Answered** (user, 2026-10-22): the minimal native module `lambda.io.mail` (§5.3). URLs stay strings, so no new path scheme is needed. |
| **MQ3** | Default for remote content? | Blocked, allowed per message or per sender, as macOS Mail does. |
| **MQ4** | Who owns the OAuth client registrations? | Implemented as recommended (M4): the person registers their own client, kept in the profile's `oauth.mark`. For Google the demo asks for it in a setup panel the first time **Login with Gmail** is pressed; Microsoft's is written by hand, as the README explains. Ruling pending. |
| **MQ5** | Gmail labels: as mailboxes, or as chips on messages? | Mailboxes in M1, as IMAP presents them. Chips through `X-GM-LABELS` in M5. |
| **MQ6** | Add JMAP (Fastmail) or Gmail API adapters? | Not in scope. Revisit after M5; both are reachable through `io.fetch` today. |
| **MQ7** | Secret store on Linux and Windows? | macOS first. The other platforms report a clear "not supported" until a backend lands. |
| **MQ8** | Where does the cache live? | The user data directory (`~/Library/Application Support/Lambda Mail/` on macOS) for the demo, and `./temp/` for tests. |
| **MQ9** | ML8 needs a design ruling: mail sessions and secrets only for local-file documents. Extend D7.5.4's origin gate, or write a new D7.5 ruling? | A new D7.5 ruling that generalises the origin gate to credentials and non-HTTP network protocols. |
| **MQ10** | POP3: delete messages from the server after download? | Leave them on the server by default, with an account option to delete after a set number of days, as macOS Mail did. |
| **MQ11** | How does the message iframe request scripting-disabled loading (MG4)? | **Answered** (user, 2026-10-22): the standard `sandbox` attribute, backed by a per-document scripting-disabled flag. Radiant treats any sandboxed frame as script-free, even with `allow-scripts`, and logs that the token is not yet honoured. When sandboxing lands, the attribute gains its full meaning with no change to the demo. |
| **MQ12** | ML11 needs D7.2.4 amended: a built-in module nested under `lambda.io`. | **Answered: ratified as D7.2.4v4** (user, 2026-10-22; Formal Design 39.1.0). v3 allowed built-in modules only at `lambda.<module>`. v4 adds that `lambda.io.<leaf>` may name a built-in runtime module whose functions park on IO, which a Jube module may not do (D7.4.2v2). It is imported by its full path only, and its leaf may name neither a shipped package nor an `io` system function. Marked `*` until `lambda.io.mail` exists. |
| **MQ14** | How does the message frame block remote loads in the engine? Today the sanitizer removes them (ML5v2's second layer), which CSS escapes or `image-set()` strings could slip past. Radiant has a per-document `LOCAL_ONLY` admission policy, but no markup asks for it. | A `<meta http-equiv="Content-Security-Policy">` that the sanitizer writes into the message: Radiant would map a policy whose fetch directives admit no network source to `LOCAL_ONLY`, leaving the rest of CSP unimplemented. A sender's own policy can only narrow it. The alternatives are mapping `sandbox` itself, which is not what the standard means, or the `csp` attribute of CSP Embedded Enforcement, a draft that only Chromium ships. |
| **MQ13** | Who writes the SQLite index? | **Answered** (user, 2026-10-22): no SQLite index for now. The store is a Mark index per mailbox (ML7v3), written by Lambda code with `output(…, {atomic: true})`. |

---

## M0 checkpoint — 2026-10-22

**Delivered.**
- **libcurl** 8.10.1 rebuilt on macOS with IMAP, POP3 and SMTP (Appendix A).
  The archive grew from 783 KB to 826 KB. The rebuild followed the setup
  script, which has also required curl's cookie engine and Brotli since
  2026-10-09. This machine's archive predated that, so cookies and Brotli
  decoding are now on here too. `stage_brotli_for_curl` failed when
  `mac-deps/brotli/out` existed without this checkout's build manifest; it now
  runs the incremental build step first.
- **`lib/mail_imap.{h,c}`**: framing (literals read by byte count, status lines
  never carry literals, large literals streamed into a file), the token-tree
  parser, and the command encoder (quoted or literal, LITERAL+ and LITERAL-,
  UTF8=ACCEPT).
- **`lambda/runtime/io_mail.cpp`**: `open`, `command`, `idle` and `send`, with
  their option-name tables, the `io_mail_*` registry rows and the
  `lambda.io.mail` import (D7.2.4v4).
- **A socket waiter in the scheduler** (`lambda_socket_waiter_*`): it parks a
  task until its socket is ready, on the loop. Closing a waiter wakes a task
  parked on it with an error. `lambda_task_running()` exposes the running task
  to other runtime files.
- **`io_fs_option_text`**, promoted from `io.fetch`'s static helper to the
  shared io header rather than copied (AGENTS rule 13).
- **`make test-mail`** and `test/mail/`: scripted loopback IMAP, POP3 and SMTP
  servers in Python, with Lambda scripts driving them.

**Verification.**
- `test_mail_imap_gtest`, 21 tests: byte-by-byte framing, a hostile literal, a
  status line ending in braces, spilling, limits, parsing, encoding, and the
  refusal of injected command text.
- `make test-mail`, 6 tests: an IMAP session (SELECT, FETCH, the hostile body
  round-tripped byte for byte through APPEND, a spill under `to`, a NO error,
  NOOP); IDLE with an event, a quiet timeout and a cancellation that sends
  `DONE` on close; password and token logins and the refusals; POP3 STAT, UIDL
  and RETR with dot-unstuffing and `-ERR`; SMTP with a rejected recipient and a
  line starting with a dot; the refused unaliased import; and the fully
  qualified spelling `lambda.io.mail.open(…)`.
- `make test-lambda-baseline`: 5 936 of 5 937. The one failure,
  `latex_phase3_corpus`, passes alone in 50 s; it is the LaTeX corpus test
  known to time out under parallel load.

**Findings.**
- curl's pingpong layer hands every reply line to the header callback. POP3's
  `text` and SMTP's rejected-recipient list rely on that, and the tests confirm
  it.
- A process that exits with a session still open resets the connection without
  LOGOUT, because the worker that would log out never runs. Servers tolerate
  this; a graceful exit would need the scheduler to drain sessions at teardown.
- Not yet exercised: TLS and STARTTLS through the handoff, a live server
  (Dovecot in an Apple `container`), Gmail and Microsoft OAuth, and a Windows
  build.

**Next.** M1: accounts and presets, the Keychain store (MG6), the scripting-off
message view (MG4), MIME decoding (MG5), the store (ML7v3), and the window.

## M1 plan — 2026-10-22

M1 splits into four steps, in dependency order. Each ends with its own tests.

| Step | Delivers | Needs |
|---|---|---|
| **M1a Data** | MIME decoding in the `eml` reader (MG5): RFC 2047 headers, address lists with display names, charsets to UTF-8, and a MIME part tree. The `lambda.mail` package: IMAP response interpretation, modified UTF-7, the Mark store (ML7v3), and a headless sync procedure. | No new rulings. Header sync fetches each message's header block and decodes it with `parse(bytes, 'eml')`, so no separate decoding function is needed. |
| **M1b Secrets** | The OS secret store (MG6) | Ruled (user, 2026-10-22): `lambda.io.secret` with `get`, `set`, `delete`, Keychain on macOS |
| **M1c Message view** | Script-free, remote-blocked rendering of HTML mail (MG4, ML5v2) | MQ11 answered (user, 2026-10-22): the `sandbox` attribute plus a per-document scripting-disabled flag |
| **M1d Window** | `test/demo/mail/`: sidebar, list, viewer, attachments, presets and autoconfig; the sync task reports through `dom.dispatch` (AR-O13, resolving MG3) | M1a–M1c |

## M1a checkpoint — 2026-10-22

**Delivered.**
- **`lib/mail_mime.{h,c}`**: charsets to UTF-8 (fast paths for UTF-8 and
  windows-1252, which WHATWG also uses for the latin-1 labels; iconv
  otherwise, with GB2312 read as GB18030 and Shift_JIS as CP932), RFC 2047
  header decoding (adjacent words in one charset decode together, so a split
  multibyte character survives), RFC 2045/2231 parameters, and RFC 5322
  address lists with groups, comments and source routes. `lambda-lib` now
  links iconv, which `lambda-data` already did for archive names.
- **The `eml` reader** (`input-eml.cpp`) now uses those helpers instead of
  its own parameter parser. It decodes `subject` and adds `addresses`
  (`{name, address}`, the shape `doc.misc.eml()` writes) and `mime`, the part
  tree. A boundary is never RFC 2047-decoded, since `=` and `?` are legal in
  it. The `doc.misc` eml writer accepts both fields as views of the headers
  and `raw_body` and does not serialize them.
- **The `lambda.mail` package** (`lmd/package/mail/`): `imap` (modified
  UTF-7, LIST, SELECT, FETCH, UID sets), `mime` (bodies, attachments, `cid:`
  parts, snippets, search text), `store` (the ML7v3 layout, atomic index
  saves, merges by UID) and `sync` (mailboxes, a mailbox sync, message
  download).

**Verification.**
- `test_mail_mime_gtest`: 8 tests; `test_mail_imap_gtest`: 21.
- `test/lambda/mail/` goldens (`eml_mime`, `imap_parse`, `mime_helpers`),
  passing on both the interpreter and the JIT tier, over a fixture corpus in
  `test/input/mail/`: encoded headers, a mixed/related/alternative message
  with an RFC 2231 filename, legacy charsets, format=flowed, and a malformed
  message.
- `make test-mail`, 7 tests: the M0 six plus a store sync that runs three
  rounds against the fake server: first sync, then an unread flag, an
  expunge and a new delivery, then a UIDVALIDITY reset. The hostile and the
  spilled bodies are stored byte for byte.
- `make test-lambda-baseline` caught one regression, which is now fixed: the
  `doc.misc` round trip, whose writer rejected the new fields. The script
  format round trips (8 eml cases) pass. `latex_phase3_corpus` timed out under
  load and passes alone.

**Findings.**
- `if (c) return x` and a line that begins with `[` need braces or a `;` in
  Lambda; `state`, `type` and `last` are reserved words; a procedure that
  propagates with `^` must declare a `T^` return type. All are recorded here
  because the package hit each of them.
- Updating an index builds string-keyed maps (`{*: m, [k]: v}`) one entry at
  a time. If each step copies the map, a merge is quadratic in the mailbox
  size; MG8 will measure it on a 50 000-entry mailbox.

**Next.** M1b and M1c need the user's decisions (MG6's API, MQ11); M1d, the
window, follows them.

## M1b–M1d checkpoint — 2026-10-22

**Delivered.**
- **M1b, `lambda.io.secret`** (`lambda/runtime/io_secret.cpp`, `lib/secret_store.{h,c}`):
  - `get`, `set` and `delete` of generic passwords named by a service and an account, backed by the Keychain on macOS. Other platforms raise an error (MQ7).
  - Each call runs on the offload pool and parks, since the OS may wait on a prompt. Native copies of a value are wiped and values are never logged.
  - `lambda.io.<leaf>` modules now resolve from one table in `build_ast.cpp`, shared by `mail` and `secret`. Both refuse an unaliased import.
- **M1c, script-free frames** (RAD_20 §6.3):
  - A frame carrying `sandbox` loads with `DomDocument::scripting_disabled`. The flag is inherited by nested documents and never cleared, and `allow-scripts` is logged as not honoured.
  - With the flag set: no script runs, the parser treats `<noscript>` content as markup, `on*` attributes never compile, external scripts are not prefetched, and `.ls` documents and `<webview>` are refused.
  - `lambda.mail.sanitize` is the second layer: it removes scripts, handlers, embedded documents, forms and frame navigation, blocks remote loads until allowed, inlines `cid:` parts and cleans `<noscript>` content.
- **M1d, the window** (`test/demo/mail/`, `lambda demo mail`):
  - The account sheet, with presets and an ISPDB lookup (`lambda.mail.providers`).
  - A sidebar with roles and unread counts, and a message list that mounts only the rows near its viewport.
  - A viewer with headers, quoted plain text, the sandboxed HTML frame, a remote-content bar and an attachment preview with Save to Downloads.
  - One document-owned task per account, which reports through `dom.dispatch`.
  - The profile holds no password (ML6).

**Engine fixes found on the way.**
- **Frames rendered from a task's report never loaded.** Layout cannot load a frame while an evaluator is executing, and nothing retried. `layout_ensure_iframe_document` now records the deferral, and the host pump's next quiescent turn reflows. This may also be the archive demo's known issue of dropped frames; that is not re-checked.
- **Grid `fr` rows grew to the content of scroll containers.** Two causes:
  - After the content pass, Radiant grew every `fr` row to its items' content height. In a definite-height container an `fr` row now grows only through an item's automatic minimum.
  - The §11.5 floor for `auto`-minimum flexible tracks used min-content where §12.5.4 uses the minimum contribution. That contribution is zero for scroll containers and for `min-height: 0`.
  - `clientHeight` of such items was the content height (5000 instead of 300), which made the list mount every row.
- **A typed password reached `log.txt`.** `lambda.dom.validate` parsed every control's value as a number, and a failed parse logs its text. Only `number` and `range` values are parsed now (HTML §4.10.5.3).

**Verification.**
- `make test-mail`: 11 tests, one skipped off macOS. They include the Keychain round trip and the window replay against the fake server (`tests/mail_window.json`, 32 assertions).
- The window replay adds an account through the sheet and syncs. Inside the HTML message's frame it checks that the script did not run, the tracker has no source, the `cid:` image is inlined and the link is inert. It then loads remote content, previews an attachment and switches mailboxes.
- Goldens on both tiers: `test/lambda/mail/sanitize`, `providers`, and `test/demo/mail/tests/mail_model`.
- UI tests:
  - `dom_iframe_sandbox` (frames and nesting);
  - `dom_iframe_task_render` (fails with the retry disabled, passes with it);
  - `layout_grid_fr_scroll` (browser heights).

**Findings.**
- The Keychain grants read access per program. A password stored by one build of `lambda.exe` prompts the next build, so the tests remove their items with the `security` tool. A leftover item made one development run prompt.
- The replay harness logs what it types (`event_sim: type`). That is test-only.

**Not yet.**
- MG8's release-build timing.
- MQ14 (engine-level remote blocking).
- ML8's origin gate (MQ9).
- Opening links in the system browser (§5.8).
- A unified Inbox, keyboard navigation of the list, and live accounts (the manual checklist).

## ML9 checkpoint — 2026-10-22

The user ruled ML2, ML3, ML4, ML9 and ML10. ML2, ML4 and ML10 were already
implemented in M0, and ML3 keeps its M0 interim (a session closes when its
opening task ends). ML9 was implemented on that ruling.

**Delivered.**
- **`lambda.mail.pop3`:**
  - `sync_inbox` lists the server with UIDL and finds what the local Inbox lacks with `except`, so a round with nothing new makes one pass. It downloads each missing message once with RETR, streaming large ones through `incoming/`.
  - Each message is filed with `sync.file_message`, shared with IMAP downloads. The entry gets a local UID and the server's UIDL. The index is saved every 25 downloads, so an interrupted first sync keeps its progress.
  - Nothing is deleted on the server (MQ10's recommended default; MQ10 is not ruled). A message the server drops stays local.
  - `mark_seen` keeps read state in the store.
- **Providers:** POP3 servers in the presets where the host offers them (not iCloud or Proton Mail Bridge), `pop3_url`, and POP3 entries from ISPDB documents.
- **The window:**
  - The account sheet chooses IMAP or POP3, and its server fields are now protocol-neutral.
  - A POP3 account's task runs each round in a child task, so the session closes and releases the server's maildrop lock (RFC 1939 §8) between rounds.
  - Opening a POP3 message marks it read locally. IMAP read state waits for M2, where it goes to the server.
- **`store.safe_name`** replaces the demo's own encoder. A capital letter now becomes `_` plus its lower case, so names that differ only in case (UIDLs, mailbox names) stay distinct on macOS's case-insensitive file system. Store paths change accordingly, for example `INBOX` is filed as `_i_n_b_o_x`.

**Verification.**
- `make test-mail`: 13 tests, one skipped off macOS. `test_pop3_sync` checks that a second round downloads nothing, that a new delivery is the only download, that stored files match the server byte for byte, and that no DELE is sent.
- `tests/mail_window_pop3.json` adds a POP3 account through the sheet, checks the local read state, and confirms that Get Mail downloads nothing twice.
- Goldens on both tiers: `test/lambda/mail/pop3`, plus the updated `providers` and `mail_model`.

**Not yet.** MQ10's optional deletion after a set number of days, flags and moves for POP3 mail (M2), and the "N new messages" count on the status line.

## M4 checkpoint — 2026-10-22

The user asked to implement OAuth after Gmail refused an ordinary Google
password over IMAP (`[AUTHENTICATIONFAILED] Invalid credentials`).

**Delivered.**
- **Native enablers (MG7):**
  - `hash(value, 'sha256' | 'sha384' | 'sha512')` returns binary, from mbedTLS.
  - `uuid()` is a procedure returning a version 4 UUID from mbedTLS's entropy-seeded CTR_DRBG.
  - `url_encode(text)` keeps the RFC 3986 unreserved characters.
  - `lambda.io.loopback` (`listen()` with `.port`, and `accept(listener, seconds, page)` returning `{method, path, query}`) is a built-in module under D7.2.4v4. It binds 127.0.0.1 only, and its wait parks the task on the event loop with the socket waiter mail already uses.
- **`lambda.oauth`** (`lmd/package/oauth.ls`):
  - `sign_in` takes PKCE with an S256 challenge, a verifier of two UUIDs (64 hex characters) and a UUID `state`. It shows the page in the system browser, or through an `open` callback, then receives the redirect and exchanges the code.
  - `refresh` renews tokens. Both raise with the provider's reason.
- **The demo:**
  - The Gmail and Outlook.com presets name Google's and Microsoft's endpoints. When `oauth.mark` registers a client, the account sheet offers "Sign in with Google/Microsoft".
  - The sign-in runs in a document-owned task, which writes the refresh token to the Keychain itself and reports only the outcome, so no token travels through view events.
  - Each login trades the refresh token for an access token (`login_options`, shared by IMAP and POP3) and saves a rotated one.
  - Accounts carry `auth` (`"password"` or `"oauth"`) and their endpoints; accounts saved before keep the password path.
  - The sheet now also shows the certificate-pin field for Proton Mail Bridge.

**Verification.**
- `make test-mail`: 16 tests, one skipped off macOS.
  - `test_oauth` drives `lambda.oauth` against a fake provider: sign-in through the loopback listener with the S256 challenge checked, refresh, a refused sign-in, a bad client, and an IMAP login with the token.
  - `test_sync_oauth` drives the demo's account task, which refreshes the token, logs in with it rather than a password, and syncs the Inbox.
  - `tests/mail_signin_sheet.json` checks the sheet's sign-in choice and the Bridge pin field.
- Goldens on both tiers: `test/lambda/hash_url_encode` (FIPS 180-2 vectors), `test/lambda/oauth_pkce` (the RFC 7636 Appendix B vector), `test/lambda/proc/uuid`, and the updated `mail_model`.

**Not yet.**
- A real Google sign-in. It needs the person's own client, which the README explains how to create.
- Microsoft against a real account.
- Signing in again from the window when a refresh token is revoked.
- Rulings on MQ4 and on the new system-function names, which follow E7/AR-E7.

## Login with Gmail — 2026-10-22

The user asked for a "Login with Gmail" button that works through OAuth.

**Delivered.**
- **One-press sign-in:**
  - The account sheet opens with **Login with Gmail**. It signs in as whoever the person chooses on Google's page, then learns the address from Google's OpenID userinfo endpoint (`oauth.userinfo`, scope `openid email profile` added to Gmail's), so nothing is typed.
  - An account for the same address, say one added with a password, turns to OAuth in place instead of being added twice (`model.gmail_account`, `with_account`).
  - The sign-in runs in a document-owned task (`mod_signin.ls`) that stores the refresh token itself, as before.
- **In-app client setup (MQ4):**
  - Without a Google client, the button opens a setup panel: three steps, **Open Google Cloud** (`oauth.open_browser`), and fields for the client ID and secret.
  - The panel checks the ID's `.apps.googleusercontent.com` suffix and writes the client to `oauth.mark`, merged with any other provider's.
- **Remove:** each sidebar account has **Remove** with a confirm row. It cancels the account's task, deletes the Keychain item and rewrites `accounts.mark`.
- **Keychain:** `secret_store_set` replaces an item (delete, then add) instead of updating it. An updated item keeps its access list, which trusts only the program that created it, so after a rebuild the writer was asked for access to the value it had just written.
- **Radiant fix (event dispatch):**
  - When an author click handler rebuilds the subtree under the target, the UA tier rebinds the target through its child-index path. That path now also records each step's node name, and a rebuilt node whose name differs is no longer taken as the target.
  - Before the fix, pressing Save in the setup panel, which swaps the panel for the sheet, delivered the button's click to the Protocol `<select>` that now sat at the same indices, and its dropdown opened.

**Verification.**
- `make test-mail`: 18 tests, one skipped off macOS.
  - `test_signin_gmail` drives `mod_signin.sign_in_gmail` against the fake provider. It checks that the address comes from userinfo with no login hint sent, that a password account of the same address turns to OAuth, and that the token lands in a test Keychain service.
  - `tests/mail_google_setup.json` covers the setup panel: a rejected ID, the save, `oauth.mark`'s contents, and the Protocol dropdown staying shut.
  - `mail_window_pop3.json` now ends by removing the account; the test checks `accounts.mark` and the Keychain.
- `mail_model` golden on both tiers, with the Gmail cases.
- Radiant baseline after the dispatch fix: 6305/6307. The two failures were there before (CssCascadeMemory jqueryui bytes, and the NativeLayout CPU-budget timeout); all 680 UI replays pass.

**Follow-up, the first real try.** Google's page answered "The OAuth client
was not found" (`Error 401: invalid_client`). Both values saved in the user's
`oauth.mark` began with `- ` before a well-formed ID and secret, and the panel
had checked only the ID's suffix. Simulated pastes (plain text, fragment HTML
and list-item HTML) save clean values, so the prefix came with what was pasted
or typed; headless runs cannot read the macOS pasteboard to show which.
- `google_client_error` now checks the whole shape: the ID against
  `\("\d"+ "-" ("a" to "z" | "\d")+ ".apps.googleusercontent.com")`, and the
  secret against `\(("\w" | "-")+)`.
- Login with Gmail reopens the panel, prefilled with the error, for a saved
  client that fails the check. **Change Google client** opens it for one that
  passes. The secret field is plain text, so stray characters can be seen.
- Tests: `tests/mail_google_fix.json` (`test_demo_google_fix`), a prefixed case
  in `mail_google_setup.json`, and golden cases.

**Not yet.**
- A real Google sign-in from the button, after the user corrects the client.

## Appendix A. libcurl build change

**macOS** (`setup-mac-deps.sh`, `build_curl_with_http2_for_mac`): remove
`--disable-imap`, `--disable-pop3` and `--disable-smtp` from the configure
line. Keep `--disable-mime`, because Lambda builds MIME itself.
`curl_supports_web_features` decides whether an existing `libcurl.a` must be
rebuilt. It should also require the `Curl_handler_imaps`,
`Curl_handler_pop3s` and `Curl_handler_smtps` symbols, so existing checkouts
rebuild once.

**Windows** (`setup-windows-deps.sh`): the same three flags. Schannel supports
STARTTLS and implicit TLS through curl. Pinning a Bridge certificate under
Schannel needs a check in M0.

**Linux**: no build change. At first use, check
`curl_version_info(CURLVERSION_NOW)->protocols` for `imaps`, `pop3s` and
`smtps`, and raise a clear error when they are missing.

No curl source file changes (§2.6). The size cost of `imap.c`, `pop3.c`,
`smtp.c` and the SASL and authentication helpers is measured in M0 and
recorded here.

## Appendix B. IMAP session engine sketch

- **Connect.** An easy handle with the URL `imaps://host:993/`, or
  `imap://host:143/` with `CURLOPT_USE_SSL = CURLUSESSL_ALL`.
  `CURLOPT_USERNAME`, then `CURLOPT_PASSWORD` or `CURLOPT_XOAUTH2_BEARER`
  (`CURLOPT_LOGIN_OPTIONS "AUTH=XOAUTH2"` forces the mechanism). Set
  `CURLOPT_CONNECT_ONLY = 1`, the trust hook, and an optional
  `CURLOPT_PINNEDPUBLICKEY`. The transfer runs on the multi backend, so the
  task parks.
- **After connect.** Issue `CAPABILITY` again, because servers change it after
  authentication. Then `ENABLE` (CONDSTORE, QRESYNC, `UTF8=ACCEPT` when
  offered) and `ID` where servers expect it.
- **Commands.** The engine's own tag counter and one command in flight per
  session (§2.6). Untagged responses that arrive before the tagged completion
  belong to that command's `responses` (§5.3).
- **Framing.** Read a line to CRLF. If it ends in `{n}` or `{n+}`, read exactly
  `n` bytes as a literal and continue the same response. Hard limits apply to
  line length, nesting depth and in-memory literal size. Larger literals spill
  to a file under the cache directory.
- **Sending literals** (APPEND, 8-bit search strings): use LITERAL+ when
  advertised (RFC 7888); otherwise wait for the `+` continuation.
- **IDLE.** Send `IDLE`, wait for `+`, then park until an untagged response,
  cancellation, or a timer of about 25 minutes (RFC 2177 servers drop idle
  clients after 30). Then send `DONE`.
- **Waiting on the socket.** Register `CURLINFO_ACTIVESOCKET` with the event
  loop. `curl_easy_recv` returns `CURLE_AGAIN` when no data is ready.
- **Logging.** `log_debug` lines prefixed `mail:`, with credentials redacted.

## Appendix C. External references

- Google, [OAuth 2.0 for iOS and desktop apps](https://developers.google.com/identity/protocols/oauth2/native-app):
  loopback redirect, required PKCE, desktop clients treated as public clients.
- Google, [Using OAuth 2.0](https://developers.google.com/identity/protocols/oauth2):
  7-day refresh tokens for clients in "Testing" status.
- Google, [XOAUTH2 mechanism for IMAP and SMTP](https://developers.google.com/gmail/xoauth_migration).
- Microsoft, [Outlook.com and Basic authentication](https://support.microsoft.com/office/outlook-and-other-apps-are-unable-to-connect-to-outlook-com-f4202ebf-89c6-4a8a-bec3-3d60cf7deaef):
  basic authentication off for Outlook.com since 2024-09-16.
- Microsoft 365 Message Center MC786329, retirement of basic authentication
  for SMTP client submission in Exchange Online. Its dates have moved more
  than once; treat OAuth as the only path.
- Proton, [Bridge port settings](https://proton.me/support/port-already-occupied-error):
  default ports 1143 and 1025.
- IETF: RFC 9051 and 3501 (IMAP), 2177 (IDLE), 7162 (CONDSTORE/QRESYNC), 6851
  (MOVE), 6154 (special-use), 7888 (LITERAL+), 8970 (PREVIEW), 7628
  (OAUTHBEARER), 6409 and 8314 (submission and implicit TLS), 5322, 2045–2049
  and 2047 (message format, MIME and encoded words), 3676 (`format=flowed`),
  7636 and 8252 (PKCE and OAuth for native apps).
- curl 8.10.1 sources in `mac-deps/curl-8.10.1/`: `lib/imap.c`,
  `lib/pingpong.c`, `lib/multi.c`, and `docs/KNOWN_BUGS` §3.

## Appendix D. Store file sketches

A starting point for M1, not a contract; `lambda.mail.store` owns the real
formats. Each file carries a `version` field so a later phase can migrate it.
The index is a map rather than an element (M1a): entries are updated by UID,
which a map's `entries` array keeps simple.

```lambda
// <account>/mailboxes.mark
<mailboxes version: 1,
    <box id: "b01", path: "INBOX", role: 'inbox', unread: 12, total: 4810>
    <box id: "b02", path: "[Gmail]/Sent Mail", role: 'sent', unread: 0, total: 931>
>

// <account>/index/b01.mark — as lambda.mail.store writes it (M1a)
{version: 1, path: "INBOX", uidvalidity: 1700000001, uidnext: 48214, highestmodseq: 912377,
 entries: [{uid: 48213, file: "gm1278455344230334865", flags: ["\\Seen", "\\Flagged"],
            date: "Tue, 21 Oct 2026 09:14:00 +0200", internaldate: "21-Oct-2026 09:14:00 +0200",
            from: {name: "Ada Lovelace", address: "ada@example.org"},
            to: [{name: null, address: "me@example.org"}],
            subject: "Draft review", message_id: "<draft.1@example.org>",
            size: 48210, thread: 1781234567890123456, has_file: true}]}

// <account>/pending.mark
<pending version: 1,
    <action id: 7, kind: 'move', from: "b01", to: "b07", uids: [48213],
            undo: {kind: 'move', from: "b07", to: "b01"}>
>
```

## Appendix S. Superseded proposals

- ~~**ML7** — **The cache is files.** A per-account directory holds raw `.eml`
  files (the source of truth) and a Mark index per mailbox, keyed by
  UIDVALIDITY. Search runs over plain-text sidecars.~~ Superseded by ML7v2,
  then reinstated by the user as **ML7v3** (2026-10-22). ML7v3 adds message
  identity, atomic saves, the action order across files, and the limitations
  SL1–SL5. The reason given for dropping ML7, "no crash safety", was
  overstated: `output(…, {atomic: true})` already makes each single-file save
  atomic. What ML7 lacks is an atomic update across several files (SL2).
- ~~**ML7v2** — **Raw messages are files; the index is SQLite.** One SQLite
  database per profile holds accounts, mailboxes, message summaries, mailbox
  membership with flags, and the offline action queue. Lambda code writes it
  in transactions through the SQLite subset of RDB Phase 3 writes, and the
  window reads it through the RDB query path.~~ Superseded by **ML7v3** (user,
  2026-10-22): keep to what Lambda has today. Kept here as the candidate
  design for the later phase that removes SL1–SL3. It needs SQLite writes from
  Lambda code, which are not implemented: `input-rdb.cpp` opens databases
  read-only, and writes are Phase 3 ("Future") in
  [Lambda_IO_RDB.md](Lambda_IO_RDB.md). Note that SQLite is built with
  `SQLITE_THREADSAFE=0` and without FTS5. Its schema sketch:

  ```sql
  CREATE TABLE account (id INTEGER PRIMARY KEY, email TEXT, kind TEXT, -- 'imap' or 'pop3'
                        settings TEXT, secret_ref TEXT);
  CREATE TABLE mailbox (id INTEGER PRIMARY KEY, account INTEGER, path TEXT, role TEXT,
                        uidvalidity INTEGER, uidnext INTEGER, highestmodseq INTEGER,
                        unread INTEGER, total INTEGER, UNIQUE (account, path));
  -- server_id: X-GM-MSGID, EMAILID, the POP3 UIDL, or "mailbox:uidvalidity:uid"
  CREATE TABLE message (id INTEGER PRIMARY KEY, account INTEGER, server_id TEXT,
                        message_id TEXT, thread TEXT, date INTEGER,
                        from_name TEXT, from_addr TEXT, recipients TEXT,
                        subject TEXT, snippet TEXT, size INTEGER,
                        attachments INTEGER, has_file INTEGER,
                        UNIQUE (account, server_id));
  CREATE TABLE entry (mailbox INTEGER, uid INTEGER, message INTEGER,
                      flags TEXT, modseq INTEGER, PRIMARY KEY (mailbox, uid));
  CREATE INDEX entry_message ON entry (message);
  CREATE INDEX message_date ON message (account, date);
  CREATE TABLE pending (id INTEGER PRIMARY KEY, account INTEGER, action TEXT,
                        created INTEGER);
  ```
- ~~**ML5** — HTML mail rendered with scripts disabled through the iframe
  `sandbox` attribute, implemented in full.~~ Superseded by **ML5v2**
  (user, 2026-10-22): no script runs in mail content until Lambda implements
  sandboxing. Only a scripting-disabled loading flag is needed now.
