// D7.5.4: one shared native handle lifetime and bounded overlap policy.
import dom
pub pn close(owner,voices) { for (token in voices) dom.audio_close(owner,token) }
pub pn start(owner,voices,bytes,gain=1.0,limit=14) map {
 var live=[]
 var released=0
 for (token in voices) {
  if (dom.audio_state(owner,token)=="playing") { live=[*live,token];continue }
  dom.audio_close(owner,token)
  released=released+1
 }
 let capacity=max(1,min(32,int(limit)))
 while (len(live)>=capacity) {
  dom.audio_close(owner,live[0])
  live=slice(live,1,len(live))
  released=released+1
 }
 let token=dom.audio_open(owner,bytes)
 if (token==null or not dom.audio_play(owner,token,max(0.0,min(1.0,gain)))) {
  if (token!=null) dom.audio_close(owner,token)
  close(owner,live)
  return {voices:[],failed:true,opened:if (token==null) 0 else 1,closed:released+len(live)+(if (token==null) 0 else 1)}
 }
 return {voices:[*live,token],failed:false,opened:1,closed:released}
}
