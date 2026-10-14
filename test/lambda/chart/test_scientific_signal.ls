import signal: lambda.chart.signal
fn near(a,b) => abs(a-b)<1e-9
let tone={kind:"signal",sample_rate:8,samples:[for (i in 0 to 7) math.cos(2*math.pi*i/8)]};
let spectrum=signal.analyze(tone);
let psd=signal.analyze(tone,{method:"periodogram",window:"rectangular",detrend:"none"});
let padded=signal.analyze(tone,{method:"periodogram",window:"rectangular",detrend:"none",n_fft:17});
let welch=signal.analyze(tone,{method:"welch",segment_length:4,overlap:2,window:"rectangular",detrend:"none"});
let stft=signal.analyze(tone,{method:"stft",segment_length:4,overlap:2,window:"rectangular"});
let spectrogram=signal.analyze(tone,{method:"spectrogram",segment_length:4,overlap:2,window:"rectangular"});
let coherent=signal.analyze(tone,{method:"coherence",reference:tone,segment_length:4,window:"rectangular",detrend:"none"});
let cross=signal.analyze(tone,{method:"csd",reference:{*:tone,samples:[for (i in 0 to 7) math.sin(2*math.pi*i/8)]},segment_length:8,window:"rectangular",detrend:"none"});
let x={kind:"signal",sample_rate:4,samples:[1,0,0,0]};
let y={*:x,samples:[0,0,1,0]};
let correlation=signal.analyze(x,{method:"correlation",reference:y});
let missing=signal.analyze({*:tone,samples:[null,1,2,3,4,5,6,7]},{method:"welch",segment_length:4,overlap:0,missing:"segment"});
let complex_tone=signal.analyze({kind:"signal",sample_rate:5,samples:{real:[for (i in 0 to 4) math.cos(2*math.pi*i/5)],imag:[for (i in 0 to 4) math.sin(2*math.pi*i/5)]}});
let a=[2,-1,3,0,4];
let b=[1,2,-2,1,0];
let oracle=signal.analyze({kind:"signal",sample_rate:5,samples:a},{method:"correlation",reference:{kind:"signal",sample_rate:5,samples:b}});
let gaps=signal.analyze({kind:"signal",sample_rate:8,samples:[1,2,3,4,null,6,7,8,9,10,11,12]},
    {method:"spectrogram",segment_length:4,overlap:0,missing:"segment"});
let channels=[for (i in 0 to 7) for (channel in ["a","b"]) {channel:channel,t:i/8,v:tone.samples[i]*(if (channel=="a") 1 else 2)}];
let grouped=signal.analyze(channels,{field:"v",time:"t",groupby:["channel"]});
let checks={
    window:signal.window("hann",4)==[0.0,0.49999999999999994,1.0,0.5000000000000001] or all([for (i,v in signal.window("hann",4)) near(v,[0,0.5,1,0.5][i])]),
    tone:len(spectrum.data)==5 and near(spectrum.data[1].amplitude,1) and near(spectrum.data[1].real,4),
    phase:near(spectrum.data[1].phase,0),
    psd:near(sum(psd.data |> ~.value),0.5),
    padding:near(sum(padded.data |> ~.value)*8/17,0.5),
    welch:welch.metadata.segment_count==3,
    raw:len(stft.data)==9 and near(stft.data[0].time,1.5/8),
    spectrogram:spectrogram.data.kind=="grid" and len(spectrogram.data.x)==4 and len(spectrogram.data.y)==4 and len(spectrogram.data.cell_fields.value)==3,
    support:stft.metadata.segments[0].support==[0.0,0.375],
    coherence:all([for (row in coherent.data where row.value!=null) near(row.value,1)]),
    csd_sign:near(cross.data[1].phase,0-math.pi/2),
    correlation:correlation.data[5].lag==2 and near(correlation.data[5].value,1) and near(correlation.data[1].value,0),
    omitted:missing.diagnostics.omitted_windows==1 and missing.metadata.segment_count==1,
    complex:(complex_tone.data |> ~.frequency) == [-2.0,-1.0,0.0,1.0,2.0],
    complex_amplitude:near(complex_tone.data[3].amplitude,1),
    dc_nyquist:near(signal.analyze({*:tone,samples:[1,1,1,1,1,1,1,1]}).data[0].amplitude,1) and
        near(signal.analyze({*:tone,samples:[1,-1,1,-1,1,-1,1,-1]}).data[4].amplitude,1),
    zero:all(signal.analyze({*:tone,samples:[0,0,0,0,0,0,0,0]}).data |> ~.phase==null),
    empty:signal.analyze({*:tone,samples:[]}).data==[],
    bad_empty:signal.analyze({*:tone,samples:[]},{n_fft:0}) is error,
    bad_window:signal.window([0,0],2) is error,
    bad_samples:signal.analyze({*:tone,samples:[null,1]}) is error,
    bad_segment:signal.analyze(tone,{segment_length:4}) is error,
    bad_raw:signal.analyze(tone,{method:"stft",scaling:"density"}) is error,
    bad_complex:signal.analyze({*:tone,samples:{real:[1,0],imag:[0,1]}},{sides:"onesided"}) is error,
    bad_rate:signal.analyze({*:tone,sample_rate:0}) is error
    ,direct_correlation:all([for (row in oracle.data) near(row.value,sum([for (i,v in a where i+row.lag>=0 and i+row.lag<len(b)) v*b[i+row.lag]]))])
    ,padded_windows:signal.analyze(tone,{method:"stft",segment_length:4,overlap:2,boundary:"pad_zero"}).metadata.segment_count==4
    ,missing_gap:len(gaps.data.x)==4 and all([for (row in gaps.data.cell_fields.value) row[1]==null and row[0]!=null and row[2]!=null])
    ,linear_detrend:all(signal.analyze({*:tone,samples:[0,1,2,3,4,5,6,7]},{method:"periodogram",detrend:"linear"}).data |> near(~.value,0))
    ,calibrated_hann:near(signal.analyze(tone,{window:"hann"}).data[1].amplitude,1)
    ,zero_coherence:all(signal.analyze({*:tone,samples:[0,0,0,0,0,0,0,0]},{method:"coherence",reference:{*:tone,samples:[0,0,0,0,0,0,0,0]}}).data |> ~.value==null)
    ,bad_reference:signal.analyze(tone,{method:"csd",reference:{*:tone,samples:[null,1,2,3,4,5,6,7]}}) is error
    ,sample_budget:signal.analyze(tone,{max_samples:4}) is error
    ,grouped:len(grouped.data)==10 and near(grouped.data[1].amplitude,1) and near(grouped.data[6].amplitude,2) and grouped.data[6].channel=="b"
    ,group_identity:grouped.data[1].id!=grouped.data[6].id and len(grouped.metadata.groups)==2
    ,group_reference:signal.analyze(channels,{field:"v",time:"t",groupby:["channel"],method:"coherence",reference:channels}).data[1].value>0.999999
    ,group_budget:signal.analyze(channels,{field:"v",time:"t",groupby:["channel"],max_samples:8}) is error
    ,group_cells:signal.analyze(channels,{field:"v",time:"t",groupby:["channel"],max_cells:7}) is error
    ,group_grid:signal.analyze(channels,{field:"v",time:"t",groupby:["channel"],method:"spectrogram"}) is error
};
[for (label,passed in checks where passed!=true) string(label)]
