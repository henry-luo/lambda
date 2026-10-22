// The mail demo's "Login with Gmail" (test/demo/mail/mod_signin.ls) against the
// fake provider of test_mail.py: the address comes from userinfo, an account
// for the same address turns to OAuth, and the refresh token lands in a
// test-only Keychain service, removed after.
import secret: lambda.io.secret
import signin: ~~.demo.mail.mod_signin

let SERVICE = "Lambda Mail (signin test)"

pn browser(url) { io.fetch(url) ^ { print("browser failed: ", ^.message, "\n") } }

pn main() {
    let ports = input("temp/mail/test_ports.json")^
    let base = "http://127.0.0.1:" ++ string(ports.oauth)
    let endpoints = {provider: "google", name: "Google", authorize_url: base ++ "/authorize", token_url: base ++ "/token",
        userinfo_url: base ++ "/userinfo", scope: "https://mail.example/ openid email profile", params: null}
    let client = {id: "lambda-test", secret: "s3cret"}
    let show = pn (url) => { start(browser, [url]) }
    let existing = [{id: "ada-example-org", name: "Ada (password)", email: "Ada@Example.org", user: "ada",
        provider: "manual", protocol: "imap", imap: {host: "127.0.0.1", port: 1, security: 'plain'}, pop3: null,
        smtp: null, pin: null, auth: "password", oauth: null}]
    let made = signin.sign_in_gmail(SERVICE, endpoints, client, existing, {open: show, seconds: 20})^
    let a = made.account
    print("replaced ", a.id, " ", a.email, " ", a.name, " ", a.auth, " ", a.provider, " ", a.imap.host, " ", made.replaces, "\n")
    print("token ", secret.get(SERVICE, a.id)^, "\n")
    let fresh = signin.sign_in_gmail(SERVICE, endpoints, client, [], {open: show, seconds: 20})^
    print("new ", fresh.account.id, " ", fresh.account.name, " ", fresh.replaces, "\n")
    print("deleted ", secret.delete(SERVICE, a.id)^, "\n")
}
