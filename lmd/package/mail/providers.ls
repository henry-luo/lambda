// lambda.mail.providers — the servers an address uses (vibe/Lambda_Demo_Mail.md
// §4.1): presets for the common hosts, Mozilla's ISPDB autoconfig for the
// rest, and the lambda.io.mail URL and options a setting names.
//
// A server is {host, port, security}; security is 'tls' (implicit TLS),
// 'starttls', or 'plain', which lambda.io.mail allows only on loopback.

pub let PRESETS = [
    {id: "gmail", name: "Gmail", domains: ["gmail.com", "googlemail.com"],
     imap: {host: "imap.gmail.com", port: 993, security: 'tls'},
     smtp: {host: "smtp.gmail.com", port: 465, security: 'tls'},
     note: "Use an app password: Google Account, Security, App passwords."},
    {id: "icloud", name: "iCloud Mail", domains: ["icloud.com", "me.com", "mac.com"],
     imap: {host: "imap.mail.me.com", port: 993, security: 'tls'},
     smtp: {host: "smtp.mail.me.com", port: 587, security: 'starttls'},
     note: "Use an app-specific password from account.apple.com."},
    {id: "fastmail", name: "Fastmail", domains: ["fastmail.com", "fastmail.fm"],
     imap: {host: "imap.fastmail.com", port: 993, security: 'tls'},
     smtp: {host: "smtp.fastmail.com", port: 465, security: 'tls'},
     note: "Use an app password from Fastmail's Privacy & Security settings."},
    {id: "proton", name: "Proton Mail Bridge", domains: ["proton.me", "protonmail.com", "protonmail.ch", "pm.me"],
     imap: {host: "127.0.0.1", port: 1143, security: 'starttls'},
     smtp: {host: "127.0.0.1", port: 1025, security: 'starttls'},
     note: "Proton Mail Bridge must be running. Use the password and the certificate pin it shows."},
    {id: "outlook", name: "Outlook.com", domains: ["outlook.com", "hotmail.com", "live.com", "msn.com"],
     imap: {host: "outlook.office365.com", port: 993, security: 'tls'},
     smtp: {host: "smtp.office365.com", port: 587, security: 'starttls'},
     note: "Microsoft accounts need OAuth sign-in, which a later milestone adds."},
    {id: "yahoo", name: "Yahoo Mail", domains: ["yahoo.com", "ymail.com"],
     imap: {host: "imap.mail.yahoo.com", port: 993, security: 'tls'},
     smtp: {host: "smtp.mail.yahoo.com", port: 465, security: 'tls'},
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
pub fn smtp_url(server) => scheme(server, "smtps", "smtp") ++ "://" ++ server.host ++ ":" ++ string(server.port)

// the options lambda.io.mail open and send take for a server
pub fn session_options(server, user, password, pin) => {user: user, password: password,
    *: (if (pin == null or pin == "") {} else {pin: pin}),
    *: (if (server.security == 'plain') {tls: false} else {})}

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

// The settings in a clientConfig document, or null when it names no IMAP
// server over TLS or STARTTLS: plain text is refused off loopback.
pub fn from_autoconfig(doc, address) {
    let config = [for (c in content(doc) where c is element and string(name(c)) == "clientConfig") c][0]
    let provider = if (config == null) null else child(config, "emailProvider")
    let imap = if (provider == null) [] else [for (s in servers(provider, "incomingServer", "imap")
        where server_of(s).security != 'plain') s]
    let smtp = if (provider == null) [] else [for (s in servers(provider, "outgoingServer", "smtp")
        where server_of(s).security != 'plain') s];
    if (len(imap) == 0) null
    else {id: "autoconfig", name: child_text(provider, "displayName") or domain_of(address),
          imap: server_of(imap[0]), smtp: if (len(smtp) == 0) null else server_of(smtp[0]),
          user: username(child_text(imap[0], "username"), address), note: null}
}

// Looks the address's domain up in the ISPDB; null when it has no entry.
pub pn autoconfig(address) map^ {
    let domain = domain_of(address)
    if (domain == "") { return null }
    let reply = io.fetch("https://autoconfig.thunderbird.net/v1.1/" ++ domain, {timeout: 15})^
    if (not reply.ok or not (reply.body is string)) { return null }
    from_autoconfig(parse(reply.body, 'xml')^, address)
}
