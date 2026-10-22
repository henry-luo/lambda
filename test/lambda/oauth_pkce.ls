// lambda.oauth's pure parts (RFC 7636, RFC 6749): the PKCE challenge, base64url,
// the authorization URL, form bodies, and token replies. The sign-in itself
// runs against a fake provider in test/mail (make test-mail).
import oauth: lambda.oauth

"=== PKCE ===";
// RFC 7636 Appendix B
[oauth.code_challenge("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"), oauth.base64url(b'\xFBFF'), oauth.base64url(b'\x00')]

"=== requests ===";
let provider = {authorize_url: "https://auth.example/authorize", token_url: "https://auth.example/token",
    scope: "mail offline_access", params: {access_type: "offline", prompt: "consent"}}
let client = {id: "client 1", secret: null};
[oauth.authorize_url(provider, client, "http://127.0.0.1:5000", "chal", "st", "ada@example.com"),
 oauth.authorize_url({*: provider, params: null}, client, "http://127.0.0.1:5000", "chal", "st", null),
 oauth.form({grant_type: "authorization_code", code: "a/b c", client_secret: null})]

"=== token replies ===";
oauth.tokens_of(200, "{\"access_token\": \"at\", \"refresh_token\": \"rt\", \"expires_in\": 3599, \"token_type\": \"Bearer\"}")
oauth.tokens_of(400, "{\"error\": \"invalid_grant\", \"error_description\": \"Bad Request\"}")
oauth.tokens_of(401, "{\"error\": \"invalid_client\"}")
oauth.tokens_of(200, "{\"token_type\": \"Bearer\"}")
oauth.tokens_of(502, null);
[oauth.query_value({query: [{name: "code", value: "c1"}, {name: "state", value: "s"}]}, "state"), oauth.query_value({query: []}, "code")]
