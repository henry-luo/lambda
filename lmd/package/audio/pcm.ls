// D7.5.3: native audio consumes owned WAV bytes; synthesis stays outside the player.
fn le16(v) array => [v%256,(v div 256)%256]
fn le32(v) array => [v%256,(v div 256)%256,(v div 65536)%256,(v div 16777216)%256]
// Soundflow's original mono PCM encoder, shared without changing its sample quantization.
pub pn wav(samples,rate=24000) binary^ {
 let valid=if (not (samples is array) or not (rate is int) or rate<8000 or rate>96000 or len(samples)>100000000)
  raise error("audio.pcm: expected mono samples and an 8000..96000 Hz rate")
 let n=len(samples)
 let head=[82,73,70,70,*le32(36+n*2),87,65,86,69,102,109,116,32,*le32(16),*le16(1),*le16(1),
  *le32(rate),*le32(rate*2),*le16(2),*le16(16),100,97,116,97,*le32(n*2)]
 var bytes:int[]=fill(44+n*2,0)
 var i=0
 while (i<44) { bytes[i]=head[i];i=i+1 }
 i=0
 while (i<n) {
  let sample=samples[i]
  if (not (sample is number) or abs(sample)>1e30) { raise error("audio.pcm: nonfinite sample") }
  let v=int(max(-1.0,min(1.0,sample))*32000.0),u=if (v<0) v+65536 else v
  bytes[44+i*2]=u%256
  bytes[45+i*2]=u div 256
  i=i+1
 }
 return binary(bytes)
}
