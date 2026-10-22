// Drives lambda.io.mail against the fake IMAP server of test_mail.py.
import mail: lambda.io.mail

pn main() {
    let ports = input("temp/mail/test_ports.json")^
    let url = "imap://127.0.0.1:" ++ string(ports.imap)
    let s = mail.open(url, {user: "ada", password: "secret", tls: false})^
    let selected = mail.command(s, "SELECT", ["INBOX"])^
    print("select ", selected.status, " ", selected.code, " ", selected.text, "\n")
    for (r in selected.responses) { print("  ", r, "\n") }
    let listing = mail.command(s, "UID FETCH 1:* (UID FLAGS)")^
    for (r in listing.responses) { print("fetch ", r, "\n") }
    // the hostile message survives byte for byte: append it to Copies
    let fetched = mail.command(s, "UID FETCH 3 (BODY.PEEK[])")^
    let message = fetched.responses[0][2][3]
    print("body ", type(message), " ", len(message), "\n")
    let appended = mail.command(s, "APPEND Copies (\\Seen)", [message])^
    print("append ", appended.status, " ", appended.code, "\n")
    // a large literal streams into a file under `to`
    let big = mail.command(s, "UID FETCH 4 (BODY.PEEK[])", null, {to: "temp/mail/spill"})^
    print("spilled ", type(big.responses[0][2][3]), "\n")
    // a refusal raises with the server's reply
    mail.command(s, "SELECT", ["Missing"]) ^ { print("refused: ", ^.message, "\n") }
    let still = mail.command(s, "NOOP")^
    print("noop ", still.status, "\n")
}
