// D10.2.2v2: segment lookup preserves source offsets at and between boundaries.
import rebuild: lambda.doc.latex.tex_engine.reconstruct
let segments = [
    {out_start:0,length:3,src_start:10,synthesized:false},
    {out_start:3,length:2,src_start:99,synthesized:true},
    {out_start:5,length:3,src_start:20,synthesized:false}
];
[for (offset in [-1,0,2,3,4,5,7,8,9]) rebuild.map_offset(segments,offset)^];
rebuild.map_offset([],42)^;
rebuild.map_offset(segments,0,1)^
