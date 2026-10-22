// Drives IMAP IDLE and its cancellation against the fake server of test_mail.py.
import mail: lambda.io.mail

pn idle_until_cancelled(url) {
    let s = mail.open(url, {user: "ada", password: "secret", tls: false})^
    mail.command(s, "SELECT", ["INBOX"])^
    mail.idle(s, 30)^
    mail.idle(s, 30)^
}

pn main() {
    let ports = input("temp/mail/test_ports.json")^
    let url = "imap://127.0.0.1:" ++ string(ports.imap)
    let s = mail.open(url, {user: "ada", password: "secret", tls: false})^
    mail.command(s, "SELECT", ["INBOX"])^
    // the connection's first IDLE sees a new message arrive
    let changes = mail.idle(s, 5)^
    print("event ", changes, "\n")
    let quiet = mail.idle(s, 1)^
    print("quiet ", quiet, "\n")
    let after = mail.command(s, "NOOP")^
    print("noop ", after.status, "\n")
    let h = start(idle_until_cancelled, [url])
    sleep(1500)^
    cancel(h)
    wait(h) ^ { print("cancelled: ", ^.message, "\n") }
}
