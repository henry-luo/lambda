import reader: lambda.doc.latex.tex_engine.reader
let Reader = reader.Reader;
let Token = reader.Token;
let ReadStep = reader.ReadStep;
let CaptureError = reader.CaptureError;
let SourceSpan = reader.SourceSpan;

fn first(source:string) ReadStep? => reader.next(reader.reader(source))
fn first_token(source:string) Token? => first(source).token
fn loaded(source:string) Reader => reader.load_line(reader.reader(source)) or reader.reader("")
fn verb(source:string) SourceSpan | CaptureError => reader.verb_line(loaded(source))
fn group(source:string) SourceSpan => reader.raw_group_command(reader.reader(source))
fn inline(source:string, language:bool) SourceSpan | CaptureError => reader.raw_inline_command(reader.reader(source),language)
fn category(c:string, overrides:map) int => reader.catcode(c,overrides);
fn filtered_capture(value) SourceSpan | string | error =>
 reader.verb_line(value) ^reader.CaptureError { "capture" }^;

[first_token("é").value == "é", first("é").token.end == 2,
 first("") is null, reader.next(null) is null,
 first("% ignored\nA").reader.line_no == 2,
 first("% ignored\nA").token.start == 10];
[category("!",{}) == 12, category("!",{'!':11}) == 11,
 category("!",{'!':"11"}) == 12, category("!",{'!':error("bad category")}) == 12,
 category("!",{'!':nan}) == 12, category("!",{'!':inf}) == 12,
 category("!",{'!':-1}) == 12, category("!",{'!':16}) == 12];
let initial=reader.reader("a!b");
let one=reader.next(initial);
let changed: Reader={*:one.reader,catcodes:{'!':11}};
[initial.position == 0, one.reader.position == 1, reader.next(changed).token.cat == 11,
 reader.next(one.reader).token.cat == 12, reader.next(initial).token.seq == 1,
 reader.next(one.reader).token.seq == 2];
[(verb("|é猫|Z") ^ { null }) != null, (verb("|é猫|Z")^).end == 7,
 (verb("|é猫|Z")^).reader.position == 4, (verb("|é猫|Z")^).reader.position_byte == 7,
 (verb("|unterminated") ^ { ^.message }) == "verb unterminated",
 (verb("§abc§") ^ { ^.message }) == "verb delimiter", (verb("") ^ { ^.message }) == "verb delimiter",
 group("{a{b}c}Z").end == 7, group("{a\\}b}Z").end == 6,
 (inline("{python}|é猫|Z",true)^).end == 15,
 (inline("§abc§",false) ^ { ^.message }) == "raw inline delimiter"];
// Incomplete optional/group/delimited input retains the reader's existing endpoints.
[(inline("[abc",false)^).end == 4, (inline("{abc",false)^).end == 4,
 (inline("|abc\nZ",false)^).end == 4, group("{abc").end == 4];
[error("verb delimiter") ^reader.CaptureError { true } ^ { false },
 error("verb unterminated") ^reader.CaptureError { true } ^ { false },
 error("raw inline delimiter") ^reader.CaptureError { true } ^ { false },
 error("other") ^reader.CaptureError { false } ^ { true }];
[(filtered_capture(loaded("|bad"))^ == "capture"),
 ((filtered_capture(42) ^ { "propagated" }) == "propagated")];
let supers: Reader={*:reader.reader("^^a ^^41 ^^"),catcodes:{'^':7}};
let super_first=reader.next(supers);
[super_first.token.value == "!", super_first.token.end == 3,
 reader.next({*:supers,preserve_source:true}).token.value == "^"];
let long=join([for(i in 1 to 4096) "a"], "");
[group("{" ++ long ++ "}Z").end == 4098,
 (inline("|" ++ long ++ "|Z",false)^).end == 4098,
 (verb("|" ++ long ++ "|Z")^).end == 4098,
 first("\\" ++ long).token.value == long]
