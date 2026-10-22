// OAuth tokens, refused logins and refused targets for lambda.io.mail.
import mail: lambda.io.mail

pn main() {
    let ports = input("temp/mail/test_ports.json")^
    let url = "imap://127.0.0.1:" ++ string(ports.imap)
    let s = mail.open(url, {user: "ada", token: "tok-123", tls: false})^
    let r = mail.command(s, "NOOP")^
    print("token ", r.status, "\n")
    // the fully qualified spelling reaches the same module
    let q = lambda.io.mail.open(url, {user: "ada", password: "secret", tls: false})^
    let qr = mail.command(q, "NOOP")^
    print("qualified ", qr.status, "\n")
    // a refused login carries the server's own reason
    mail.open(url, {user: "ada", password: "wrong", tls: false}) ^ { print("denied: ", ^.message, "\n") }
    mail.open("pop3://127.0.0.1:" ++ string(ports.pop3), {user: "ada", password: "wrong", tls: false}) ^ {
        print("pop3 denied: ", ^.message, "\n")
    }
    mail.send("smtp://127.0.0.1:" ++ string(ports.smtp), {user: "ada", password: "wrong", tls: false,
        from: "ada@example.org", to: ["bob@example.org"]}, "Subject: x\r\n\r\nx\r\n") ^ { print("smtp denied: ", ^.message, "\n") }
    // credentials never travel without TLS to another machine
    mail.open("imap://mail.example.com", {user: "ada", password: "secret", tls: false}) ^ {
        print("refused: ", ^.message, "\n")
    }
    mail.open("http://127.0.0.1:1", {user: "ada", password: "secret"}) ^ { print("scheme: ", ^.message, "\n") }
    mail.open(url, {user: "ada", tls: false}) ^ { print("missing: ", ^.message, "\n") }
}
