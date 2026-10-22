// The mail demo's account task (test/demo/mail/mod_sync.ls) for an OAuth
// account against the fake servers of test_mail.py: it trades the saved
// refresh token for an access token, logs in to IMAP with it, and syncs the
// Inbox. The refresh token goes to a test-only Keychain service, removed after.
import secret: lambda.io.secret
import sync: ~~.demo.mail.mod_sync

let SERVICE = "Lambda Mail (oauth test)"

pn main() {
    let ports = input("temp/mail/test_ports.json")^
    let base = "http://127.0.0.1:" ++ string(ports.oauth)
    let account = {id: "ada-oauth", name: "Ada", email: "ada@example.org", user: "ada", provider: "manual",
        protocol: "imap", imap: {host: "127.0.0.1", port: ports.imap, security: 'plain'}, pop3: null, smtp: null,
        pin: null, auth: "oauth",
        oauth: {provider: "fake", name: "Fake", authorize_url: base ++ "/authorize", token_url: base ++ "/token",
            scope: "https://mail.example/", params: null}}
    let client = {id: "lambda-test", secret: "s3cret"}
    secret.set(SERVICE, account.id, "ref-1")^
    let me = self()
    let task = start(sync.run, [account, "temp/mail/oauth-store", SERVICE, null, client, pn (m) => { send(me, m) ^ { null } }])
    send(task, {op: 'select', path: "INBOX"})^
    var waiting = true
    while (waiting) {
        let m = receive()^
        if (m.type == "status") { print("status ", m.state, " ", m.text, "\n") }
        if (m.type == "index") { print("index ", m.path, " ", len(m.entries), "\n") }
        waiting = not (m.type == "index" or (m.type == "status" and m.state == "failed"))
    }
    cancel(task)
    print("deleted ", secret.delete(SERVICE, account.id)^, "\n")
}
