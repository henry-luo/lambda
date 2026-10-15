// Authored record values exercise field serialization, beyond retained-source replay.
import writers: lambda.doc.misc

fn stable(source,target) => writers.format(parse(source,target)^,{type:target})^ == source
fn wire_entry(value,wanted) => [for (entry in value.entries where entry.name == wanted) entry][0]

let contact_source = writers.vcf({
    full_name:"Ada Café 中文 😀",
    name:{family:"Example",given:"Ada"},
    email:["ada@example.com","ada@home.example"],
    note:"First line\nSecond, semicolon; and slash\\.",
    address:{street:"1 Main Street",city:"Singapore",country:"Singapore"}
})^
let contact = parse(contact_source,"vcf")^
let calendar_source = writers.ics({components:[{
    type:"VEVENT",uid:"event@example.com",stamp:"20261015T000000Z",
    start_time:{year:2026,month:10,day:16,hour:9,minute:30,second:0,timezone:"UTC"},
    duration:{hours:1,minutes:30},summary:"Planning, café 中文 😀",
    components:[{type:"VALARM",action:"DISPLAY",trigger:"-PT15M",description:"Reminder"}]
}]})^
let calendar = parse(calendar_source,"ics")^
let event = calendar.components[0]
let message_source = writers.eml({
    from:{name:"Ada Café",address:"ada@example.com"},
    to:["team@example.com","bob@example.com"],
    date:"Thu, 15 Oct 2026 00:00:00 +0000",subject:"Café 中文 😀",
    body:{type:"text/plain; charset=UTF-8",content:"Hello café 中文 😀.\n",encoding:"quoted-printable"},
    attachments:[{filename:"sample.bin",content:b'\64AAECA/7/'}]
})^
let message = parse(message_source,"eml")^
let document = parse("A **bold** word.\n","markdown")^
let loss_document = <doc <body <p <unknown "content">>>>
let report = writers.format_report(loss_document,{type:"typst"})^;

{
    vcf:[stable(contact_source,"vcf"),contact.full_name == "Ada Café 中文 😀",
        [for (entry in contact.entries where entry.name == "email") entry.value] == ["ada@example.com","ada@home.example"],
        contact.name == {family:"Example",given:"Ada"},
        contact.properties.note == "First line\\nSecond\\, semicolon\\; and slash\\\\."],
    ics:[stable(calendar_source,"ics"),event.start_time.timezone == "UTC",
        event.start_time.hour == "09",event.duration.hours == "1",
        event.duration.minutes == "30",wire_entry(event,"SUMMARY").value == "Planning\\, café 中文 😀",
        wire_entry(event,"VALARM").value.type == "VALARM"],
    eml:[stable(message_source,"eml"),len(message.header_entries) >= 6,
        contains(message.raw_body,"content-type: application/octet-stream"),
        contains(message.raw_body,"AAECA/7/"),contains(message.headers.subject,"=?UTF-8?B?")],
    routes:[for (target in writers.formats)
        if (target in ["asciidoc","man","typst"]) format(document,target)^ == writers.format(document,{type:target})^
        else if (target == "vcf") format(contact,target)^ == contact_source
        else if (target == "ics") format(calendar,target)^ == calendar_source
        else format(message,target)^ == message_source],
    validation:[
        (writers.format(document,"invalid") ^ { ^.code }) == 7802,
        (writers.format(document,{unknown:true}) ^ { ^.code }) == 7802,
        (writers.vcf({}) ^ { ^.code }) == 7802,
        (writers.ics({components:[{type:"VEVENT"}]}) ^ { ^.code }) == 7802,
        (writers.eml({from:"ada@example.com",date:"bad\nheader",body:"text"}) ^ { ^.code }) == 7802,
        (writers.format(loss_document,{type:"typst"}) ^ { ^.code }) == 7802,
        report.diagnostics[0].code == "unsupported-element",
        writers.format(loss_document,{type:"typst",loss:"allow"})^ == report.text]
}
