// lambda.mail.providers (vibe/Lambda_Demo_Mail.md §4.1): presets by domain,
// URLs and options for lambda.io.mail, and ISPDB autoconfig documents.
import prov: lambda.mail.providers

"=== presets ===";
[for (p in prov.PRESETS) [p.id, p.domains, p.pop3 != null]];
[prov.domain_of("Ada@Example.COM"), prov.domain_of("no-at-sign"), prov.domain_of("a@b@c.example")];
[prov.preset_for("ada@gmail.com").id, prov.preset_for("ada@pm.me").id, prov.preset_for("x@nowhere.test")]

"=== urls and options ===";
[prov.imap_url(prov.preset("gmail").imap), prov.imap_url(prov.preset("proton").imap), prov.smtp_url(prov.preset("icloud").smtp),
 prov.pop3_url(prov.preset("gmail").pop3), prov.pop3_url({host: "127.0.0.1", port: 1110, security: 'plain'})]
prov.session_options({host: "127.0.0.1", port: 1143, security: 'plain'}, "ada", "pw", null)
prov.session_options(prov.preset("proton").imap, "ada", "pw", "sha256//abc=")

"=== autoconfig ==="
let doc = parse("<clientConfig version=\"1.1\"><emailProvider id=\"example.com\"><domain>example.com</domain><displayName>Example Mail</displayName><incomingServer type=\"pop3\"><hostname>pop.example.com</hostname><port>995</port><socketType>SSL</socketType></incomingServer><incomingServer type=\"imap\"><hostname>imap.example.com</hostname><port>993</port><socketType>SSL</socketType><username>%EMAILLOCALPART%</username></incomingServer><outgoingServer type=\"smtp\"><hostname>smtp.example.com</hostname><port>587</port><socketType>STARTTLS</socketType><username>%EMAILADDRESS%</username></outgoingServer></emailProvider></clientConfig>", 'xml')^
let plain = parse("<clientConfig><emailProvider id=\"plain.example\"><incomingServer type=\"imap\"><hostname>imap.plain.example</hostname><port>143</port><socketType>plain</socketType></incomingServer></emailProvider></clientConfig>", 'xml')^;
prov.from_autoconfig(doc, "ada@example.com")
prov.from_autoconfig(plain, "ada@plain.example")
let pop_only = parse("<clientConfig><emailProvider id=\"pop.example\"><displayName>Pop Only</displayName><incomingServer type=\"pop3\"><hostname>pop.pop.example</hostname><port>995</port><socketType>SSL</socketType><username>%EMAILADDRESS%</username></incomingServer></emailProvider></clientConfig>", 'xml')^;
prov.from_autoconfig(pop_only, "ada@pop.example")
