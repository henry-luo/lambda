// Context glyphs use the same real advances as note spacing.
import c: .common
import f: .fonts
import n: .notation
import d: .draw

fn clef_name(clef) => (if (clef.sign=="G") "gClef" else if (clef.sign=="F") "fClef" else "cClef") ++
    (if (clef.octave==0) "" else if (clef.octave==1) "8va" else if (clef.octave == -1) "8vb"
     else if (clef.octave==2) "15ma" else "15mb")
fn digits(profile, value, y) map^ => c.fold(split(value,""),{width:0.0,shapes:[]},(st,ch)=>(
    let key=if (ch=="+") "timeSigPlus" else "timeSig"++ch,
    let metric=f.glyph(profile,key)^,
    {width:st.width+metric.advance, shapes:[*st.shapes,d.glyph(profile,key,st.width,y)^]}))
fn time_signature(profile,meter) map^ {
    let groups=meter.groups or [{beats:meter.beats,unit:meter.unit}];
    c.fold([for (i,g in groups) {index:i,group:g}],{width:0.0,shapes:[]},(st,row)=>(
        let top=digits(profile,row.group.beats,1.0)^,
        let bottom=digits(profile,string(row.group.unit),3.0)^,
        let width=max(top.width,bottom.width),
        let separator=if (row.index==0) null else d.glyph(profile,"timeSigPlus",st.width+0.4,2.0)^,
        let x=if (separator==null) st.width else separator.bounds.right+0.4,
        {width:x+width,shapes:[*st.shapes,*if (separator==null) [] else [separator],
            *d.move_all(top.shapes,x+(width-top.width)/2,0.0),
            *d.move_all(bottom.shapes,x+(width-bottom.width)/2,0.0)]}))^
}
fn key_signature(profile, fifths, clef, natural = false) array^ {
    let steps=if (fifths>0) ["F","C","G","D","A","E","B"] else ["B","E","A","D","G","C","F"]
    let octaves=if (fifths>0) [5,5,5,5,4,5,4] else [4,5,4,5,4,5,4]
    let key=if (natural) "accidentalNatural" else if (fifths>0) "accidentalSharp" else "accidentalFlat"
    let advance=f.glyph(profile,key)^.advance+0.2;
    [for (i in 0 to (abs(fifths)-1)) (
        let pitch={step:steps[i],octave:octaves[i]-(if (clef.sign=="F") 2 else if (clef.sign=="C") 1 else 0)},
        d.glyph(profile,key,float(i)*advance,n.height(pitch,{*:clef,octave:0}))^)]
}
pub fn prepare(profile, context, previous = null, system_start = false) map^ {
    let show_clef=system_start or previous==null or context.clef!=previous.clef
    let show_key=system_start or previous==null or context.fifths!=previous.fifths
    let show_meter=context.meter.visible!=false and (previous==null or context.meter!=previous.meter)
    let clef=if (show_clef) d.glyph(profile,clef_name(context.clef),0.0,4.0-float(context.clef.line-1))^ else null
    let x1=if (clef==null) 0.0 else clef.bounds.right+0.8
    let cancelled=if (show_key and previous!=null and previous.fifths!=0 and previous.fifths!=context.fifths)
        key_signature(profile,previous.fifths,context.clef,true)^ else []
    let cancel_width=if (len(cancelled)==0) 0.0 else d.union([for (s in cancelled) s.bounds]).right+0.4
    let keys=if (show_key and context.fifths!=0) key_signature(profile,context.fifths,context.clef)^ else []
    let key_width=if (len(keys)==0) 0.0 else d.union([for (s in keys) s.bounds]).right+0.7
    let x2=x1+cancel_width+key_width
    let meter=if (show_meter and not contains(["common","cut"],context.meter.symbol)) time_signature(profile,context.meter)^ else {shapes:[]}
    let common=if (show_meter and contains(["common","cut"],context.meter.symbol))
        d.glyph(profile,if (context.meter.symbol=="common") "timeSigCommon" else "timeSigCutCommon",x2,2.0)^ else null
    let shapes=[*if (clef!=null) [clef] else [], *d.move_all(cancelled,x1,0.0),*d.move_all(keys,x1+cancel_width,0.0),
        *d.move_all(meter.shapes,x2,0.0),
        *if (common!=null) [common] else []];
    {width:if (len(shapes)==0) 0.0 else d.union([for (s in shapes) s.bounds]).right+1.0,shapes:shapes}
}
