import t: lambda.chart.timeline
import media: lambda.chart.timeline.media
let image={src:"photo.jpg?size=large",alt:"People",caption:"Celebration",credit:"Author"}
let spec=t.validate({events:[{id:"a",media:[image]}]})
let video={src:"https://www.youtube.com/watch?v=abc123",start:12,end:20,muted:true}
let resolved=media.embed_url(video)
let rendered=media.render(image,spec)
let native=t.capabilities("native");
[
 ["image detection",media.identify(image).kind=="image"],
 ["video detection",media.identify({src:"movie.mp4"}).kind=="video"],
 ["audio detection",media.identify({src:"song.mp3"}).kind=="audio"],
 ["provider detection",media.identify(video).provider=="youtube"],
 ["exact domain",media.identify({src:"https://youtube.com.evil.test/watch?v=abc"}).provider==null],
 ["YouTube embed",starts_with(resolved,"https://www.youtube-nocookie.com/embed/abc123?") and contains(resolved,"start=12") and contains(resolved,"end=20")],
 ["Spotify URI",media.embed_url({src:"spotify:track:abc"})=="https://open.spotify.com/embed/track/abc"],
 ["Vimeo embed",media.embed_url({src:"https://vimeo.com/12345"})=="https://player.vimeo.com/video/12345"],
 ["figure",name(rendered)=='figure' and rendered[0].alt=="People"],
 ["captions",name(rendered[1])=='figcaption'],
 ["video element",name(media.render({src:"movie.mp4"},spec)[0])=='video'],
 ["audio element",name(media.render({src:"song.mp3"},spec)[0])=='audio'],
 ["rich presentation",t.render({events:[{id:"a",media:[image]}]}) is element],
 ["native capabilities",native.video.sources==["local"] and native.video.platforms==["macos"] and not native.fullscreen],
 ["browser capabilities",t.capabilities("browser").video.sources==["local","remote"]],
 ["static capability",t.capabilities("svg").video==false],
 ["missing source",t.validate({events:[{media:[{kind:"image"}]}]}) is error],
 ["invalid dimensions",t.validate({events:[{media:[{*:image,width:-1}]}]}) is error],
 ["invalid interval",media.validate({*:video,start:30}) is error],
 ["invalid scheme",media.validate({src:"javascript:bad()"}) is error]
]
