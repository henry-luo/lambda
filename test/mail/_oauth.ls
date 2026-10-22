// lambda.oauth against the fake provider of test_mail.py. The "browser" is a
// task that follows the provider's redirect into the app's loopback listener.
// Then a refresh, refusals, and an IMAP login with the access token.
import oauth: lambda.oauth
import mail: lambda.io.mail

pn browser(url) { io.fetch(url) ^ { print("browser failed: ", ^.message, "\n") } }

pn main() {
    let ports = input("temp/mail/test_ports.json")^
    let base = "http://127.0.0.1:" ++ string(ports.oauth)
    let provider = {authorize_url: base ++ "/authorize", token_url: base ++ "/token",
        scope: "https://mail.example/", params: {access_type: "offline"}}
    let client = {id: "lambda-test", secret: "s3cret"}
    let show = pn (url) => { start(browser, [url]) }
    let tokens = oauth.sign_in(provider, client, {login_hint: "ada", seconds: 20, open: show})^
    print("signed in ", tokens.access_token, " ", tokens.refresh_token, " ", tokens.expires_in, "\n")
    let info = oauth.userinfo({*: provider, userinfo_url: base ++ "/userinfo"}, tokens.access_token)^
    print("userinfo ", info.email, " ", info.name, "\n")
    oauth.userinfo({*: provider, userinfo_url: base ++ "/userinfo"}, "bad") ^ { print("bad token: ", ^.message, "\n") }
    let renewed = oauth.refresh(provider, client, tokens.refresh_token)^
    print("refreshed ", renewed.access_token, " ", renewed.refresh_token, "\n")
    oauth.refresh(provider, client, "stale") ^ { print("stale: ", ^.message, "\n") }
    oauth.sign_in(provider, client, {login_hint: "refuse", seconds: 20, open: show}) ^ { print("refused: ", ^.message, "\n") }
    oauth.sign_in(provider, {id: "lambda-test", secret: "wrong"}, {seconds: 20, open: show}) ^ { print("client: ", ^.message, "\n") }
    let s = mail.open("imap://127.0.0.1:" ++ string(ports.imap), {user: "ada", token: renewed.access_token, tls: false})^
    print("imap ", mail.command(s, "NOOP")^.status, "\n")
}
