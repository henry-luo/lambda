// Drives lambda.io.mail against the fake POP3 server of test_mail.py.
import mail: lambda.io.mail

pn main() {
    let ports = input("temp/mail/test_ports.json")^
    let url = "pop3://127.0.0.1:" ++ string(ports.pop3)
    let s = mail.open(url, {user: "ada", password: "secret", tls: false})^
    let stat = mail.command(s, "STAT")^
    print("stat ", stat.status, " ", stat.text, " ", stat.responses, "\n")
    let uidl = mail.command(s, "UIDL")^
    print("uidl ", uidl.responses, "\n")
    let message = mail.command(s, "RETR", [1])^
    print("retr ", message.text, " ", message.responses, "\n")
    mail.command(s, "DELE", [9]) ^ { print("refused: ", ^.message, "\n") }
}
