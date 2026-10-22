// Syncs the fake account into a store (test_mail.py changes the server
// between runs) and downloads two bodies: one small, one spilled to disk.
import wire: lambda.io.mail
import sync: lambda.mail.sync

pn main() {
    let ports = input("temp/mail/test_ports.json")^
    let root = "temp/mail/store"
    let s = wire.open("imap://127.0.0.1:" ++ string(ports.imap), {user: "ada", password: "secret", tls: false})^
    for (b in sync.mailboxes(s)^) { print("box ", b.name, " ", b.role, " ", b.selectable, "\n") }
    let index = sync.sync_mailbox(s, root, "b01", "INBOX", false)^
    print("index ", index.uidvalidity, " ", index.uidnext, " ", len(index.entries), "\n")
    for (e in index.entries) {
        let sender = if (e.from == null) "-" else (if (e.from.name == null) e.from.address else e.from.name)
        print("entry ", e.uid, " ", e.flags, " ", e.subject, " ", sender, " ", e.file, "\n")
    }
    for (e in index.entries where e.uid == 3 or e.uid == 4) {
        print("fetched ", sync.fetch_message(s, root, e)^, "\n")
    }
    print("synced\n")
}
