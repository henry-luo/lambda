// D7.5.3: PCM ownership and exact Soundflow-compatible mono encoding.
import pcm: lambda.audio.pcm
import synth: ~~.~~.demo.soundflow.mod_synth
pn main() {
 let samples=[0.0,0.5,-0.5,1.0,-1.0]
 let bytes=pcm.wav(samples,8000)^
 var bad_rate=false
 var bad_sample=false
 pcm.wav(samples,0) ^ { bad_rate=^ is error }
 pcm.wav([1e31]) ^ { bad_sample=^ is error }
 print([bytes==b'\x524946462E00000057415645666D74201000000001000100401F0000803E000002001000646174610A0000000000803E80C1007D0083',len(bytes)==54,
  synth.wav(samples)==pcm.wav(samples,24000)^,
  bad_rate,bad_sample])
}
