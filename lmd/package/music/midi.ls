// bounded SMF Type 0/1 codec; bytes are immutable and file access remains in lambda-io.
import c: .common
import t: .time
import tl: .timeline

fn check(ok, message) bool^ => c.require(ok,"MIDI: "++message)^
fn uint(bytes, at, count) int^ {
    let valid=check(at>=0 and count>=0 and at+count<=len(bytes),"truncated integer")^;
    c.fold([for (i in 0 to (count-1)) int(bytes[at+i])],0,(a,b)=>a*256+b)
}
fn vlq(bytes, pos, end, value = 0, count = 0) map^ {
    let valid=check(pos<end and count<4,"truncated or excessive variable-length quantity")^
    let b=int(bytes[pos])
    let next=value*128+b%128;
    if (b<128) {value:next,pos:pos+1} else vlq(bytes,pos+1,end,next,count+1)^
}
fn track(bytes, pos, end, division, index, tick = 0, running = 0, events = [], tempos = [], metadata = [], warnings = []) map^ {
    let budget=check(len(events)+len(metadata)+len(tempos)<=200000,"event budget exceeded")^
    let delta=vlq(bytes,pos,end)^
    let now=tick+delta.value
    let at=t.ratio(now,division)^
    let lead=int(bytes[delta.pos])
    let status=if (lead>=128) lead else running
    let start=delta.pos+(if (lead>=128) 1 else 0)
    let valid=check(delta.pos<end and status>=128,"missing running status")^;
    if (status==255) {
        let kind=int(bytes[start])
        let length=vlq(bytes,start+1,end)^
        let next=length.pos+length.value
        let bounded=check(start<end and next<=end,"meta event exceeds track")^
        let data=slice(bytes,length.pos,next)
        let meta={at:at,type:kind,data:data,track:index}
        if (kind==47) {
            let valid_end=check(length.value==0 and next==end,"invalid end-of-track")^;
            {events:events,tempos:tempos,metadata:metadata,duration:at,warnings:warnings}
        } else if (kind==81) {
            let valid_tempo=check(length.value==3,"tempo requires three bytes")^;
            track(bytes,next,end,division,index,now,0,events,[*tempos,tl.tempo(at,uint(data,0,3)^)],metadata,warnings)^
        } else {
            let valid_meter=check(kind!=88 or length.value==4 and data[0]>0 and data[1]<=7,"invalid time signature")^;
            track(bytes,next,end,division,index,now,0,events,tempos,[*metadata,meta],warnings)^
        }
    } else if (status==240 or status==247) {
        let length=vlq(bytes,start,end)^
        let next=length.pos+length.value
        let bounded=check(next<=end,"SysEx exceeds track")^;
        track(bytes,next,end,division,index,now,0,events,tempos,metadata,
            [*warnings,c.diagnostic("midi-sysex","SysEx is not executed",string(index))])^
    } else {
        let kind=status div 16
        let size=if (kind==12 or kind==13) 1 else 2
        let a=int(bytes[start])
        let b=if (size==2) int(bytes[start+1]) else 0
        let bounded=check(kind>=8 and kind<=14 and start+size<=end and a<128 and b<128,"invalid channel event")^
        let supported=contains([8,9,11,12,14],kind)
        let ignored=kind==10 or kind==13 or kind==11 and not contains([0,32,7,39,10,42,11,43,64,6,38,100,101,120,121,123],a)
        let event={*:tl.message(at,status,a,b),track:index};
        track(bytes,start+size,end,division,index,now,status,
            if (supported) [*events,event] else events,tempos,metadata,
            if (ignored) [*warnings,c.diagnostic("midi-message","unsupported sound-changing MIDI message",string(status)++":"++string(a))] else warnings)^
    }
}
fn tracks(bytes, pos, remaining, division, result = []) array^ {
    if (remaining==0) {
        let end=check(pos==len(bytes),"trailing bytes after tracks")^;
        result
    } else {
        let header=check(slice(bytes,pos,pos+4)==b'\x4D54726B',"expected MTrk chunk")^
        let length=uint(bytes,pos+4,4)^
        let end=pos+8+length
        let bounded=check(end<=len(bytes),"truncated track")^
        let value=track(bytes,pos+8,end,division,len(result))^;
        tracks(bytes,end,remaining-1,division,[*result,value])^
    }
}
pub fn read(bytes,options = {}) map^ {
    let opts=c.option_warnings(options,["strict"])^
    let valid=check(bytes is binary and len(bytes)>=14 and len(bytes)<=16000000 and
        slice(bytes,0,4)==b'\x4D546864',"expected an SMF header within 16 MB")^
    let length=uint(bytes,4,4)^
    let kind=uint(bytes,8,2)^
    let count=uint(bytes,10,2)^
    let division=uint(bytes,12,2)^
    let header=check(length>=6 and count>0 and count<=128 and (kind==1 or kind==0 and count==1) and division>0 and division<32768,
        "unsupported header (Type 0/1, metrical timing required)")^
    let parsed=tracks(bytes,8+length,count,division)^
    let events=tl.ordered([for (tr in parsed,e in tr.events) e])
    let channels=sort(unique([for (e in events) e.status%16]))
    let metadata=[for (tr in parsed,m in tr.metadata) m]
    let diagnostics=c.warnings([*opts,*[for (tr in parsed,w in tr.warnings) w]],options)^
    let result=tl.finish(events,[for (tr in parsed,tempo in tr.tempos) tempo],
        [for (ch in channels) {id:"channel-"++string(ch+1),label:"Channel "++string(ch+1),channels:[ch]}],[],
        c.fold([for (tr in parsed) tr.duration],t.ZERO,t.later),diagnostics)^;
    {*:result,metadata:metadata,source_format:kind,source_ppqn:division}
}

fn gcd(a,b) => if (b==0) a else gcd(b,a%b)
fn resolution(a,b) int^ {
    let value=(a div gcd(a,b))*b
    let valid=check(value<32768,"exact timing exceeds SMF resolution; supply ppqn and quantize:true")^;
    value
}
fn big(value,count) => [for (i in 0 to (count-1)) (value div int(256**(count-i-1)))%256]
fn variable(value) array^ {
    let valid=check(value>=0 and value<=268435455,"delta exceeds SMF variable-length range")^;
    if (value<128) [value] else [*[for (b in variable(value div 128)^) b+128*(if (b<128) 1 else 0)],value%128]
}
fn ticks(at,ppqn,quantize) int^ {
    let value=float(at.num)*ppqn/at.den
    let valid=check(quantize or at.num*ppqn%at.den==0,"event is not representable at requested ppqn")^;
    int(round(value))
}
fn track_bytes(events,end) array^ {
    let sorted=sort(events,(e)=>e.tick)
    let data=[for (i,e in sorted) *[*variable(e.tick-(if (i==0) 0 else sorted[i-1].tick))^,*e.bytes]]
    let tail=[*variable(end-(if (len(sorted)==0) 0 else sorted[len(sorted)-1].tick))^,255,47,0]
    let body=[*data,*tail];
    [77,84,114,107,*big(len(body),4),*body]
}
fn to_binary(bytes) binary^ {
    // Mark's canonical binary literal is the shared immutable-byte construction path.
    let hex="0123456789ABCDEF"
    parse("b'\\x"++join([for (b in bytes) hex[b div 16]++hex[b%16]],"")++"'",'mark')^
}
pub fn write(performance,options = {}) binary^ {
    let opts=c.option_warnings(options,["strict","ppqn","quantize"])^
    let warnings=c.warnings(opts,options)^
    let valid=check(performance.kind=="music-performance" and (options.quantize==null or options.quantize is bool),"invalid performance/options")^
    let ppqn=if (options.ppqn!=null) options.ppqn else c.fold([performance.duration.den,
        *[for (e in performance.events) e.at.den],*[for (e in performance.tempos) e.at.den]],480,resolution)^
    let valid_ppqn=check(c.is_integer(ppqn) and ppqn>0 and ppqn<32768,"ppqn must be 1..32767")^
    let quantize=options.quantize==true
    let end=ticks(performance.duration,ppqn,quantize)^
    let quantized=[for (e in performance.events) {*:e,at:t.ratio(ticks(e.at,ppqn,quantize)^,ppqn)^}]
    let durations=tl.check_notes(quantized)^
    let tempo_events=[for (e in performance.tempos) {tick:ticks(e.at,ppqn,quantize)^,bytes:[255,81,3,*big(e.microseconds,3)]}]
    let metadata=[for (e in (performance.metadata or []) where e.type!=47 and e.type!=81)
        {tick:ticks(e.at,ppqn,quantize)^,bytes:[255,e.type,*variable(len(e.data))^,*[for (b in e.data) int(b)]]}]
    let channels=sort(unique([for (e in performance.events) e.status%16]))
    let chunks=[track_bytes(tempo_events++metadata,end)^,
        *[for (channel in channels) track_bytes([for (e in performance.events where e.status%16==channel)
            {tick:ticks(e.at,ppqn,quantize)^,bytes:[e.status,e.a,*if (e.status div 16!=12) [e.b] else []]}],end)^]];
    to_binary([77,84,104,100,0,0,0,6,0,1,*big(len(chunks),2),*big(ppqn,2),*[for (chunk in chunks) *chunk]])^
}
