// lambda.mail.pop3 against the fake POP3 server of test_mail.py: downloads
// what the local Inbox lacks, once; run it again to see nothing re-fetched.
import mail: lambda.io.mail
import pop3: lambda.mail.pop3

pn main() {
    let ports = input("temp/mail/test_ports.json")^
    let root = "temp/mail/pop3-store"
    let s = mail.open("pop3://127.0.0.1:" ++ string(ports.pop3), {user: "ada", password: "secret", tls: false})^
    let index = pop3.sync_inbox(s, root, pn (n) => { print("downloaded ", n, "\n") })^
    print("entries ", len(index.entries), " uidnext ", index.uidnext, "\n")
    for (e in index.entries) { print("entry ", e.uid, " ", e.uidl, " ", e.file, " ", e.subject, " ", e.size, " ", e.has_file, "\n") }
    let seen = pop3.mark_seen(root, 1, true)^
    print("seen ", seen.entries[0].flags, "\n")
}
