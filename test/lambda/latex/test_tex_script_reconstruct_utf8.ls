import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let source="café";
let stale={issue:null,main_file:1,sources:[{id:0,data:source}],
    output:[{kind:"char",value:"x",cat:12,file:0,start:3,end:4,direct:false,emit_origin:0}]};
let rebuilt=reconstruct.reconstruct(stale)^;
{issue:rebuilt.issue,text:rebuilt.text}
let broken_gap={issue:null,main_file:0,sources:[{id:0,data:source}],output:[
    {kind:"char",value:"f",cat:11,file:0,start:2,end:3,direct:true,seq:1},
    {kind:"char",value:"x",cat:12,file:0,start:4,end:5,direct:true,seq:2}]};
let guarded=reconstruct.reconstruct(broken_gap)^;
{issue:guarded.issue}
