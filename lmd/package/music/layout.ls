// Exact score time becomes floating staff-space geometry only at this boundary.
import c: .common
import t: .time
import f: .fonts
import n: .notation
import h: .header
import d: .draw

fn options_for(options) map^ {
    let valid=c.option_warnings(options,["width","scale","mode","page_height","parts","show_title","show_lyrics","respect_breaks","strict"])^
    let o={width:900.0,scale:1.0,mode:'scroll',page_height:1120.0,show_title:true,show_lyrics:true,respect_breaks:true,*:options}
    let checked=c.require(c.finite(o.width) and o.width>=100.0 and o.width<=20000.0 and
        c.finite(o.scale) and o.scale>=0.25 and o.scale<=8.0 and c.finite(o.page_height) and o.page_height>=100.0 and
        o.page_height<=30000.0 and contains(['scroll','pages'],o.mode) and o.show_title is bool and
        o.show_lyrics is bool and o.respect_breaks is bool and (o.parts==null or o.parts is array),"invalid layout options")^;
    o
}
fn prior_context(part,i,staff) => if (i==0) null else (
    let previous=part.measures[i-1],
    (if (len(previous.changes)>0) previous.changes[len(previous.changes)-1].context else previous.context).staffs[staff-1])
fn measure_headers(parts,i,profile,repeated) => [for (p in parts, j,st in p.measures[i].context.staffs) (
    let header=h.prepare(profile,st,prior_context(p,i,j+1),repeated)^,
    {*:header,part:p.id,staff:j+1})]
fn at_onset(items,onset) => [for (x in items where x.onset==onset) x]
fn measure_plan(parts,i,profile,options) map^ {
    let raw=[for (p in parts) {*:p.measures[i],part:p.id}]
    let events=n.prepare([for (m in raw,e in m.events) if (options.show_lyrics) e else {*:e,lyrics:[]}],profile)^
    let beams=n.beam_groups(events)^
    let headers=measure_headers(parts,i,profile,false)
    let repeated=measure_headers(parts,i,profile,true)
    let repeat_prefix=if (any([for (m in raw,b in m.barlines) b.location=="left" and b.repeat=="forward"])) 2.0 else 0.0
    let changes=[for (m in raw,j,change in m.changes,k,st in change.context.staffs where change.onset.num>0) (
        let previous=if (j==0) m.context else m.changes[j-1].context,
        {*:h.prepare(profile,st,previous.staffs[k],false)^,onset:change.onset,part:m.part,staff:k+1})]
    let directions=[for (m in raw,j,dir in m.directions) {*:dir,part:m.part,id:m.part++"/measure/"++string(i)++"/direction/"++string(j),
        widths:[for (entry in dir.entries) f.measure(profile,[entry.text],1.4)^[0].width]}]
    let times=[for (entry in n.ordered([for (at in unique([t.ZERO,*[for (e in events) e.onset],
        *[for (ch in changes) ch.onset],*[for (dir in directions) dir.onset]])) {onset:at}])) entry.onset]
    let columns=c.fold([for (j,at in times) {onset:at,index:j}],{end:0.0,columns:[]},(acc,slot)=>(
        let notes=at_onset(events,slot.onset),
        let grace=[for (e in notes where e.grace and not e.chord) e],
        let left=max([0.4,*[for (e in notes) e.left]])+float(len(grace))*2.0,
        let right=max([1.4,*[for (e in notes) e.right],*[for (dir in at_onset(directions,slot.onset),w in dir.widths) w]]),
        let prefix=max([0.0,*[for (ch in at_onset(changes,slot.onset)) ch.width]]),
        let next=times[slot.index+1] or raw[0].span,
        let weight=sqrt(max(0.0625,t.value(t.sub(next,slot.onset)^))),
        let x=acc.end+prefix+left,
        {end:x+right+weight,columns:[*acc.columns,{onset:slot.onset,x:x,left:left,right:right,prefix:prefix,weight:weight}]}));
    {index:i,label:raw[0].label,raw:raw,events:events,beams:beams,headers:headers,repeated:repeated,changes:changes,
        directions:directions,columns:columns.columns,body_width:columns.end+0.8,
        prefix:repeat_prefix+max([0.0,*[for (x in headers) x.width]]),start_prefix:repeat_prefix+max([0.0,*[for (x in repeated) x.width]]),
        new_system:any([for (m in raw) m.new_system]),new_page:any([for (m in raw) m.new_page])}
}
fn systems(measures,available,options) => (
    let packed=c.fold(measures,{done:[],current:[],width:0.0},(acc,m)=>(
        let split=len(acc.current)>0 and (acc.width+m.prefix+m.body_width>available or
            options.respect_breaks and (m.new_system or m.new_page)),
        {done:if (split) [*acc.done,{measures:acc.current,width:acc.width}] else acc.done,
         current:if (split) [m] else [*acc.current,m],
         width:(if (split) 0.0 else acc.width)+m.body_width+(if (split or len(acc.current)==0) m.start_prefix else m.prefix)})),
    [*packed.done,{measures:packed.current,width:packed.width}])
fn staff_plan(parts,system) => c.fold([for (p in parts,i in 1 to len(p.measures[0].context.staffs)) {part:p.id,staff:i}],
    {bottom:0.0,staffs:[]},(acc,key)=>(
        let events=[for (m in system.measures,e in m.events where e.part==key.part and e.staff==key.staff) e],
        let top=min([-3.0,*[for (e in events) min(e.y+e.glyph.ink.top,
            if (not e.rest and e.stem_direction=="up") e.y-4.0 else e.y-1.0)]]),
        let bottom=max([7.0,*[for (e in events) max([e.y+e.glyph.ink.bottom,
            if (not e.rest and e.stem_direction=="down") e.y+4.0 else e.y+1.0,
            if (len(e.lyrics)>0) max(7.5,e.y+3.0)+float(len(e.lyrics)-1)*1.7 else 0.0])]]),
        let y=acc.bottom-top,
        {bottom:y+bottom+2.0,staffs:[*acc.staffs,{*:key,y:y,top:top,bottom:bottom}]}))
pub fn staff_y(staffs,part,staff) => [for (s in staffs where s.part==part and s.staff==staff) s.y][0]
fn place_system(system,parts,available,left,profile,is_last) map^ {
    let vertical=staff_plan(parts,system)
    let justify=not is_last or system.width>available*0.7
    let extra=if (justify) max(0.0,available-system.width)/float(len(system.measures)) else 0.0
    let result=c.fold([for (i,m in system.measures) {m:m,index:i}],{x:left,measures:[],events:[],directions:[]},(acc,row)=>(
        let m=row.m,
        let prefix=if (row.index==0) m.start_prefix else m.prefix,
        let total_weight=sum([for (col in m.columns) col.weight]),
        let columns=[for (i,col in m.columns) {*:col,x:acc.x+prefix+col.x+
            extra*sum([for (prior in slice(m.columns,0,i)) prior.weight])/total_weight}],
        let width=prefix+m.body_width+extra,
        let events=[for (e in m.events) (
            let column=at_onset(columns,e.onset)[0],
            let grace=[for (g in m.events where g.part==e.part and g.staff==e.staff and g.voice==e.voice and
                g.onset==e.onset and g.grace and not g.chord) g.anchor],
            let grace_x=if (e.grace) -2.0*float(len(grace)-index_of(grace,e.anchor)) else 0.0,
            {*:e,x:column.x+e.dx+grace_x,y:staff_y(vertical.staffs,e.part,e.staff)+e.y,local_y:e.y,
                staff_y:staff_y(vertical.staffs,e.part,e.staff)})],
        {x:acc.x+width,events:[*acc.events,*events],directions:[*acc.directions,*[for (dir in m.directions)
            {*:dir,x:at_onset(columns,dir.onset)[0].x,staff_y:staff_y(vertical.staffs,dir.part,dir.staff)}],
            *[for (raw in m.raw,j,bar in raw.barlines where bar.ending!=null)
                {id:raw.part++"/measure/"++string(m.index)++"/ending/"++string(j),part:raw.part,staff:1,entries:[],
                 x:if (bar.ending.type=="start") acc.x else acc.x+width,staff_y:staff_y(vertical.staffs,raw.part,1)}]],
         measures:[*acc.measures,{*:m,x:acc.x,width:width,columns:columns,placed_prefix:prefix,
            headers:if (row.index==0) m.repeated else m.headers,events:events}]}));
    {*:result,staffs:vertical.staffs,height:vertical.bottom,width:result.x-left,left:left,
        new_page:system.measures[0].new_page,beams:[for (m in system.measures,b in m.beams) b]}
}
fn pages_for(systems,options,unit) => (
    let top=if (options.show_title) 10.0 else 3.0,
    let result=c.fold([for (i,s in systems) {s:s,index:i}],{page:0,y:top,systems:[]},(acc,row)=>(
        let s=row.s,
        let next=options.mode=='pages' and row.index>0 and
            (acc.y+s.height>options.page_height/unit-3.0 or options.respect_breaks and s.new_page),
        let y=if (next) 3.0 else acc.y,
        let page=acc.page+(if (next) 1 else 0),
        {page:page,y:y+s.height+3.0,systems:[*acc.systems,{*:s,index:row.index,page:page,y:y}]})),
    {systems:result.systems,pages:[for (p in 0 to result.page) (
        let bottom=max([for (s in result.systems where s.page==p) s.y+s.height])+3.0,
        {index:p,width:max([options.width/unit,*[for (s in result.systems where s.page==p) s.left+s.width+3.0]]),
         height:if (options.mode=='pages') max(options.page_height/unit,bottom) else bottom})]})
pub fn layout(score, options = {}) map^ {
    let valid=c.require(score.kind=="music-score" and score.version==1,"expected a music Score")^
    let o=options_for(options)^
    let ids=[for (p in score.parts) p.id]
    let valid_parts=c.require(o.parts==null or len(o.parts)>0 and len(unique(o.parts))==len(o.parts) and
        all([for (p in o.parts) contains(ids,p)]),"invalid part selection")^
    let parts=[for (p in score.parts where o.parts==null or contains(o.parts,p.id)) p]
    let stable_staffs=c.require(all([for (p in parts,m in p.measures) len(m.context.staffs)==len(p.measures[0].context.staffs)]),
        "changing staff counts are not supported")^
    let profile=f.prepare()^
    let label_width=max([0.0,*[for (p in parts) f.measure(profile,[p.label],1.4)^[0].width]])+2.0
    let unit=10.0*o.scale
    let left=3.0+label_width
    let available=max(1.0,o.width/unit-left-3.0)
    let measures=[for (i,m in score.measures) measure_plan(parts,i,profile,o)^]
    let broken=systems(measures,available,o)
    let placed=[for (i,s in broken) place_system(s,parts,available,left,profile,i==len(broken)-1)^]
    let paged=pages_for(placed,o,unit)
    let warnings=c.warnings([*score.diagnostics,
        *c.option_warnings(options,["width","scale","mode","page_height","parts","show_title","show_lyrics","respect_breaks","strict"])^,
        // compare natural widths; floating justification can round past the available width.
        *[for (s in broken where s.width>available) c.diagnostic("overflow","system exceeds requested width",s.measures[0].label)],
        *[for (page in paged.pages where o.mode=='pages' and page.height>o.page_height/unit)
            c.diagnostic("overflow","system exceeds requested page height",string(page.index))]],o)^;
    {kind:"music-layout",version:1,score:score,options:o,profile:profile,unit:unit,parts:parts,
        pages:paged.pages,systems:paged.systems,diagnostics:warnings}
}
