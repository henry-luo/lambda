import t: lambda.chart.timeline
import native: lambda.chart.timeline.native_media
let raw={id:"media-story",view:"story",spacing:"sequence",source:{revision:"r1"},
    slideshow:{duration:100,media_end:true,media_timeout:200},
    events:[{id:"a",title:"A",media:[{id:"film",kind:"video",src:"film.mp4"},{id:"voice",kind:"audio",src:"voice.wav"}]},{id:"b",title:"B"}]}
fn act(current, action, now, spec = raw) => t.update(current,action,{spec:spec,revision:spec.source.revision,now:now})
fn completion(current, id, status = "ended") => {type:"media_event",event_id:"a",id:id,status:status,
    token:current.media_session.token,revision:"r1"}
let played=act(t.initial_state(raw),{type:"play"},0)
let held=act(played.state,{type:"tick"},100)
let waiting=act(held.state,{type:"tick"},150)
let paused=act(waiting.state,{type:"pause"},160)
let resumed=act(paused.state,{type:"resume"},1160)
let first_ended=act(resumed.state,completion(resumed.state,"film"),1170)
let all_ended=act(first_ended.state,completion(first_ended.state,"voice"),1180)
let expired=act(held.state,{type:"tick"},300)
let early_film=act(played.state,completion(played.state,"film"),30)
let early_voice=act(early_film.state,completion(early_film.state,"voice"),40)
let failed=act(held.state,completion(held.state,"film","error"),110)
let failure_finished=act(failed.state,completion(failed.state,"voice","error"),120)
let restarted=act(held.state,{type:"play"},200)
let stopped=act(held.state,{type:"stop"},200)
let sought=act(held.state,{type:"seek",time_ms:100},200)
let final_raw={*:raw,events:[raw.events[0]]}
let final_play=act(t.initial_state(final_raw),{type:"play"},0,final_raw)
let final_held=act(final_play.state,{type:"tick"},100,final_raw)
let final_timeout=act(final_held.state,{type:"tick"},300,final_raw)
let passive={*:raw,slideshow:{duration:100}}
let passive_play=act(t.initial_state(passive),{type:"play"},0,passive)
let old_revision={*:raw,source:{revision:"r2"}}
let revision_tick=act(held.state,{type:"tick"},120,old_revision)
let active_raw={*:passive,media_options:{active_playback:true}}
let active_start=act(t.initial_state(active_raw),{type:"reconcile"},0,active_raw)
let active_stop=act(active_start.state,{type:"stop"},0,active_raw)
let active_still=act(active_stop.state,{type:"reconcile"},1,active_raw)
let active_away=act(active_still.state,{type:"activate",id:"b"},2,active_raw)
let active_back=act(active_away.state,{type:"activate",id:"a"},3,active_raw)
let repeat_raw={*:raw,slideshow:{*:raw.slideshow,repeat:"infinite"}}
let repeat_play=act(t.initial_state(repeat_raw),{type:"play"},0,repeat_raw)
let repeat_done=act(act(repeat_play.state,completion(repeat_play.state,"film"),10,repeat_raw).state,completion(repeat_play.state,"voice"),20,repeat_raw)
let repeat_again=act(repeat_done.state,{type:"tick"},200,repeat_raw);
[
 ["player requests share session token",len(played.requests)==2 and all(played.requests |> ~.kind=="media_command") and played.requests[0].token==played.requests[1].token],
 ["player request identity",played.requests[0].event_id=="a" and played.requests[0].id=="film" and played.requests[0].revision=="r1"],
 ["wait at slide boundary",held.state.active_id=="a" and held.state.playback.phase=="playing" and held.state.playback.elapsed==100 and held.state.media_wait!=null],
 ["waiting progress complete",held.state.story_frame.progress==1 and held.state.story_frame.waiting_media==true],
 ["waiting does not restart players",len(waiting.requests)==0 and waiting.state.media_wait.elapsed==50],
 ["pause freezes wait",paused.state.playback.phase=="paused" and paused.state.media_wait.elapsed==60 and all(paused.requests |> ~.command=="pause")],
 ["resume preserves wait budget",resumed.state.playback.phase=="playing" and resumed.state.media_wait.elapsed==60 and all(resumed.requests |> ~.command=="play")],
 ["wait for every player",first_ended.state.active_id=="a" and first_ended.state.media_wait!=null],
 ["completion releases next slide",all_ended.state.active_id=="b" and all_ended.state.media_wait==null and all_ended.state.playback.elapsed==100],
 ["deactivated players paused",len(all_ended.requests)==2 and all(all_ended.requests |> ~.command=="pause")],
 ["bounded unavailable player",expired.state.active_id=="b" and expired.state.media_wait==null],
 ["timeout notification",len(expired.notifications |: ~.name=="timeline_media_timeout")==1],
 ["timeout emitted once",len(act(expired.state,{type:"tick"},310).notifications |: ~.name=="timeline_media_timeout")==0],
 ["early completions do not skip duration",early_voice.state.active_id=="a" and act(early_voice.state,{type:"tick"},100).state.active_id=="b"],
 ["failure releases completed set",failed.state.active_id=="a" and failure_finished.state.active_id=="b"],
 ["stale token ignored",act(held.state,{*:completion(held.state,"film"),token:999},110).state==held.state],
 ["stale revision ignored",act(held.state,{*:completion(held.state,"film"),revision:"r0"},110).state==held.state],
 ["stale event ignored",act(held.state,{*:completion(held.state,"film"),event_id:"b"},110).state==held.state],
 ["unknown player ignored",act(held.state,completion(held.state,"missing"),110).state==held.state],
 ["invalid player state rejected",act(held.state,completion(held.state,"film","unknown"),110) is error],
 ["restart invalidates session",restarted.state.media_wait==null and restarted.state.media_session.token!=held.state.media_session.token],
 ["stop clears wait and players",stopped.state.media_wait==null and stopped.state.media_session==null and stopped.state.playback.phase=="idle"],
 ["seek clears wait",sought.state.active_id=="b" and sought.state.media_wait==null],
 ["final slide waits before ended",final_held.state.playback.phase=="playing" and final_held.state.active_id=="a"],
 ["final timeout ends",final_timeout.state.playback.phase=="ended" and final_timeout.state.media_wait==null],
 ["disabled media synchronization",passive_play.state.media_session==null and len(passive_play.requests)==0 and act(passive_play.state,{type:"tick"},100,passive).state.active_id=="b"],
 ["source replacement cannot retain wait",revision_tick.state.media_wait==null and revision_tick.state.active_id=="b"],
 ["invalid timeout",t.validate({*:raw,slideshow:{media_timeout:0}}) is error and t.validate({*:raw,slideshow:{media_timeout:-1}}) is error],
 ["active playback independent of slideshow",active_start.state.playback.phase=="idle" and active_start.state.media_session.phase=="playing" and len(active_start.requests)==2],
 ["explicit stop stays stopped",active_still.state.media_session==null and len(active_still.requests)==0],
 ["activation restarts active media",active_back.state.media_session!=null and active_back.state.media_session.token!=active_start.state.media_session.token],
 ["repeat restarts player generation",repeat_again.state.active_id=="a" and repeat_again.state.media_session.token!=repeat_play.state.media_session.token],
 ["previous cycle completion ignored",act(repeat_again.state,completion(repeat_play.state,"film"),210,repeat_raw).state==repeat_again.state],
 ["duplicate player IDs rejected",t.validate({*:raw,events:[{id:"a",media:[raw.events[0].media[0],raw.events[0].media[0]]}]}) is error],
 ["invalid player ID rejected",t.validate({*:raw,events:[{id:"a",media:[{*:raw.events[0].media[0],id:42}]}]}) is error],
 ["accepted completion notification",len(first_ended.notifications |: ~.name=="timeline_media" and ~.detail.status=="ended")==1],
 ["duplicate completion notification suppressed",len(act(first_ended.state,completion(first_ended.state,"film"),1171).notifications |: ~.name=="timeline_media")==0],
 ["paused completion notification",len(act(paused.state,completion(paused.state,"film"),170).notifications |: ~.name=="timeline_media")==1],
 ["inferred remote providers keep external controllers",len(native.queued([
    {kind:"media_command",media:{src:"https://www.youtube.com/watch?v=example"}},
    {kind:"media_command",media:{kind:"audio",src:"local.wav"}}]))==1]
]
