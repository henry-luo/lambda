# Mail

`./lambda.exe demo mail` opens a mail client in the spirit of macOS Mail
([design](../../../vibe/Lambda_Demo_Mail.md)). It reads IMAP accounts through
the built-in module [`lambda.io.mail`](../../../doc/Lambda_Sys_Func.md#lambdaiomail).
The [`lambda.mail`](../../../doc/Lambda_Packages.md) package syncs mailboxes
into a local store. Passwords stay in the Keychain through
[`lambda.io.secret`](../../../doc/Lambda_Sys_Func.md#lambdaiosecret).

| Area | What it does |
| --- | --- |
| Account sheet | Email, password, provider and protocol. Presets cover Gmail, iCloud, Fastmail, Proton Mail Bridge, Outlook.com and Yahoo. An unknown domain is looked up in Mozilla's ISPDB. **Other (manual)** takes the incoming server, port, security, user name and a certificate pin. Use an app password; Proton Mail Bridge needs the password and pin it shows |
| POP3 accounts | Choose **POP3** in the sheet. Each message is downloaded once into a local Inbox and stays on the server. Read state lives only on this computer: opening a message marks it read there |
| Sidebar | Each account's status and its mailboxes. Special-use mailboxes (Inbox, Drafts, Sent, Junk, Trash, Archive) come first, then the rest by name, with unread counts for mailboxes that have been synced. **Get Mail** checks the shown mailbox now |
| List | Newest first: unread dot, sender, date, subject, flag and size. Only the rows near the viewport are mounted, so a mailbox of any size scrolls through a few dozen rows |
| Viewer | Header block and the body. Plain text shows quote levels and unwraps `format=flowed`. HTML loads in a sandboxed frame that runs no script; remote images and styles stay blocked until **Load Remote Content**, and `cid:` images come from the message. Links show their target but do not navigate yet |
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
| `mod_model.ls` | state, the sidebar, the list window, labels, reports, quoting, the account sheet (pure) |
| `mod_ui.ls` | layout, and the list and viewer templates |
| `mod_sync.ls` | the account task |
| `mail.css` | style |
| `tests/` | the model golden and the window replay |

## Tests

The model golden runs in `test_lambda_gtest` (`tests_mail_model`):

```sh
./test/test_lambda_gtest.exe --gtest_filter='*mail_model*'
```

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

The POP3 replay adds a POP3 account, checks that the Inbox holds both messages and that opening one marks it read locally, and checks that **Get Mail** downloads nothing twice. The test then checks the fake server: two `RETR`s and no `DELE`.

## Not yet

These are later milestones of the design: marking IMAP messages read, flags,
moves and deletes, IDLE push, compose and send, OAuth sign-in, search and
conversations. Also not yet done:

- opening links in the system browser;
- a unified Inbox;
- keyboard navigation of the list;
- the release-build measurement of a 50 000-message mailbox (MG8).
