// Sends through the fake SMTP server of test_mail.py.
import mail: lambda.io.mail

pn main() {
    let ports = input("temp/mail/test_ports.json")^
    let url = "smtp://127.0.0.1:" ++ string(ports.smtp)
    let message = "From: ada@example.org\r\nTo: bob@example.org\r\nSubject: hello\r\n\r\nHi Bob,\r\n.a line starting with a dot\r\n"
    let rejected = mail.send(url, {user: "ada", password: "secret", tls: false,
        from: "ada@example.org", to: ["bob@example.org", "nobody@example.org"]}, message)^
    print("rejected ", rejected, "\n")
}
