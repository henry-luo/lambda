# Mail

`./lambda.exe demo mail` opens a mail client in the spirit of macOS Mail
([design](../../../vibe/Lambda_Demo_Mail.md)). It reads IMAP accounts through
the built-in module [`lambda.io.mail`](../../../doc/Lambda_Sys_Func.md#lambdaiomail).
The [`lambda.mail`](../../../doc/Lambda_Packages.md) package syncs mailboxes
into a local store. Passwords stay in the Keychain through
[`lambda.io.secret`](../../../doc/Lambda_Sys_Func.md#lambdaiosecret).

| Area | What it does |
| --- | --- |
| Demo mailbox | **Try the demo mailbox** in the account sheet adds an account that needs no server. See [the demo mailbox](#the-demo-mailbox) |
| Account sheet | Email, password, provider and protocol. Presets cover Gmail, iCloud, Fastmail, Proton Mail Bridge, Outlook.com and Yahoo. An unknown domain is looked up in Mozilla's ISPDB. **Other (manual)** takes the incoming server, port, security, user name and a certificate pin. Use an app password; Proton Mail Bridge needs the password and pin it shows |
| POP3 accounts | Choose **POP3** in the sheet. Each message is downloaded once into a local Inbox and stays on the server. Read state lives only on this computer: opening a message marks it read there |
| Sidebar | Each account's status and its mailbox tree. Special-use mailboxes (Inbox, Drafts, Sent, Junk, Trash, Archive) come first with their own icons, then the folders by name, with unread counts for mailboxes that have been synced. An account and any folder with children collapse and expand. The folder button beside the account name opens **New Mailbox** (a name and a parent). **Get Mail** checks the shown mailbox now, and the pencil opens a new message |
| List | Newest first. Each row has an unread dot and flag, then the sender (the recipients in Sent and Drafts), date, an attachment clip, the subject and two lines of preview. Only the rows near the viewport are mounted, so a mailbox of any size scrolls through a few dozen rows |
| Viewer | A toolbar, then a header with the sender's initials, name, address, date, subject and recipients, then the body. Plain text shows quote levels and unwraps `format=flowed`. HTML loads in a sandboxed frame that runs no script; remote images and styles stay blocked until **Load Remote Content**, and `cid:` images come from the message. Links show their target but do not navigate yet. A draft shows **Edit Draft** |
| Toolbar | **Reply**, **Reply All** and **Forward** for every account. The demo mailbox also offers **Archive**, **Move to Junk**, **Delete** (to Trash, or for good in Trash), **Flag** and **Mark as Unread**. After a move, the row below opens, as in Mail |
| Compose | A window with To, Cc, Subject, From and a plain-text body. Replies quote the original under an "On … wrote:" line, with `In-Reply-To` and `References`. Reply All leaves out your own address and duplicates. A forward carries the original's attachments. **Send** builds `multipart/alternative` (the text and its HTML) with [`lambda.mail.compose`](../../../lmd/package/mail/compose.ls) over `lambda.doc.misc`'s eml writer. **Save Draft** files the message in Drafts |
| Attachments | The strip below a message. A click opens a preview (images, text, PDF), where **Save to Downloads** writes the file without replacing an existing one |

The profile lives in `~/Library/Application Support/Lambda Mail/`. Set
`MAIL_DEMO_ROOT` to use another folder; its passwords are then filed under
their own Keychain service, `Lambda Mail (<folder>)`. The profile holds
`accounts.mark` (servers and user names, never a password) and one folder per
account with the store's `index/`, `messages/` and the cached
`mailboxes.mark`. A mailbox opens from its index at once, and the account's
task then brings it up to date. It checks again every two minutes.

macOS asks once per build of `lambda.exe` before it lets that build read a
saved password: the Keychain grants access per program.

## The demo mailbox

**Try the demo mailbox**, at the bottom left of the account sheet, adds the account **Demo Mailbox** (`alex@lambda.demo`). Use it to show the window without a real account. Its task never connects to a server. The first time it runs, it writes about 330 made-up messages into the profile's `demo/` folder. They are dated back from that moment and spread over ten mailboxes: Inbox, Drafts, Sent, Junk, Trash, Archive, Projects with two nested mailboxes, and Receipts.

The mail is chosen to exercise the window:

- HTML mail of many kinds: personal notes with lists and tables, newsletters with a `cid:` logo and a blocked remote image, CI builds, code reviews, calendar invitations, invoices, shipping updates, security alerts and a garish piece of junk;
- plain-text mailing-list traffic, with replies quoted two levels deep;
- a sender name in an RFC 2047 encoded-word;
- PNG, CSV, text and PDF attachments;
- flagged and unread messages, enough of them to scroll the virtualized list.

Everything is written by `lambda.doc.misc`'s eml writer, like the mail the window sends. Opening a message marks it read in the local index, as for POP3. Flags, moves, new mailboxes, drafts and sent mail all stay in that store too. Sending files the message in Sent, and in the Inbox as well when it is addressed to `alex@lambda.demo`. **Get Mail** delivers one more message to the Inbox per press, from a pool of five. Every person, address and company in it is invented (`.example` and `lambda.demo`). To start the mailbox over, remove the account, delete the profile's `demo/` folder, and add it again.

## Signing in with Google or Microsoft

Gmail and Outlook.com accept OAuth sign-in instead of a password, and Gmail
refuses an ordinary Google password over IMAP. A provider only signs people in
to an app it knows, so you register the demo as your own app once.

### Login with Gmail

**Add Account** opens the account sheet, whose first button is **Login with
Gmail**. The first time you press it, the sheet asks for a Google client:

1. **Open Google Cloud** opens console.cloud.google.com. Create a project there.
2. Under **Google Auth Platform**, set up the consent screen as an **External** app in **Testing**, and add your Gmail address as a **test user**.
3. Under **Clients**, create a client of type **Desktop app**.
4. Paste its client ID and client secret into the sheet and press **Save**.

The demo keeps the client in the profile's `oauth.mark`
(`~/Library/Application Support/Lambda Mail/oauth.mark`). A desktop client's
secret is not confidential, so the panel shows it in the clear, but keep the
file to yourself.

The panel checks the whole shape of both values, since Google answers a
mangled ID only with "The OAuth client was not found" (`invalid_client`). A
saved client that fails the check, say one with a stray character pasted before
it, reopens the panel with the values for correcting. **Change Google client**
under the button opens the panel for a client that passes.

Press **Login with Gmail** again and Google's page opens in your browser. Choose
the account and allow mail access. The browser returns to the demo on a port of
127.0.0.1, and the demo asks Google which address signed in (OpenID userinfo),
so you type nothing. The account's refresh token goes to the Keychain, and each
later login trades it for a fresh access token, so no password is ever stored.
If an account for the same address already exists, for one added with a
password, it switches to OAuth instead of being added twice.

While the app stays in Testing, Google expires its sign-ins after seven days, so
the account will ask you to sign in again weekly.

The first time the demo reads its Keychain item after a rebuild, macOS asks
whether `lambda.exe` may use it. Choose **Always Allow**. Each build is a new
program to the Keychain.

### Microsoft, and writing `oauth.mark` by hand

`oauth.mark` holds one client per provider:

```lambda
{google: {client_id: "1234-abc.apps.googleusercontent.com", client_secret: "GOCSPX-…"},
 microsoft: {client_id: "00000000-0000-0000-0000-000000000000"}}
```

**Microsoft:**
1. In the Azure portal under **App registrations**, register an app for personal Microsoft accounts.
2. Add the platform **Mobile and desktop applications** with the redirect URI `http://127.0.0.1`, the loopback address the demo listens on. Microsoft ignores the port of a loopback redirect when it matches one.
3. Copy its client ID into `oauth.mark`.

Microsoft sign-in is wired the same way as Google's but has not been tried against a real account yet.

With a client registered, choosing Gmail (or Outlook.com) in the account sheet
also offers **Sign in: With Google** (or Microsoft). **Sign in with Google**
then signs in as the address you typed.

### Removing an account

Each account in the sidebar has a **Remove** button. After you confirm, the
demo stops the account's task, deletes its password or refresh token from the
Keychain, and drops it from `accounts.mark`. The mail already downloaded stays
in the profile folder.

## How it is wired

The view's `on load` handler reads the accounts. It starts one task per
account with `{owner: 'document'}` (S13.3.1v2), so each task outlives the
handler and ends with the window. A task (`mod_sync.ls`) holds the account's
session, syncs the mailbox the window shows and downloads messages on
request. It reports through `dom.dispatch` on the app element, and those
reports arrive as `on mail(evt)`. The list and the viewer are templates of
their own. Scrolling re-runs only the list, and the message frame keeps its
document while the rest of the window changes.

| File | Role |
| --- | --- |
| `mail.ls` | the document: view, handlers, account tasks, the profile, attachments |
| `mod_model.ls` | state, the sidebar tree, the list window and rows, labels, reports, compose state, the account sheet (pure) |
| `mod_ui.ls` | layout: the sidebar, the list and viewer templates, the compose window and the sheets |
| `mod_sync.ls` | the account task: IMAP, POP3 or the demo store, and SMTP sending |
| `mod_demo.ls` | the demo mailbox: its account, its made-up mail and its local store |
| `mail.css` | style |
| `tests/` | the model golden and the window replay |

## Tests

The model golden runs in `test_lambda_gtest` (`tests_mail_model`):

```sh
./test/test_lambda_gtest.exe --gtest_filter='*mail_model*'
```

The package's own goldens are in `test/lambda/mail/`; `compose.ls` covers replies, forwards, quoting, the HTML of a plain-text body and the RFC 5322 text of a draft.

The window replays, `tests/mail_window.json` (IMAP) and
`tests/mail_window_pop3.json` (POP3), run in `make test-mail` against the fake
servers (`test/mail/fake_mail.py`). They need macOS, since the
password goes to the Keychain under a test-only service that the test removes
again. The IMAP replay:

- adds an account through the sheet and syncs the Inbox;
- checks rows, unread dots, decoded senders and mailbox roles;
- opens an HTML message, then checks inside its frame that the script did not run, the tracker has no source, the `cid:` image is inlined and the link is inert;
- loads remote content, previews the attachment, reads a plain-text message and switches mailboxes.

```sh
python3 test/mail/test_mail.py DemoWindowTests
```

`tests/mail_window_demo.json` adds the demo mailbox. It then checks:

- the mailboxes, unread counts, previews and attachment clips;
- an HTML message in its frame, a plain-text one with quote levels, and an attachment preview;
- flagging, then a reply: its recipients, subject and quote, the text typed above the quote, and the copy in Sent;
- **Get Mail**, then archiving and deleting, each followed by the row below opening;
- collapsing and expanding a folder, and **New Mailbox** under a folder;
- saving a new message as a draft, editing that draft, and sending it.

It checks too that the fake servers saw no command.

The POP3 replay adds a POP3 account, checks that the Inbox holds both messages and that opening one marks it read locally, and checks that **Get Mail** downloads nothing twice. The test then checks the fake server: two `RETR`s and no `DELE`.

## Not yet

These are later milestones of the design: marking IMAP messages read, flags,
moves and deletes (the demo mailbox does these locally), IDLE push, search and
conversations. Sending from an IMAP or POP3 account goes through its SMTP
server, and **New Mailbox** on an IMAP account sends `CREATE`; neither has run
yet, since this checkout's libcurl lacks the mail protocols. Compose is plain
text: rich text, attachments added by hand, signatures, saving drafts and
Sent copies to the server, and an outbox are P2. Also not yet done:

- signing in again from the window when a refresh token stops working (remove the account's folder and `accounts.mark` entry, then add it again);
- opening links in the system browser;
- a unified Inbox;
- keyboard navigation of the list;
- the release-build measurement of a 50 000-message mailbox (MG8).
