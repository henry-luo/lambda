// lambda.oauth — OAuth 2.0 sign-in for native apps (RFC 8252) with PKCE
// (RFC 7636): the system browser shows the provider's sign-in page, a
// loopback listener receives the redirect, and the code becomes tokens
// (vibe/Lambda_Demo_Mail.md M4, MG7).
//
// A provider is {authorize_url, token_url, scope, params}, where params are
// extra authorization parameters (Google's access_type, say). A client is
// {id, secret}: the app's registration with the provider. Google issues a
// secret even to desktop clients and requires it; a Microsoft public client
// has none, and a null secret is not sent.
import loopback: lambda.io.loopback

// the page the browser shows once the sign-in has reached the app
let DONE_PAGE = "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>Signed in</title></head>" ++
    "<body style=\"font:16px -apple-system,sans-serif;margin:3em\"><h1>Signed in</h1>" ++
    "<p>You can close this window and return to Lambda.</p></body></html>"

// base64url without padding (RFC 4648 §5); JSON writes binary as standard base64
pub fn base64url(bytes) {
    let json = format(bytes, 'json')
    let standard = slice(json, 1, len(json) - 1);
    replace(replace(replace(standard, "+", "-"), "/", "_"), "=", "")
}

// RFC 7636 §4.2: S256 = BASE64URL(SHA256(ASCII(code_verifier)))
pub fn code_challenge(verifier) => base64url(hash(verifier, 'sha256'))

// fields as a query string or form body; null fields are left out
pub fn form(fields) => join([for (k, v at fields where v != null) url_encode(string(k)) ++ "=" ++ url_encode(string(v))], "&")

pub fn authorize_url(provider, client, redirect_uri, challenge, nonce, hint) =>
    provider.authorize_url ++ "?" ++ form({response_type: "code", client_id: client.id, redirect_uri: redirect_uri,
        scope: provider.scope, state: nonce, code_challenge: challenge, code_challenge_method: "S256",
        login_hint: hint, *: (if (provider.params == null) {} else provider.params)})

// a query item of the redirect lambda.io.loopback received, or null
pub fn query_value(request, name) => [for (q in request.query where q.name == name) q.value][0]

// {access_token, refresh_token, expires_in, scope} from a token endpoint's
// JSON reply, or {error} with the provider's reason
pub fn tokens_of(status, body) {
    let data = if (body is string) parse(body, 'json') ^ { null } else null;
    if (data != null and status >= 200 and status < 300 and data.access_token != null)
        {access_token: data.access_token, refresh_token: data.refresh_token, expires_in: data.expires_in, scope: data.scope}
    else {error: if (data != null and data.error != null)
        string(data.error) ++ (if (data.error_description != null) ": " ++ string(data.error_description) else "")
        else "status " ++ string(status)}
}

pn token_request(provider, client, fields) map^ {
    let body = form({*: fields, client_id: client.id, client_secret: client.secret})
    let reply = io.fetch(provider.token_url, {method: "POST", body: body, timeout: 30,
        headers: {'content-type': "application/x-www-form-urlencoded", accept: "application/json"}})^
    let tokens = tokens_of(reply.status, reply.body)
    if (tokens.error != null) { raise error("oauth: the token request failed: " ++ tokens.error) }
    tokens
}

// the system browser: open on macOS, start on Windows, xdg-open elsewhere
pub pn open_browser(url) null^ {
    let platform = sys.os.platform#
    if (platform == "darwin") { io.exec("open", [url])^ }
    else if (platform == "windows") { io.exec("cmd", ["/c", "start", "", url])^ }
    else { io.exec("xdg-open", [url])^ }
    null
}

// Signs in through the provider's page and returns the tokens. options:
// login_hint (the address to sign in as), seconds (the wait for the person,
// default 300), and open (a pn that shows the page; default the system
// browser). A refusal or a mismatched reply raises.
pub pn sign_in(provider, client, options) map^ {
    let listener = loopback.listen()^
    let redirect_uri = "http://127.0.0.1:" ++ string(listener.port)
    // 64 hex characters of the 43–128 unreserved ones RFC 7636 §4.1 asks for
    let verifier = replace(uuid()^ ++ uuid()^, "-", "")
    // the `state` parameter: a reply must carry it back (RFC 6749 §10.12)
    let nonce = uuid()^
    let url = authorize_url(provider, client, redirect_uri, code_challenge(verifier), nonce, options.login_hint)
    if (options.open != null) { options.open(url) } else { open_browser(url)^ }
    let request = loopback.accept(listener, if (options.seconds == null) 300 else options.seconds, DONE_PAGE)^
    if (query_value(request, "state") != nonce) { raise error("oauth: the sign-in reply does not match its request") }
    let refused = query_value(request, "error")
    if (refused != null) {
        let detail = query_value(request, "error_description")
        raise error("oauth: sign-in refused: " ++ refused ++ (if (detail != null) ": " ++ detail else ""))
    }
    let code = query_value(request, "code")
    if (code == null) { raise error("oauth: the sign-in reply carries no code") }
    token_request(provider, client, {grant_type: "authorization_code", code: code, redirect_uri: redirect_uri,
        code_verifier: verifier})
}

// Who signed in, from the provider's OpenID Connect userinfo endpoint
// (provider.userinfo_url) with an access token whose scope includes `openid`
// and `email`: {email, name, …} as the provider sends it.
pub pn userinfo(provider, access_token) map^ {
    if (provider.userinfo_url == null) { raise error("oauth: the provider names no userinfo endpoint") }
    let reply = io.fetch(provider.userinfo_url, {timeout: 30,
        headers: {authorization: "Bearer " ++ access_token, accept: "application/json"}})^
    let info = if (reply.ok and reply.body is string) parse(reply.body, 'json') ^ { null } else null
    if (info == null or info.email == null) { raise error("oauth: the userinfo request failed: status " ++ string(reply.status)) }
    info
}

// New tokens for a refresh token. The reply may omit refresh_token (Google
// keeps the old one valid) or carry a new one (Microsoft rotates it).
pub pn refresh(provider, client, refresh_token) map^ {
    token_request(provider, client, {grant_type: "refresh_token", refresh_token: refresh_token})
}
