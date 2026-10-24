// lambda.mail.providers — the servers an address uses (vibe/Lambda_Demo_Mail.md
// §4.1): presets for the common hosts, Mozilla's ISPDB autoconfig for the
// rest, and the lambda.io.mail URL and options a setting names.
//
// A server is {host, port, security}; security is 'tls' (implicit TLS),
// 'starttls', or 'plain', which lambda.io.mail allows only on loopback.
// `pop3` is null for a host that offers no POP3 (§3). `oauth` names the
// sign-in endpoints for lambda.oauth where the host offers OAuth (M4); the
// app registers its own client with the provider (MQ4).

// `openid email profile` let a sign-in learn the address it was made for
// (userinfo_url), so "Login with Gmail" needs no typing
pub let GOOGLE = {provider: "google", name: "Google", authorize_url: "https://accounts.google.com/o/oauth2/v2/auth",
    token_url: "https://oauth2.googleapis.com/token", scope: "https://mail.google.com/ openid email profile",
    userinfo_url: "https://openidconnect.googleapis.com/v1/userinfo",
    params: {access_type: "offline", prompt: "consent"}}
pub let MICROSOFT = {provider: "microsoft", name: "Microsoft", userinfo_url: null,
    authorize_url: "https://login.microsoftonline.com/common/oauth2/v2.0/authorize",
    token_url: "https://login.microsoftonline.com/common/oauth2/v2.0/token",
    scope: "https://outlook.office.com/IMAP.AccessAsUser.All https://outlook.office.com/POP.AccessAsUser.All " ++
        "https://outlook.office.com/SMTP.Send offline_access",
    params: null}

pub let PRESETS = [
    {id: "gmail", name: "Gmail", domains: ["gmail.com", "googlemail.com"],
     imap: {host: "imap.gmail.com", port: 993, security: 'tls'},
     smtp: {host: "smtp.gmail.com", port: 465, security: 'tls'},
     pop3: {host: "pop.gmail.com", port: 995, security: 'tls'},
     oauth: GOOGLE,
     note: "Sign in with Google, or use an app password: Google Account, Security, App passwords."},
    {id: "icloud", name: "iCloud Mail", domains: ["icloud.com", "me.com", "mac.com"],
     imap: {host: "imap.mail.me.com", port: 993, security: 'tls'},
     smtp: {host: "smtp.mail.me.com", port: 587, security: 'starttls'},
     pop3: null, oauth: null,
     note: "Use an app-specific password from account.apple.com."},
    {id: "fastmail", name: "Fastmail", domains: ["fastmail.com", "fastmail.fm"],
     imap: {host: "imap.fastmail.com", port: 993, security: 'tls'},
     smtp: {host: "smtp.fastmail.com", port: 465, security: 'tls'},
     pop3: {host: "pop.fastmail.com", port: 995, security: 'tls'}, oauth: null,
     note: "Use an app password from Fastmail's Privacy & Security settings."},
    {id: "proton", name: "Proton Mail Bridge", domains: ["proton.me", "protonmail.com", "protonmail.ch", "pm.me"],
     imap: {host: "127.0.0.1", port: 1143, security: 'starttls'},
     smtp: {host: "127.0.0.1", port: 1025, security: 'starttls'},
     pop3: null, oauth: null,
     note: "Proton Mail Bridge must be running. Use the password it shows, and pin its exported certificate."},
    {id: "outlook", name: "Outlook.com", domains: ["outlook.com", "hotmail.com", "live.com", "msn.com"],
     imap: {host: "outlook.office365.com", port: 993, security: 'tls'},
     smtp: {host: "smtp.office365.com", port: 587, security: 'starttls'},
     pop3: {host: "outlook.office365.com", port: 995, security: 'tls'},
     oauth: MICROSOFT,
     note: "Microsoft accounts sign in with Microsoft; passwords are refused."},
    {id: "yahoo", name: "Yahoo Mail", domains: ["yahoo.com", "ymail.com"],
     imap: {host: "imap.mail.yahoo.com", port: 993, security: 'tls'},
     smtp: {host: "smtp.mail.yahoo.com", port: 465, security: 'tls'},
     pop3: {host: "pop.mail.yahoo.com", port: 995, security: 'tls'}, oauth: null,
     note: "Use an app password from Yahoo's account security page."}
]

pub fn domain_of(address) {
    let at = [for (i, ch in split(address or "", "") where ch == "@") i];
    if (len(at) == 0) "" else lower(trim(slice(address, at[len(at) - 1] + 1)))
}

pub fn preset(id) => [for (p in PRESETS where p.id == id) p][0]
pub fn preset_for(address) => [for (p in PRESETS where domain_of(address) in p.domains) p][0]

fn scheme(server, implicit, starttls) => if (server.security == 'tls') implicit else starttls
pub fn imap_url(server) => scheme(server, "imaps", "imap") ++ "://" ++ server.host ++ ":" ++ string(server.port)
pub fn pop3_url(server) => scheme(server, "pop3s", "pop3") ++ "://" ++ server.host ++ ":" ++ string(server.port)
pub fn smtp_url(server) => scheme(server, "smtps", "smtp") ++ "://" ++ server.host ++ ":" ++ string(server.port)

// `tls: false` for a plain-text server, else nothing; a fn, since in a pn
// `else {}` reads as an empty block rather than an empty map
pub fn tls_option(server) => if (server.security == 'plain') {tls: false} else {}

// the options lambda.io.mail open and send take for a server
pub fn session_options(server, user, password, pin) => {user: user, password: password,
    *: (if (pin == null or pin == "") {} else {pin: pin}), *: tls_option(server)}

// ---- ISPDB autoconfig (https://wiki.mozilla.org/Thunderbird:Autoconfiguration) ----

fn child(e, tag) => [for (c in content(e) where c is element and string(name(c)) == tag) c][0]
fn child_text(e, tag) {
    let c = if (e == null) null else child(e, tag);
    if (c == null) null else trim(join([for (t in content(c) where t is string) t], ""))
}
fn servers(provider, tag, kind) => [for (s in content(provider) where s is element and string(name(s)) == tag and s.type == kind) s]

// ISPDB names the address's parts with placeholders
fn username(template, address) {
    let local = slice(address, 0, len(address) - len(domain_of(address)) - 1);
    replace(replace(replace(template or "%EMAILADDRESS%", "%EMAILADDRESS%", address), "%EMAILLOCALPART%", local),
        "%EMAILDOMAIN%", domain_of(address))
}

fn server_of(s) {
    let socket = lower(child_text(s, "socketType") or "");
    {host: child_text(s, "hostname"), port: int(child_text(s, "port")) ^ { 0 },
     security: if (socket == "ssl") 'tls' else if (socket == "starttls") 'starttls' else 'plain'}
}

// a provider's servers of one kind over TLS or STARTTLS: plain text is refused off loopback
fn secure(provider, tag, kind) => if (provider == null) []
    else [for (s in servers(provider, tag, kind) where server_of(s).security != 'plain') s]

// The settings in a clientConfig document, or null when it names neither an
// IMAP nor a POP3 server over TLS or STARTTLS.
pub fn from_autoconfig(doc, address) {
    let config = [for (c in content(doc) where c is element and string(name(c)) == "clientConfig") c][0]
    let provider = if (config == null) null else child(config, "emailProvider")
    let imap = secure(provider, "incomingServer", "imap")
    let pop3 = secure(provider, "incomingServer", "pop3")
    let smtp = secure(provider, "outgoingServer", "smtp")
    let incoming = if (len(imap) > 0) imap[0] else if (len(pop3) > 0) pop3[0] else null;
    if (incoming == null) null
    else {id: "autoconfig", name: child_text(provider, "displayName") or domain_of(address),
          imap: if (len(imap) == 0) null else server_of(imap[0]), smtp: if (len(smtp) == 0) null else server_of(smtp[0]),
          pop3: if (len(pop3) == 0) null else server_of(pop3[0]),
          user: username(child_text(incoming, "username"), address), note: null}
}

// Looks the address's domain up in the ISPDB; null when it has no entry.
pub pn autoconfig(address) map^ {
    let domain = domain_of(address)
    if (domain == "") { return null }
    let reply = io.fetch("https://autoconfig.thunderbird.net/v1.1/" ++ domain, {timeout: 15})^
    if (not reply.ok or not (reply.body is string)) { return null }
    from_autoconfig(parse(reply.body, 'xml')^, address)
}
