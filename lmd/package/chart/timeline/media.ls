// provider resolution is pure; loading/player operations belong to the selected host.
import c: .common
import util: lambda.chart.util
import url: lambda.dom.urlencode
import radiant

let providers=[
    {id:"youtube",domains:["youtube.com","youtu.be","youtube-nocookie.com"],kind:"video"},
    {id:"vimeo",domains:["vimeo.com"],kind:"video"},
    {id:"dailymotion",domains:["dailymotion.com","dai.ly"],kind:"video"},
    {id:"wistia",domains:["wistia.com","wistia.net"],kind:"video"},
    {id:"soundcloud",domains:["soundcloud.com"],kind:"audio"},
    {id:"spotify",domains:["spotify.com"],kind:"audio"},
    {id:"googlemaps",domains:["maps.google.com","google.com"],kind:"map"},
    {id:"googledoc",domains:["docs.google.com","drive.google.com"],kind:"document"},
    {id:"documentcloud",domains:["documentcloud.org"],kind:"document"},
    {id:"twitter",domains:["twitter.com","x.com"],kind:"social"},
    {id:"instagram",domains:["instagram.com","instagr.am"],kind:"social"},
    {id:"tiktok",domains:["tiktok.com"],kind:"social"},
    {id:"bluesky",domains:["bsky.app"],kind:"social"},
    {id:"flickr",domains:["flickr.com","flic.kr"],kind:"image"},
    {id:"imgur",domains:["imgur.com"],kind:"image"},
    {id:"wikipedia",domains:["wikipedia.org"],kind:"document"},
    {id:"wikimedia",domains:["wikimedia.org"],kind:"image"},
    {id:"vine",domains:["vine.co"],kind:"video",retired:true}]

pub fn catalog() => providers
fn address(src) {
    let pieces=split(src,"/");
    let absolute=starts_with(src,"https://") or starts_with(src,"http://") or starts_with(src,"//");
    let host=if (absolute) lower(pieces[2]) else "";
    let location=split(split(src,"?")[0],"#")[0];
    let path=if (absolute) join(slice(split(location,"/"),3),"/") else location;
    let queries=split(split(src,"?")[1],"&");
    {host:host,path:path,segments:split(path,"/"),query:map([for (pair in queries,
        let fields=split(pair,"=") where len(fields)>1) (fields[0],join(slice(fields,1),"="))]),absolute:absolute}
}
fn domain(host, suffix) bool | error => host==suffix or ends_with(host,"." ++ suffix)
pub fn identify(record) {
    let source=c.text(record.src);
    let uri=address(source);
    let custom=record.provider;
    let candidates=[for (provider in providers where any([for (suffix in provider.domains) domain(uri.host,suffix)]) and
        (provider.id!="googlemaps" or uri.host=="maps.google.com" or starts_with(uri.path,"maps"))) provider];
    let provider=if (custom!=null) c.text(custom) else if (starts_with(source,"spotify:")) "spotify" else candidates[0].id;
    let extension=lower(split(uri.path,".")[len(split(uri.path,"."))-1]);
    let kind=if (record.kind!=null and c.text(record.kind)!="auto" and c.text(record.kind)!="embed") c.text(record.kind)
        else if (starts_with(source,"data:image/") or contains(["jpg","jpeg","png","gif","svg","webp","avif","bmp"],extension)) "image"
        else if (contains(["mp4","webm","ogv","mov","m4v"],extension)) "video"
        else if (contains(["mp3","wav","ogg","oga","m4a","aac","flac"],extension)) "audio"
        else if (extension=="pdf") "document" else if (provider!=null) c.option(c.attrs((providers |: ~.id==provider)[0]),"kind","embed")
        else if (record.content!=null) "custom" else "embed";
    {*:record,kind:kind,provider:provider,address:uri}
}
fn leaf(uri) => (uri.segments |: ~!="")[len(uri.segments |: ~!="")-1]
fn youtube_id(uri) => if (uri.query.v!=null) uri.query.v else if (uri.segments[0]=="watch") null else leaf(uri)
fn query(record, fields) => join([for (key in fields where record[key]!=null)
    key ++ "=" ++ url.form_encode(c.text(record[key]))],"&")

pub fn embed_url(record) {
    let media=identify(record);
    let uri=media.address;
    let provider=media.provider;
    if (provider=="youtube") {
        let id=youtube_id(uri);
        if (id==null or id=="") c.fail("media.src","YouTube video ID is missing") else
        "https://www.youtube-nocookie.com/embed/" ++ id ++ "?" ++ query({start:c.option(record,"start",uri.query.start),end:record.end,
            autoplay:if (record.autoplay) 1 else 0,mute:if (record.muted) 1 else 0,controls:if (record.controls==false) 0 else 1},["start","end","autoplay","mute","controls"])
    } else if (provider=="vimeo") "https://player.vimeo.com/video/" ++ leaf(uri)
    else if (provider=="dailymotion") "https://www.dailymotion.com/embed/video/" ++ leaf(uri)
    else if (provider=="wistia") "https://fast.wistia.net/embed/iframe/" ++ leaf(uri)
    else if (provider=="soundcloud") "https://w.soundcloud.com/player/?url=" ++ url.form_encode(record.src)
    else if (provider=="spotify") "https://open.spotify.com/embed/" ++
        (if (starts_with(record.src,"spotify:")) replace(slice(record.src,8),":","/") else replace(uri.path,"embed/",""))
    else if (provider=="googlemaps") record.src ++ (if (contains(record.src,"?")) "&" else "?") ++ "output=embed"
    else if (provider=="googledoc") if (contains(uri.path,"/pub") or contains(uri.path,"/preview")) record.src
        else split(split(record.src,"/edit")[0],"?")[0] ++ "/preview"
    else if (provider=="documentcloud") split(record.src,"?")[0] ++ (if (ends_with(uri.path,".html")) "" else ".html")
    else if (provider=="instagram") split(record.src,"?")[0] ++ (if (ends_with(uri.path,"/")) "" else "/") ++ "embed/"
    else if (provider=="twitter") "https://platform.twitter.com/embed/Tweet.html?id=" ++ leaf(uri)
    else if (provider=="tiktok") "https://www.tiktok.com/embed/v2/" ++ leaf(uri)
    else if (provider=="imgur") "https://imgur.com/" ++ leaf(uri) ++ "/embed"
    else record.src
}

pub fn validate(record) {
    let value=identify(record);
    let failure=util.first_error([
        if (record.id!=null) c.id(record.id),
        if (not contains(["image","video","audio","embed","map","document","social","custom","text","quote"],value.kind)) c.fail("media.kind","unsupported media kind"),
        if (not contains(["custom","text","quote"],value.kind) and (not (record.src is string) or record.src=="")) c.fail("media.src","expected a nonempty source"),
        for (key in ["width","height"] where record[key]!=null) if (not c.positive(record[key])) c.fail("media." ++ key,"expected positive pixels"),
        for (key in ["start","end"] where record[key]!=null) if (not util.finite_number(record[key]) or record[key]<0) c.fail("media." ++ key,"expected nonnegative seconds"),
        if (record.start!=null and record.end!=null and record.end<record.start) c.fail("media.end","precedes start"),
        if (record.fit!=null and not c.enum(record.fit,["contain","cover","fill","none","scale-down"])) c.fail("media.fit","invalid object fit"),
        if (starts_with(lower(c.text(record.src)),"javascript:") or starts_with(lower(c.text(record.src)),"vbscript:")) c.fail("media.src","unsupported URL scheme")]);
    if (failure is error) failure else record
}

pub fn capabilities(host = "native") {
    let kind=if (host is map) c.text(host.kind) else c.text(host);
    if (not contains(["native","browser","static","svg"],kind)) c.fail("capabilities.host","unknown host") else
    {host:kind,images:true,rich_html:kind!="svg",video:if (kind=="browser") {sources:["local","remote"],controls:true}
        else if (kind=="native") {platforms:["macos"],sources:["local"],controls:true,poster:false} else false,
        audio:if (kind=="browser") {sources:["local","remote"],controls:true} else if (kind=="native") {platforms:["macos"],sources:["local"],controls:true} else false,
        providers:[for (provider in providers) {id:provider.id,status:if (provider.retired) "retired" else if (kind=="browser") "embed" else "external_player_required"}],
        fullscreen:kind=="browser",lightbox:kind!="svg",interactive:contains(["native","browser"],kind),
        exports:["html","svg","png","jpeg","pdf","json","csv","xlsx"]}
}

// Static media descriptors serve exports and outgoing cards without a second player.
pub fn snapshot(record, event_id, index, options = {}, frozen = null) {
    let identified=identify(record);
    let key=util.binding_key(["media",event_id,c.option(record,"id",string(index))]);
    let entry=frozen.resources[key];
    let frame=options.media_frames[event_id][index];
    let candidate=if (frame!=null) frame else if (identified.kind=="image" and identified.provider==null) record.src
        else if (record.poster!=null) record.poster else record.thumbnail;
    let captured=if (frame is binary) frame else if (identified.kind=="image" and entry.status=="ready" and entry.result is binary) entry.result else null;
    let facts=if (captured!=null) radiant.image_metrics(captured) else null;
    let mime=if (identified.kind=="image" and record.mime!=null) record.mime else facts.mime_type;
    let encoded=if (captured!=null) format(captured,'json') else null;
    let source=if (encoded is string and mime!=null) "data:" ++ c.text(mime) ++ ";base64," ++ slice(encoded,1,len(encoded)-1) else candidate;
    let remote=starts_with(c.text(source),"https://") or starts_with(c.text(source),"http://") or starts_with(c.text(source),"//");
    let policy=c.text(c.option(options,"unsupported","error"));
    let attributes=map([for (key,value in c.attrs(record) where value!=null and
        contains(["caption","credit","width","height","fit","link"],key)) (key,value)]);
    if (captured!=null and mime==null) c.fail("export.media","recorded bytes require a supported image or authored image MIME type")
    else if (source!=null and (not remote or options.resources=="external"))
        {*:attributes,kind:"image",src:source,alt:c.option(record,"alt",c.text(identified.kind))}
    else if (record.fallback!=null) {*:attributes,kind:"custom",content:record.fallback}
    else if (contains(["text","quote","custom"],identified.kind)) record
    else if (policy=="omit") null
    else if (policy=="text") {*:attributes,kind:"text",content:"Unsupported media snapshot: " ++ c.text(record.src)}
    else c.fail("export.media","supply a recorded frame, local/inline poster, fallback, explicit external-resource policy, or text/omit policy")
}

pub fn render(record, spec, context = {}) {
    let passive=if (context.passive==true) snapshot(record,context.event_id,c.option(context,"index",0),
        {resources:"external",unsupported:"text"},context.snapshot) else record;
    if (passive is error) passive else render_record(passive,spec,context)
}

fn render_record(record, spec, context) {
    let valid=validate(record);
    let media=identify(record);
    let custom=spec.media_options.providers[media.provider];
    let source=if (custom.resolve is fn) custom.resolve(record,context) else if (media.provider!=null) embed_url(record) else record.src;
    let options={loading:c.option(spec.media_options,"loading","lazy"),*:c.attrs(record)};
    let player={src:source,poster:record.poster,preload:"metadata",
        *:(if (record.controls!=false) {controls:""} else {}),*:(if (record.muted) {muted:""} else {}),
        *:(if (record.autoplay) {autoplay:""} else {}),*:(if (record.loop) {loop:""} else {}),
        style:"max-width:100%;",'data-timeline-player':media.kind,
        'data-timeline-player-event':context.event_id,'data-timeline-player-id':context.media_id};
    let content=if (custom.render is fn) custom.render(record,context)
        else if (media.provider!=null and media.kind!="image") <iframe src:source,title:c.option(record,"alt",c.text(media.provider)),loading:options.loading,
            allow:"autoplay; fullscreen; picture-in-picture",allowfullscreen:"",style:"width:100%;height:100%;border:0;">
        else if (media.kind=="image") <img src:source,alt:c.text(record.alt),loading:options.loading,width:record.width,height:record.height,
            style:"max-width:100%;object-fit:" ++ c.text(c.option(record,"fit","contain")) ++ ";">
        else if (media.kind=="video") <video *:player>
        else if (media.kind=="audio") <audio *:player>
        else if (media.kind=="quote") <blockquote c.render(record.content)>
        else if (contains(["custom","text"],media.kind)) c.render(record.content)
        else <iframe src:source,title:c.option(record,"alt",c.text(media.kind)),loading:options.loading,style:"width:100%;height:100%;border:0;">;
    if (valid is error) valid else if (source is error) source else
    <figure class:"lambda-timeline-media",'data-timeline-media':media.kind,'data-timeline-provider':media.provider,
        style:"margin:0;" ++ (if (record.width!=null) "width:" ++ c.text(record.width) ++ "px;" else "") ++
            (if (record.height!=null) "height:" ++ c.text(record.height) ++ "px;" else ""),
        if (record.link!=null) <a href:record.link,target:record.target,content> else content;
        if (record.caption!=null or record.credit!=null) <figcaption c.render(record.caption)
            if (record.credit!=null) <cite c.render(record.credit)>>
    >
}
