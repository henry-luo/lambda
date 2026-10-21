// lambda.doc.readability public surface and options (vibe/Lambda_Pkg_Readability.md §2).
// Expected values match Readability.js 0.6.0 run on the same pages.
import rd: lambda.doc.readability

fn page(page_name) => "test/lambda/readability/pages/" ++ page_name ++ "/source.html"
fn source(page_name) => trim(input(page(page_name), 'text')^)
fn hrefs(e) => [for (a in [*(e?<a>)]) a.href]
fn classes(e) => [for (d in [e, *(e?element)] where d.class != null) d.class]
fn iframes(e) => [for (f in [*(e?<iframe>)]) f.src]
let basic = rd.parse(source("basic"), {url: "https://example.com/travel/river.html"})^
let parsed = rd.parse(input(page("basic"), 'html')^)^;

'1. result fields';
[basic.title, basic.byline, basic.excerpt, basic.site_name, basic.lang, basic.dir, basic.published_time, basic.length];
'2. content is the page element; html is its markup';
[name(basic.content), basic.content.id, basic.content.class, starts_with(basic.html, "<div id=\"readability-page-1\" class=\"page\">")];
'3. links resolve against options.url, and stay as written without one';
[hrefs(basic.content), hrefs(parsed.content)];
'4. a parsed document and its source text give the same article';
parsed.text == rd.parse(source("basic"))^.text;
'5. options';
[classes(rd.parse(source("basic"), {keep_classes: true})^.content)[0 to 1],
 classes(rd.parse(source("nested"), {classes_to_preserve: ["story"]})^.content),
 rd.parse(source("basic"), {char_threshold: 5000})^.length,
 rd.parse(source("nested"), {top_candidates: 1})^.length,
 [rd.parse(source("json-ld"), {disable_json_ld: true})^.title, rd.parse(source("json-ld"))^.title],
 iframes(rd.parse(source("media"), {allowed_video: \("ads.example.com")})^.content),
 rd.parse(source("unlikely"), {link_density_modifier: 1})^.length,
 rd.parse(source("basic"), {max_elems: 10}) ^ { "too many elements" }];
'6. readerable, with its thresholds';
let docs = ["<html><p id=\"main\">hello there</p></html>", "<html><p id=\"main\">" ++ join(fill(11, "hello there "), "") ++ "</p></html>",
    "<html><p id=\"main\">" ++ join(fill(12, "hello there "), "") ++ "</p></html>",
    "<html><p id=\"main\">" ++ join(fill(50, "hello there "), "") ++ "</p></html>"];
[for (o in [null, {min_content_length: 120, min_score: 0}, {min_content_length: 0, min_score: 4}])
    [for (d in docs) rd.readerable(d, o)^]];
'7. a page with no content has no article';
rd.parse("<html><body></body></html>")^ == null
