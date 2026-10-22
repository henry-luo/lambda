// The mail demo's OAuth sign-ins (vibe/Lambda_Demo_Mail.md M4). Each runs in a
// task of its own, off the window's turn, and stores the refresh token in the
// Keychain itself, so a token never passes through the window's events.
import secret: lambda.io.secret
import oauth: lambda.oauth
import model: .mod_model

pn stored(service, account, tokens) map^ {
    if (tokens.refresh_token == null) { raise error(account.oauth.name ++ " returned no refresh token; sign in again.") }
    secret.set(service, account.id, tokens.refresh_token)^
    account
}

// Signs in to an account the sheet described, as its address.
pub pn sign_in_account(service, account, client, options) map^ {
    let tokens = oauth.sign_in(account.oauth, client, {*: options, login_hint: account.email})^
    stored(service, account, tokens)
}

// "Login with Gmail": signs in as whoever the person chooses on Google's page,
// learns the address from Google (userinfo), and returns {account, replaces}.
// An account for the same address turns to OAuth rather than being added again.
pub pn sign_in_gmail(service, endpoints, client, accounts, options) map^ {
    let tokens = oauth.sign_in(endpoints, client, options)^
    let info = oauth.userinfo(endpoints, tokens.access_token)^
    let account = model.gmail_account(info.email, info.name, accounts, endpoints)
    let saved = stored(service, account, tokens)^;
    {account: saved, replaces: len([for (a in accounts where a.id == account.id) a]) > 0}
}
