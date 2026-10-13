// Windowed analysis uses the shared, unnormalised math.fft (S7.10.5v3).
import util: .util
import mesh: .mesh
import records: .records

fn option(options,key,fallback) => if (options[key]!=null) options[key] else fallback
fn energy(value) => real(value)**2+imag(value)**2
fn phase(value) => if (energy(value)==0) null else math.atan2(imag(value),real(value))
fn present(options,keys) => any([for (key in keys) options[key]!=null])

pub fn window(kind,length) {
    if (not mesh.positive_int(length)) error("signal.window: length must be a positive integer")
    else if (kind is array) if (len(kind)!=length or not all(kind |> mesh.finite(~)) or sum(kind*kind)==0)
        error("signal.window: coefficients require matching length, finite values and nonzero energy") else kind
    else if (not contains(["rectangular","hann","hamming","blackman"],kind)) error("signal.window: unknown window")
    else [for (n in 0 to (length-1),let angle=util.TAU*n/length)
        if (length==1 or kind=="rectangular") 1.0 else if (kind=="hann") 0.5-0.5*math.cos(angle)
        else if (kind=="hamming") 0.54-0.46*math.cos(angle)
        else 0.42-0.5*math.cos(angle)+0.08*math.cos(2*angle)]
}

fn descriptor(data,options) {
    let rows=if (options.sort==true and options.time!=null and data is array) sort(data,(r)=>r[options.time]) else data;
    let samples=if (data is array) [for (r in rows) r[options.field]] else data.samples;
    let times=if (data is array and options.time!=null) rows |> ~[options.time] else null;
    let inferred=if (len(times)>1) 1.0/(times[1]-times[0]) else null;
    let rate=if (data is array) option(options,"sample_rate",inferred) else data.sample_rate;
    let start=if (times!=null and len(times)>0) times[0] else option(if (data is array) options else data,"start_time",0.0);
    let complex_input=samples is map;
    let shape=if (complex_input) samples.real is array and samples.imag is array and len(samples.real)==len(samples.imag)
        else samples is array;
    let maximum=option(options,"max_samples",1048576);
    let sample_count=if (complex_input) len(samples.real) else len(samples);
    let values=if (not mesh.positive_int(maximum) or sample_count>maximum) []
        else if (complex_input) [for (i,v in samples.real) if (mesh.finite(v) and mesh.finite(samples.imag[i])) complex(float(v),float(samples.imag[i])) else null]
        else [for (v in samples) if (mesh.finite(v)) complex(float(v),0.0) else null];
    if (not (data is array) and data.kind!="signal") error("signal.analyze: expected signal descriptor or records")
    else if (data is array and options.field==null) error("signal.analyze: record input requires field")
    else if (not shape) error("signal.analyze: samples must be an array or matching real/imag arrays")
    else if (not mesh.positive_int(maximum) or sample_count>maximum) error("signal.analyze: sample limit exceeded")
    else if (not mesh.finite(rate) or rate<=0 or not mesh.finite(start)) error("signal.analyze: positive sample_rate and finite start_time required")
    else if (times!=null and any([for (i,t in times) not mesh.finite(t) or (i>0 and (t<=times[i-1] or
        abs((t-times[i-1])*rate-1.0)>1e-7))])) error("signal.analyze: timestamps must be uniformly sampled and agree with sample_rate")
    else {samples:values,sample_rate:float(rate),start_time:float(start),complex:complex_input,unit:if (data is array) options.unit else data.unit}
}

fn settings(data,options) {
    let method=option(options,"method","spectrum");
    let n=len(data.samples);
    let whole=contains(["spectrum","periodogram"],method);
    let length=option(options,"segment_length",if (whole) max(1,n) else max(1,min(256,n)));
    let overlap=option(options,"overlap",if (whole) 0 else int(floor(length/2)));
    let k=option(options,"n_fft",length);
    let quantity=option(options,"quantity","power");
    let sides=option(options,"sides",if (data.complex) "twosided" else "onesided");
    let scaling=option(options,"scaling","density");
    let detrend=option(options,"detrend",if (contains(["spectrum","stft","spectrogram"],method)) "none" else "constant");
    let boundary=option(options,"boundary","drop");
    let missing=option(options,"missing","error");
    let maximum=option(options,"max_fft",1048576);
    let cells=option(options,"max_cells",1000000);
    let win=option(options,"window",if (method=="spectrum") "rectangular" else "hann");
    let display=options.display;
    if (not contains(["spectrum","periodogram","welch","stft","spectrogram","csd","coherence","correlation"],method)) error("signal.analyze: unknown method")
    else if (not mesh.positive_int(maximum) or not mesh.positive_int(cells)) error("signal.analyze: limits must be positive integers")
    else if (not mesh.positive_int(length) or (n>0 and length>n) or not mesh.positive_int(k) or k<length or k>maximum or
        not mesh.finite(overlap) or floor(overlap)!=overlap or overlap<0 or overlap>=length) error("signal.analyze: invalid segment_length, overlap, or n_fft")
    else if (whole and present(options,["segment_length","overlap"])) error("signal.analyze: full-record methods reject segment_length and overlap")
    else if (not contains(["onesided","twosided"],sides) or (data.complex and sides=="onesided")) error("signal.analyze: invalid sides for input")
    else if (not contains(["density","spectrum"],scaling) or not contains(["none","constant","linear"],detrend) or
        not contains(["drop","pad_zero"],boundary) or not contains(["error","segment"],missing) or
        not contains(["power","amplitude","phase"],quantity)) error("signal.analyze: unsupported analysis option")
    else if (method=="stft" and present(options,["scaling","display"])) error("signal.analyze: raw STFT rejects scaling and display")
    else if (method!="spectrogram" and options.quantity!=null) error("signal.analyze: quantity is a spectrogram option")
    else if (method=="spectrogram" and quantity!="power" and options.scaling!=null) error("signal.analyze: amplitude and phase do not use power scaling")
    else if (display!=null and (display.type!="db" or not mesh.finite(display.reference) or display.reference<=0 or
        (display.floor!=null and not mesh.finite(display.floor)) or method=="coherence" or method=="csd" or quantity=="phase")) error("signal.analyze: invalid dB display")
    else if (method=="correlation" and present(options,["window","segment_length","overlap","n_fft","detrend","sides","boundary","scaling","display","quantity","unwrap"]))
        error("signal.analyze: correlation rejects spectral options")
    else if (method!="correlation" and present(options,["normalization","max_lag"])) error("signal.analyze: lag options require correlation")
    else if (options.unwrap!=null and not (options.unwrap is bool)) error("signal.analyze: unwrap must be boolean")
    else if (missing=="error" and any(data.samples |> ~==null)) error("signal.analyze: missing or nonfinite sample")
    else {
        let coefficients=window(win,int(length));
        let w1=sum(coefficients);
        let w2=sum(coefficients*coefficients);
        if (coefficients is error) coefficients
        else if (not mesh.finite(w1) or not mesh.finite(w2) or w2==0) error("signal.analyze: window sums exceed binary64 range")
        else if ((method=="spectrum" or quantity=="amplitude" or scaling=="spectrum") and abs(w1)==0) error("signal.analyze: calibration requires nonzero window sum")
        else {method:method,n:n,length:int(length),overlap:int(overlap),hop:int(length-overlap),n_fft:int(k),quantity:quantity,
            sides:sides,scaling:scaling,detrend:detrend,boundary:boundary,missing:missing,window:win,coefficients:coefficients,
            w1:w1,w2:w2,display:display,unwrap:options.unwrap==true,max_cells:cells,max_fft:maximum}
    }
}

fn detrended(samples,mode) {
    let n=len(samples);
    let mean=if (mode=="none") complex(0,0) else sum(samples)/n;
    let center=(n-1)/2.0;
    let denominator=sum([for (i in 0 to (n-1)) (i-center)**2]);
    let slope=if (mode=="linear" and denominator>0) sum([for (i,v in samples) complex(i-center)*complex(v)])/denominator else complex(0,0);
    [for (i,v in samples) complex(v)-complex(mean)-complex(i-center)*complex(slope)]
}
fn coefficients(samples,start,s) {
    let values=detrended(slice(samples,start,min(len(samples),start+s.length)),s.detrend);
    math.fft([for (i in 0 to (s.n_fft-1)) if (i<len(values)) complex(values[i])*complex(s.coefficients[i]) else complex(0,0)])
}
fn starts(s) {
    let complete=if (s.n<s.length) 0 else int(floor((s.n-s.length)/s.hop))+1;
    [for (i in 0 to (complete-1)) i*s.hop,
        if (s.boundary=="pad_zero" and complete*s.hop<s.n) complete*s.hop] |: ~!=null
}
fn bins(s,rate) => if (s.sides=="onesided") [for (k in 0 to int(floor(s.n_fft/2))) {index:k,frequency:k*rate/s.n_fft,
    factor:if (k==0 or (s.n_fft%2==0 and k==s.n_fft/2)) 1.0 else 2.0}]
    else [for (k in (0-int(floor(s.n_fft/2))) to (int(ceil(s.n_fft/2))-1)) {index:if (k<0) k+s.n_fft else k,frequency:k*rate/s.n_fft,factor:1.0}]
fn unwrap(values,index=0,previous=null,offset=0.0,result=[]) {
    if (index>=len(values)) result else {
        let p=values[index];
        let delta=if (p==null or previous==null) 0.0 else p-previous;
        let next=if (p==null or previous==null) 0.0 else offset-util.TAU*floor((delta+util.PI)/util.TAU);
        unwrap(values,index+1,p,next,[*result,if (p==null) null else p+next])
    }
}
fn display_value(value,s,amplitude=false) => if (s.display==null) value else if (value==null or value<=0) s.display.floor
    else (let db=(if (amplitude) 20.0 else 10.0)*math.log10(value/s.display.reference),
        if (s.display.floor!=null) max(s.display.floor,db) else db)
fn calibrated(x,bin,s,rate) => {real:real(x),imag:imag(x),amplitude:if (s.w1==0) null else sqrt(energy(x))/abs(s.w1)*bin.factor,
    phase:phase(x),power:energy(x)*bin.factor/(if (s.scaling=="density") rate*s.w2 else s.w1*s.w1)}
fn edges(centers,step,lo,hi) => if (len(centers)==0) [] else [max(lo,centers[0]-step/2),
    for (i in 1 to (len(centers)-1)) (centers[i-1]+centers[i])/2,min(hi,centers[len(centers)-1]+step/2)]

fn spectral(data,reference,s) {
    let scheduled=starts(s);
    let usable=[for (start in scheduled where all(slice(data.samples,start,min(s.n,start+s.length)) |> ~!=null) and
        (reference==null or all(slice(reference.samples,start,min(s.n,start+s.length)) |> ~!=null))) start];
    let frequencies=bins(s,data.sample_rate);
    if (len(usable)==0) error("signal.analyze: no usable windows")
    else if (len(usable)*len(frequencies)>s.max_cells) error("signal.analyze: time-frequency cell limit exceeded")
    else {
        let segments=[for (start in usable) {start:start,time:data.start_time+(start+(s.length-1)/2.0)/data.sample_rate,
            support:[data.start_time+start/data.sample_rate,data.start_time+(min(s.n,start+s.length)-1)/data.sample_rate],
            coverage:min(s.length,s.n-start),x:coefficients(data.samples,start,s),
            y:if (reference!=null) coefficients(reference.samples,start,s) else null}];
        let failure=util.first_error([for (segment in segments) for (v in [segment.x,segment.y]) v]);
        let rows=[for (bi,bin in frequencies,
            let values=[for (segment in segments) calibrated(segment.x[bin.index],bin,s,data.sample_rate)],
            let px=avg(values |> ~.power),
            let py=if (reference!=null) avg([for (segment in segments) calibrated(segment.y[bin.index],bin,s,data.sample_rate).power]) else null,
            let cross=if (reference!=null) complex(sum([for (segment in segments) conj(complex(segment.x[bin.index]))*complex(segment.y[bin.index])]))*
                complex(bin.factor/(len(segments)*(if (s.scaling=="density") data.sample_rate*s.w2 else s.w1*s.w1))) else null,
            let coherence=if (px==0 or py==0) null else if (reference!=null) energy(cross)/(px*py) else null,
            let value=if (s.method=="spectrum") values[0].amplitude else if (s.method=="coherence")
                if (coherence==null) null else if (coherence>1+1e-12 or coherence < -1e-12) error("signal.analyze: coherence outside numerical tolerance") else min(1.0,max(0.0,coherence))
                else if (s.method=="csd") cross else px)
            {id:[0,bi],frequency:bin.frequency,value:value,display_value:if (s.method=="csd") value else display_value(value,s,s.method=="spectrum"),
                *:if (s.method=="spectrum") values[0] else if (s.method=="csd") {real:real(cross),imag:imag(cross),phase:phase(cross)} else {}}];
        let phases=if (s.unwrap) unwrap(rows |> ~.phase) else rows |> ~.phase;
        let raw=[for (si,segment in segments,
            let bin_values=[for (bin in frequencies) calibrated(segment.x[bin.index],bin,s,data.sample_rate)],
            let phases=if (s.unwrap) unwrap(bin_values |> ~.phase) else bin_values |> ~.phase)
            [for (bi,bin in frequencies,let v=bin_values[bi])
                {id:[si,bi],time:segment.time,frequency:bin.frequency,real:v.real,imag:v.imag,phase:phases[bi],
                    value:if (s.quantity=="phase") phases[bi] else if (s.quantity=="amplitude") v.amplitude else v.power}]];
        // Missing windows occupy masked columns; adjacent valid windows never stretch over a gap.
        let times=if (s.method=="spectrogram") [for (start in scheduled) data.start_time+(start+(s.length-1)/2.0)/data.sample_rate] else segments |> ~.time;
        let time_edges=if (len(times)==1) [data.start_time+scheduled[0]/data.sample_rate,data.start_time+min(s.n,scheduled[0]+s.length)/data.sample_rate]
            else edges(times,s.hop/data.sample_rate,data.start_time,data.start_time+s.n/data.sample_rate);
        let frequency_edges=edges(frequencies |> ~.frequency,data.sample_rate/s.n_fft,
            if (s.sides=="onesided") 0.0 else -data.sample_rate/2,data.sample_rate/2);
        let result=if (s.method=="stft") [for (segment in raw) for (v in segment) v]
            else if (s.method=="spectrogram") {kind:"grid",x:time_edges,y:frequency_edges,coordinate_fields:{x:"time",y:"frequency"},
                cell_fields:{value:[for (bi in 0 to (len(frequencies)-1)) [for (start in scheduled,let si=if (contains(usable,start)) util.find_index(usable,start) else -1) if (si<0) null else raw[si][bi].value]],
                    display_value:[for (bi in 0 to (len(frequencies)-1)) [for (start in scheduled,let si=if (contains(usable,start)) util.find_index(usable,start) else -1)
                        if (si<0) null else display_value(raw[si][bi].value,s,s.quantity=="amplitude")]]}}
            else [for (i,row in rows) {*:row,*:if (s.method=="spectrum" or s.method=="csd") {phase:phases[i]} else {}}];
        let row_error=util.first_error(rows |> ~.value);
        if (failure is error) failure else if (row_error is error) row_error else
        {data:result,metadata:{*:s,coefficients:null,sample_rate:data.sample_rate,start_time:data.start_time,unit:data.unit,
            segment_count:len(segments),segments:[for (segment in segments) {start:segment.start,time:segment.time,support:segment.support,coverage:segment.coverage}],
            frequency_unit:"Hz",time_unit:"s",phase_unit:"rad",frequency_centers:frequencies |> ~.frequency,time_centers:times},
            diagnostics:{omitted_windows:len(scheduled)-len(usable),omitted_starts:[for (v in scheduled where not contains(usable,v)) v]}}
    }
}

fn correlation(data,reference,options,s) {
    let other=if (reference!=null) reference else data;
    let n=len(data.samples);
    let lag=option(options,"max_lag",max(0,n-1));
    let normalization=option(options,"normalization","none");
    let denominator=sqrt(sum(data.samples |> energy(~))*sum(other.samples |> energy(~)));
    if (not mesh.finite(lag) or floor(lag)!=lag or lag<0 or (n>0 and lag>=n) or not contains(["none","biased","unbiased","coefficient"],normalization))
        error("signal.analyze: invalid correlation lag or normalization")
    else if (any([*data.samples,*other.samples] |> ~==null)) error("signal.analyze: correlation requires complete samples")
    else if (n>0 and 2*n-1>s.max_fft) error("signal.analyze: correlation FFT limit exceeded")
    else {
        let k=max(1,2*n-1);
        let x=math.fft([*data.samples,for (i in n to (k-1)) complex(0,0)]);
        let y=math.fft([*other.samples,for (i in n to (k-1)) complex(0,0)]);
        let r=math.ifft([for (i,v in x) conj(complex(v))*complex(y[i])]);
        {data:if (n==0) [] else [for (l in (0-lag) to lag,
            let divisor=if (normalization=="biased") n else if (normalization=="unbiased") n-abs(l) else if (normalization=="coefficient") denominator else 1.0,
            let value=if (divisor==0) null else complex(r[if (l<0) k+l else l])/complex(divisor))
            {id:l,lag:l,lag_seconds:l/data.sample_rate,value:if (data.complex or other.complex or value==null) value else real(value)}],
            metadata:{method:"correlation",sample_rate:data.sample_rate,normalization:normalization,unit:data.unit},diagnostics:{}}
    }
}

fn analyze_one(input, options) {
    let data=descriptor(input,options);
    let reference=if (options.reference!=null) descriptor(options.reference,options) else null;
    let s=if (data is error) data else settings(data,options);
    if (data is error) data else if (reference is error) reference else if (s is error) s
    else if (contains(["csd","coherence"],s.method) and reference==null) error("signal.analyze: paired method requires reference signal")
    else if (reference!=null and (data.sample_rate!=reference.sample_rate or data.start_time!=reference.start_time or len(data.samples)!=len(reference.samples)))
        error("signal.analyze: reference must have matching rate, start time and length")
    else if (reference!=null and reference.complex and s.sides=="onesided") error("signal.analyze: complex reference requires twosided analysis")
    else if (reference!=null and s.missing=="error" and any(reference.samples |> ~==null)) error("signal.analyze: missing or nonfinite reference sample")
    else if (s.method=="correlation") correlation(data,reference,options,s)
    else if (s.n==0) {data:if (s.method=="spectrogram") {kind:"grid",x:[],y:[],cell_fields:{value:[],display_value:[]},coordinate_fields:{x:"time",y:"frequency"}} else [],
        metadata:{*:s,sample_rate:data.sample_rate,start_time:data.start_time,unit:data.unit,segment_count:0},diagnostics:{omitted_windows:0,omitted_starts:[]}}
    else spectral(data,reference,s)
}

// Preserve channel keys before segment/bin identity; grouping never concatenates sample gaps.
pub fn analyze(input, options = {}) {
    let fields=options.groupby;
    if (fields==null or fields==[]) analyze_one(input,options)
    else if (not (input is array) or not (fields is array) or not all(fields |> ~ is string) or len(util.unique_vals(fields))!=len(fields))
        error("signal.analyze: groupby requires records and distinct field names")
    else if (options.method=="spectrogram") error("signal.analyze: facet records before a grouped spectrogram")
    else if (any(fields |> contains(["id","frequency","value","display_value","real","imag","amplitude","power","phase","time","lag","lag_seconds"],~)))
        error("signal.analyze: group field conflicts with an analysis output field")
    else if (not mesh.positive_int(option(options,"max_samples",1048576)) or len(input)>option(options,"max_samples",1048576))
        error("signal.analyze: sample limit exceeded")
    else if (len(input)==0) analyze_one(input,{*:options,groupby:null})
    else {
        let reference_groups=if (options.reference is array) records.group_by(options.reference,fields) else null;
        let groups=[for (group in records.group_by(input,fields),
            let reference=if (reference_groups!=null) (reference_groups |: ~.key==group.key)[0].rows else options.reference)
            {key:group.key,values:records.group_fields(group.rows[0],fields),
                result:if (options.reference!=null and reference==null) error("signal.analyze: missing reference channel")
                    else analyze_one(group.rows,{*:options,groupby:null,reference:reference})}];
        let failure=util.first_error(groups |> ~.result);
        if (failure is error) failure
        else if (sum(groups |> len(~.result.data))>option(options,"max_cells",1000000)) error("signal.analyze: grouped result cell limit exceeded")
        else {
            data:[for (group in groups) for (row in group.result.data) {*:row,*:group.values,id:[group.key,row.id]}],
            metadata:{*:groups[0].result.metadata,groupby:fields,groups:[for (group in groups) {key:group.key,metadata:group.result.metadata}]},
            diagnostics:{groups:[for (group in groups) {key:group.key,diagnostics:group.result.diagnostics}]}}
    }
}
