// D10.2.2v2: segment lookup preserves source offsets at and between boundaries.
import rebuild: lambda.doc.latex.tex_engine.reconstruct
let segments = [
    {out_start:0,length:3,src_start:10,synthesized:false},
    {out_start:3,length:2,src_start:99,synthesized:true},
    {out_start:5,length:3,src_start:20,synthesized:false}
];
[for (offset in [-1,0,2,3,4,5,7,8,9]) rebuild.map_offset(segments,offset)^];
rebuild.map_offset([],42)^;
rebuild.map_offset(segments,0,1)^;

// Non-adjacent pieces need a separator only after a control word with an odd escape run.
fn fragments(parts: string[]) map^ {
    let tokens = [for (i,part in parts) {kind:"char", value:"?", cat:12, start:i, end:i + 1,
        reconstruct_text:part, reconstruct_count:1, emit_origin:i}];
    let built = rebuild.reconstruct({output:tokens, sources:[], main_file:0}, true)^;
    {text:built.text, segments:[for (segment in built.segments) segment.out_start],
        tokens:[for (span in built.token_spans) span.start]}
}
fragments(["\\", "alpha", "beta"])^;
fragments(["\\", "\\alpha", "beta"])^;
fragments(["é\\", "a", "Z"])^
